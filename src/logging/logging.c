/*
 * Copyright (C) 2026 Antonio Ricciardi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "logging.h"
#include "../config/config.h"
#include "../runtime/runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int verbose;
static FILE *log_file_stream;

static void write_log(const char *level, const char *component,
                      const char *format, va_list arguments)
{
    char timestamp[32] = "0000-00-00T00:00:00.000Z";
#ifdef _WIN32
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    snprintf(timestamp, sizeof(timestamp),
             "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             (unsigned int)utc.wYear, (unsigned int)utc.wMonth,
             (unsigned int)utc.wDay, (unsigned int)utc.wHour,
             (unsigned int)utc.wMinute, (unsigned int)utc.wSecond,
             (unsigned int)utc.wMilliseconds);
#else
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) == TIME_UTC) {
        struct tm utc;
        if (gmtime_r(&now.tv_sec, &utc) &&
            strftime(timestamp, 20, "%Y-%m-%dT%H:%M:%S", &utc) == 19)
            snprintf(timestamp + 19, sizeof(timestamp) - 19, ".%03uZ",
                     (unsigned int)(now.tv_nsec / 1000000));
    }
#endif

    char message[2048];
    va_list arguments_copy;
    va_copy(arguments_copy, arguments);
    int result = vsnprintf(message, sizeof(message), format, arguments);
    if (result < 0)
        memcpy(message, "Unable to format log message",
               sizeof("Unable to format log message"));
    fprintf(stderr, "%s %-5s [%s] %s\n", timestamp, level, component,
            message);
    fflush(stderr);

    if (log_file_stream) {
        fprintf(log_file_stream, "%s %-5s [%s] %s\n", timestamp, level, component,
                message);
        fflush(log_file_stream);
    }
    va_end(arguments_copy);
}

static void log_message(const char *level, const char *component,
                        const char *format, va_list arguments)
{
    write_log(level, component, format, arguments);
}

void c2t_log_init(void)
{
    verbose = c2t_config_get()->verbose;
    const char *path = c2t_runtime_log_path();
    if (path && !log_file_stream) {
#ifdef _WIN32
        if (_isatty(_fileno(stderr)))
            log_file_stream = fopen(path, "ab");
#else
        if (isatty(2))
            log_file_stream = fopen(path, "ab");
#endif
    }
}

int c2t_log_is_verbose(void)
{
    return verbose;
}

void c2t_log_error(const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    log_message("ERROR", component, format, arguments);
    va_end(arguments);
}

void c2t_log_warning(const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    log_message("WARN", component, format, arguments);
    va_end(arguments);
}

void c2t_log_info(const char *component, const char *format, ...)
{
    if (!verbose)
        return;
    va_list arguments;
    va_start(arguments, format);
    log_message("INFO", component, format, arguments);
    va_end(arguments);
}

void c2t_log_debug(const char *component, const char *format, ...)
{
    if (!verbose)
        return;
    va_list arguments;
    va_start(arguments, format);
    log_message("DEBUG", component, format, arguments);
    va_end(arguments);
}
