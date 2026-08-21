#include "clipboard.h"
#include "clipboard_output.h"
#include "../config/config.h"
#include "../logging/logging.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define C2T_PASTEBOARD_POLL_MS 200

static void copy_utf8(NSString *value, char *output, size_t capacity)
{
    output[0] = '\0';
    if (!value || capacity < 2)
        return;

    NSData *encoded = [value dataUsingEncoding:NSUTF8StringEncoding];
    if (!encoded)
        return;
    size_t copied = MIN((size_t)encoded.length, capacity - 1);
    const unsigned char *bytes = encoded.bytes;
    while (copied > 0 && copied < (size_t)encoded.length &&
           (bytes[copied] & 0xc0) == 0x80)
        --copied;
    memcpy(output, bytes, copied);
    output[copied] = '\0';
}

static int capture_source(c2t_clipboard_source_t *source)
{
    memset(source, 0, sizeof(*source));
    if (!c2t_config_get()->telegram_send_window_info)
        return 0;

    NSRunningApplication *application =
        NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!application)
        return 0;
    source->process_id = (uint32_t)application.processIdentifier;
    NSString *application_name = application.localizedName;
    if (!application_name)
        application_name = application.bundleURL.lastPathComponent;
    copy_utf8(application_name, source->application,
              sizeof(source->application));

    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    for (NSDictionary *window in (__bridge NSArray *)windows) {
        NSNumber *owner = window[(id)kCGWindowOwnerPID];
        NSNumber *layer = window[(id)kCGWindowLayer];
        if (owner.unsignedIntValue == source->process_id &&
            layer.integerValue == 0) {
            copy_utf8(window[(id)kCGWindowName], source->title,
                      sizeof(source->title));
            if (source->title[0])
                break;
        }
    }
    if (windows)
        CFRelease(windows);

    c2t_log_debug("macos", "Captured source application: app=%s, title=%s, "
                  "pid=%lu",
                  source->application[0] ? source->application : "unknown",
                  source->title[0] ? source->title : "unavailable",
                  (unsigned long)source->process_id);
    return source->application[0] || source->title[0] || source->process_id;
}

static int output_files(NSPasteboard *pasteboard,
                        const c2t_clipboard_source_t *source)
{
    if (!c2t_config_get()->telegram_send_files)
        return 0;

    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey: @YES};
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[NSURL.class]
                                                       options:options];
    int handled = 0;
    for (NSURL *url in urls) {
        if (!url.fileURL)
            continue;
        NSData *value = [url.absoluteString dataUsingEncoding:NSUTF8StringEncoding];
        if (!value || value.length > c2t_config_get()->queue_max_bytes)
            continue;
        clipboard_output(value.bytes, value.length, "text/uri-list", source);
        handled = 1;
    }
    return handled;
}

static int output_data(NSPasteboard *pasteboard, NSString *type,
                       const char *mime_type,
                       const c2t_clipboard_source_t *source)
{
    NSData *data = [pasteboard dataForType:type];
    if (!data || data.length > c2t_config_get()->queue_max_bytes)
        return 0;
    clipboard_output(data.bytes, data.length, mime_type, source);
    return 1;
}

static int output_image(NSPasteboard *pasteboard,
                        const c2t_clipboard_source_t *source)
{
    static NSString *const direct_types[] = {
        @"public.png", @"public.jpeg", @"public.webp",
        @"com.compuserve.gif", @"com.microsoft.bmp"
    };
    static const char *const mime_types[] = {
        "image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp"
    };
    NSArray<NSPasteboardType> *types = pasteboard.types;
    for (size_t index = 0;
         index < sizeof(direct_types) / sizeof(direct_types[0]); ++index) {
        if ([types containsObject:direct_types[index]] &&
            output_data(pasteboard, direct_types[index], mime_types[index],
                        source))
            return 1;
    }

    NSData *tiff = [pasteboard dataForType:NSPasteboardTypeTIFF];
    NSBitmapImageRep *image = tiff
        ? [NSBitmapImageRep imageRepWithData:tiff] : nil;
    NSData *png = image
        ? [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        : nil;
    if (!png || png.length > c2t_config_get()->queue_max_bytes)
        return 0;
    clipboard_output(png.bytes, png.length, "image/png", source);
    return 1;
}

static int output_text_type(NSPasteboard *pasteboard, NSString *type,
                            const char *mime_type,
                            const c2t_clipboard_source_t *source)
{
    NSString *text = [pasteboard stringForType:type];
    NSData *utf8 = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (!utf8 || utf8.length > c2t_config_get()->queue_max_bytes)
        return 0;
    clipboard_output(utf8.bytes, utf8.length, mime_type, source);
    return 1;
}

static void output_clipboard(NSPasteboard *pasteboard)
{
    c2t_clipboard_source_t source;
    const c2t_clipboard_source_t *metadata =
        capture_source(&source) ? &source : NULL;

    if (output_files(pasteboard, metadata) || output_image(pasteboard, metadata))
        return;
    NSArray<NSPasteboardType> *types = pasteboard.types;
    if ([types containsObject:@"public.vcard"] &&
        output_data(pasteboard, @"public.vcard", "text/vcard", metadata))
        return;
    if ([types containsObject:NSPasteboardTypeString])
        (void)output_text_type(pasteboard, NSPasteboardTypeString,
                               "text/plain;charset=utf-8", metadata);
    else
        c2t_log_warning("macos", "Clipboard has no supported content type");
}

int clipboard_listen(void)
{
    @autoreleasepool {
        NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
        NSInteger change_count = pasteboard.changeCount;
        c2t_log_info("macos", "Listening for clipboard changes");

        for (;;) {
            struct timespec delay = {
                .tv_sec = 0,
                .tv_nsec = C2T_PASTEBOARD_POLL_MS * 1000000L
            };
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
            }

            @autoreleasepool {
                NSInteger next = pasteboard.changeCount;
                if (next != change_count) {
                    change_count = next;
                    c2t_log_debug("macos", "Clipboard change detected");
                    output_clipboard(pasteboard);
                }
            }
        }
    }
}
