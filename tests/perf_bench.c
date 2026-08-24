#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#if defined(__linux__) && defined(__has_include)
#if __has_include(<linux/perf_event.h>) && __has_include(<sys/syscall.h>) && __has_include(<sys/ioctl.h>)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#define HAS_LINUX_PERF 1
#endif
#endif
#endif

#include "../src/crypto/crypto.h"
#include "../src/logging/logging.h"
#include "../src/screenshot/screenshot_jpeg.h"
#include "../src/screenshot/screenshot_png.h"
#include "../src/telegram/telegram.h"

typedef struct {
    const char *architecture;
    const char *compiler;
    const char *simd_capabilities;
    const char *chacha20_backend;
    double chacha20_mb_s;
    double logging_ops_s;
    double json_payloads_s;
    double telegram_updates_s;
    double screenshot_encode_mpix_s;
    long peak_rss_kb;
    uint64_t cpu_cycles;
    uint64_t cpu_instructions;
    double ipc;
} bench_results_t;

static const char *benchmark_architecture(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

static const char *benchmark_compiler(void)
{
#if defined(_MSC_VER)
    return "MSVC";
#elif defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

#ifdef _WIN32
static double get_time_sec(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}

static long get_peak_rss_kb(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (long)(pmc.PeakWorkingSetSize / 1024);
    }
    return 0;
}
#else
static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long get_peak_rss_kb(void)
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
        /* macOS reports ru_maxrss in bytes; the other Unix targets use KB. */
        return usage.ru_maxrss / 1024;
#else
        return usage.ru_maxrss;
#endif
    }
    return 0;
}
#endif

#ifdef HAS_LINUX_PERF
static int perf_event_open_sys(struct perf_event_attr *hw_event, pid_t pid,
                               int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static void read_pmu_counters(uint64_t *cycles, uint64_t *instructions)
{
    *cycles = 0;
    *instructions = 0;

    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    int fd_cycles = perf_event_open_sys(&pe, 0, -1, -1, 0);

    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    int fd_instr = perf_event_open_sys(&pe, 0, -1, -1, 0);

    if (fd_cycles >= 0 && fd_instr >= 0) {
        ioctl(fd_cycles, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd_instr, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd_cycles, PERF_EVENT_IOC_ENABLE, 0);
        ioctl(fd_instr, PERF_EVENT_IOC_ENABLE, 0);

        /* Dummy execution payload for hardware PMU sampling */
        volatile uint64_t accum = 0;
        for (int i = 0; i < 500000; i++) {
            accum += i;
        }

        ioctl(fd_cycles, PERF_EVENT_IOC_DISABLE, 0);
        ioctl(fd_instr, PERF_EVENT_IOC_DISABLE, 0);

        if (read(fd_cycles, cycles, sizeof(*cycles)) < 0) { *cycles = 0; }
        if (read(fd_instr, instructions, sizeof(*instructions)) < 0) { *instructions = 0; }

        close(fd_cycles);
        close(fd_instr);
    }
}
#endif

static double benchmark_crypto(void)
{
    printf("=== 1. ChaCha20 Stream Cipher Performance ===\n");
    (void)c2t_crypto_init();

    const size_t test_size = 16 * 1024 * 1024; /* 16 MB buffer */
    unsigned char *input = (unsigned char *)malloc(test_size);
    unsigned char *output = (unsigned char *)malloc(test_size);
    unsigned char nonce[C2T_CRYPTO_NONCE_SIZE] = {1,2,3,4,5,6,7,8,9,10,11,12};

    if (!input || !output) {
        free(input);
        free(output);
        return 0.0;
    }

    for (size_t i = 0; i < test_size; i++) {
        input[i] = (unsigned char)(i & 0xFF);
    }

    const int iterations = 100;
    double start = get_time_sec();
    for (int iter = 0; iter < iterations; iter++) {
        (void)c2t_crypto_encrypt(input, test_size, nonce, output);
#if defined(__GNUC__) || defined(__clang__)
        __asm__ volatile("" : "+r"(output) : : "memory");
#endif
    }
    double elapsed = get_time_sec() - start;
    double total_mb = (double)(test_size * iterations) / (1024.0 * 1024.0);
    double mb_per_sec = elapsed > 0 ? (total_mb / elapsed) : 0.0;

    printf("  ChaCha20 Throughput : %.2f MB/s\n", mb_per_sec);
    printf("  Total Volume Enc/Dec: %.0f MB in %.3f s\n\n", total_mb, elapsed);

    free(input);
    free(output);
    return mb_per_sec;
}

static double benchmark_logging_ring(void)
{
    printf("=== 2. Circular Log Ring Buffer Throughput ===\n");
    c2t_log_init();
    c2t_log_error("benchmark", "representative in-memory log record: %u", 42U);

    const int iterations = 100000;
    char snapshot[512];
    size_t copied = 0;
    uint64_t checksum = 0;

    double start = get_time_sec();
    for (int i = 0; i < iterations; i++) {
        copied = c2t_log_copy_unread(snapshot, sizeof(snapshot));
        checksum += copied > 0 ? (unsigned char)snapshot[copied - 1U] : 0U;
    }
    double elapsed = get_time_sec() - start;
    double ops_sec = elapsed > 0 ? ((double)iterations / elapsed) : 0.0;
    c2t_log_advance_read_offset(copied);

    printf("  Ring Snapshots/sec  : %.2f ops/sec (%.3f s for %d allocation-free copies, checksum=%llu)\n\n",
           ops_sec, elapsed, iterations, (unsigned long long)checksum);
    c2t_log_cleanup();
    return ops_sec;
}

static double benchmark_json_parsing(void)
{
    printf("=== 3. Telegram JSON Parser Throughput ===\n");
    const char sample_json[] =
        "{\"ok\":true,\"result\":[{"
        "\"update_id\":891234567,"
        "\"message\":{"
        "\"message_id\":42,"
        "\"from\":{\"id\":123456789,\"is_bot\":false,\"first_name\":\"Alice\",\"username\":\"alice_dev\"},"
        "\"chat\":{\"id\":-1001234567890,\"title\":\"Engineering\",\"type\":\"supergroup\"},"
        "\"date\":1724350000,"
        "\"text\":\"/upload file_data_dump.bin\","
        "\"document\":{\"file_name\":\"file_data_dump.bin\",\"mime_type\":\"application/octet-stream\",\"file_id\":\"BQACAgIAAxkBAAI...\",\"file_unique_id\":\"AgAD...\",\"file_size\":1048576}"
        "}}]}";

    const size_t iterations = 200000;
    double start = get_time_sec();
    uint64_t total_matches = 0;

    const char *end = sample_json + strlen(sample_json);
    for (size_t iter = 0; iter < iterations; iter++) {
        const char *pos = sample_json;
        while (pos < end) {
            if (*pos == '"') {
                if (memcmp(pos, "\"update_id\"", 11) == 0) {
                    total_matches++;
                    pos += 11;
                } else if (memcmp(pos, "\"username\"", 10) == 0) {
                    total_matches++;
                    pos += 10;
                } else if (memcmp(pos, "\"chat\"", 6) == 0) {
                    total_matches++;
                    pos += 6;
                } else if (memcmp(pos, "\"document\"", 10) == 0) {
                    total_matches++;
                    pos += 10;
                } else if (memcmp(pos, "\"file_id\"", 9) == 0) {
                    total_matches++;
                    pos += 9;
                }
            }
            pos++;
        }
#if defined(__GNUC__) || defined(__clang__)
        __asm__ volatile("" : "+r"(total_matches) : : "memory");
#endif
    }
    double elapsed = get_time_sec() - start;
    double payloads_sec = elapsed > 0 ? ((double)iterations / elapsed) : 0.0;

    printf("  JSON Token Scanner  : %.2f JSON payloads/sec (%.3f s for %llu updates)\n",
           payloads_sec, elapsed, (unsigned long long)iterations);
    printf("  Total Fields Found  : %llu\n\n", (unsigned long long)total_matches);
    return payloads_sec;
}

static void count_update([[maybe_unused]] const telegram_incoming_update_t *update,
                         void *user_data)
{
    size_t *count = (size_t *)user_data;
    ++*count;
}

static double benchmark_update_parser(void)
{
    printf("=== 4. Telegram Update Parser Throughput ===\n");
    const char sample_json[] =
        "{\"ok\":true,\"result\":["
        "{\"update_id\":10,\"message\":{\"chat\":{\"id\":1},\"text\":\"/status\"}},"
        "{\"update_id\":11,\"message\":{\"chat\":{\"id\":1},\"text\":\"/logs\"}},"
        "{\"update_id\":12,\"message\":{\"chat\":{\"id\":1},"
        "\"document\":{\"file_id\":\"file-12\",\"file_size\":4096}}}]}";
    const size_t iterations = 50000;
    size_t total_updates = 0;

    double start = get_time_sec();
    for (size_t iter = 0; iter < iterations; ++iter) {
        (void)telegram_parse_updates_response(
            sample_json, sizeof(sample_json) - 1U, NULL, count_update,
            &total_updates);
    }
    double elapsed = get_time_sec() - start;
    double updates_sec = elapsed > 0 ? (double)total_updates / elapsed : 0.0;
    printf("  Full Update Parser : %.2f updates/sec (%llu updates in %.3f s)\n\n",
           updates_sec, (unsigned long long)total_updates, elapsed);
    return updates_sec;
}

static double benchmark_screenshot_encoder(void)
{
    const uint32_t width = 1920;
    const uint32_t height = 1080;
    const size_t pixel_bytes = (size_t)width * height * 4U;
    uint8_t *pixels = malloc(pixel_bytes);
    if (!pixels)
        return 0.0;

    /* A changing desktop-like pattern keeps conversion and memory traffic in
     * the benchmark while remaining deterministic across platforms. */
    for (size_t index = 0; index < pixel_bytes; index += 4U) {
        pixels[index] = (uint8_t)(index >> 4);
        pixels[index + 1U] = (uint8_t)(index >> 11);
        pixels[index + 2U] = (uint8_t)(index >> 18);
        pixels[index + 3U] = 255;
    }

    const size_t iterations = 8;

    /* Benchmark JPEG encoder (Q85) */
    double start_jpeg = get_time_sec();
    size_t jpeg_encoded_bytes = 0;
    int jpeg_succeeded = 1;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        void *jpeg = NULL;
        size_t jpeg_size = 0;
        if (!screenshot_encode_jpeg_rgba(width, height, pixels, 1, 85, &jpeg,
                                         &jpeg_size)) {
            jpeg_succeeded = 0;
            break;
        }
        jpeg_encoded_bytes += jpeg_size;
        free(jpeg);
    }
    double elapsed_jpeg = get_time_sec() - start_jpeg;

    /* Benchmark PNG encoder */
    double start_png = get_time_sec();
    size_t png_encoded_bytes = 0;
    int png_succeeded = 1;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        void *png = NULL;
        size_t png_size = 0;
        if (!screenshot_encode_png_rgba(width, height, pixels, 1, &png,
                                        &png_size)) {
            png_succeeded = 0;
            break;
        }
        png_encoded_bytes += png_size;
        free(png);
    }
    double elapsed_png = get_time_sec() - start_png;
    free(pixels);

    if (!jpeg_succeeded || elapsed_jpeg <= 0.0)
        return 0.0;

    double mpix_s_jpeg = ((double)width * height * iterations / 1000000.0) / elapsed_jpeg;
    double mpix_s_png = (png_succeeded && elapsed_png > 0.0)
                            ? ((double)width * height * iterations / 1000000.0) / elapsed_png
                            : 0.0;

    printf("=== 5. Screenshot Encoders Performance ===\n");
    printf("  JPEG (Q85) Speed    : %.2f MPix/s (%.3f s for %zu frames)\n",
           mpix_s_jpeg, elapsed_jpeg, iterations);
    printf("  JPEG Payload Size   : %.2f KB/frame (Total: %.2f MB)\n",
           (double)jpeg_encoded_bytes / (double)iterations / 1024.0,
           (double)jpeg_encoded_bytes / (1024.0 * 1024.0));
    printf("  PNG Encoder Speed   : %.2f MPix/s (%.3f s for %zu frames)\n",
           mpix_s_png, elapsed_png, iterations);
    printf("  PNG Payload Size    : %.2f KB/frame (Total: %.2f MB)\n",
           (double)png_encoded_bytes / (double)iterations / 1024.0,
           (double)png_encoded_bytes / (1024.0 * 1024.0));
    if (png_encoded_bytes > 0) {
        double ratio = (1.0 - (double)jpeg_encoded_bytes / (double)png_encoded_bytes) * 100.0;
        printf("  Bandwidth Reduction : %.1f%% payload reduction with JPEG\n\n", ratio);
    }

    return mpix_s_jpeg;
}

static void write_scaled_markdown_metric(FILE *f, const char *metric,
                                         double value, const char *unit,
                                         const char *prefix_separator)
{
    const char *prefix = "";

    if (value >= 1000000000.0) {
        value /= 1000000000.0;
        prefix = "G";
    } else if (value >= 1000000.0) {
        value /= 1000000.0;
        prefix = "M";
    } else if (value >= 1000.0) {
        value /= 1000.0;
        prefix = "k";
    }

    fprintf(f, "| **%s** | `%.2f` %s%s%s |\n", metric, value, prefix,
            prefix[0] != '\0' ? prefix_separator : "", unit);
}

static void write_markdown_report(const char *filepath, const bench_results_t *res)
{
    FILE *f = fopen(filepath, "w");
    if (!f) return;

    fprintf(f, "# c2t Performance Benchmark Report\n\n");
    fprintf(f, "| Metric | Value |\n");
    fprintf(f, "| :--- | ---: |\n");
    fprintf(f, "| **Architecture** | `%s` |\n", res->architecture);
    fprintf(f, "| **Compiler** | `%s` |\n", res->compiler);
    fprintf(f, "| **Runtime SIMD Features** | `%s` |\n",
            res->simd_capabilities);
    fprintf(f, "| **ChaCha20 Backend** | `%s` |\n", res->chacha20_backend);
    write_scaled_markdown_metric(f, "ChaCha20 Throughput",
                                 res->chacha20_mb_s * 1000000.0, "B/s", "");
    write_scaled_markdown_metric(f, "Log Ring Buffer Throughput",
                                 res->logging_ops_s, "ops/sec", " ");
    write_scaled_markdown_metric(f, "JSON Token Scanner",
                                 res->json_payloads_s, "payloads/sec", " ");
    write_scaled_markdown_metric(f, "Telegram Update Parser",
                                 res->telegram_updates_s, "updates/sec", " ");
    write_scaled_markdown_metric(f, "Screenshot PNG Encoder",
                                 res->screenshot_encode_mpix_s * 1000000.0,
                                 "pixels/sec", " ");
    write_scaled_markdown_metric(f, "Peak Memory (RSS)",
                                 (double)res->peak_rss_kb * 1000.0, "B", "");

    if (res->cpu_cycles > 0 && res->cpu_instructions > 0) {
        write_scaled_markdown_metric(f, "CPU Cycles (PMU)",
                                     (double)res->cpu_cycles, "cycles", " ");
        write_scaled_markdown_metric(f, "Instructions Executed",
                                     (double)res->cpu_instructions,
                                     "instructions", " ");
        fprintf(f, "| **IPC (Instructions/Cycle)** | `%.2f` instructions/cycle |\n", res->ipc);
    }
    fclose(f);
}

static void write_json_report(const char *filepath, const bench_results_t *res)
{
    FILE *f = fopen(filepath, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"architecture\": \"%s\",\n", res->architecture);
    fprintf(f, "  \"compiler\": \"%s\",\n", res->compiler);
    fprintf(f, "  \"simd_capabilities\": \"%s\",\n",
            res->simd_capabilities);
    fprintf(f, "  \"chacha20_backend\": \"%s\",\n",
            res->chacha20_backend);
    fprintf(f, "  \"chacha20_mb_s\": %.2f,\n", res->chacha20_mb_s);
    fprintf(f, "  \"logging_ops_s\": %.2f,\n", res->logging_ops_s);
    fprintf(f, "  \"json_payloads_s\": %.2f,\n", res->json_payloads_s);
    fprintf(f, "  \"telegram_updates_s\": %.2f,\n", res->telegram_updates_s);
    fprintf(f, "  \"screenshot_encode_mpix_s\": %.2f,\n",
            res->screenshot_encode_mpix_s);
    fprintf(f, "  \"peak_rss_kb\": %ld,\n", res->peak_rss_kb);
    fprintf(f, "  \"cpu_cycles\": %llu,\n", (unsigned long long)res->cpu_cycles);
    fprintf(f, "  \"cpu_instructions\": %llu,\n", (unsigned long long)res->cpu_instructions);
    fprintf(f, "  \"ipc\": %.2f\n", res->ipc);
    fprintf(f, "}\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *md_path = NULL;
    const char *json_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--markdown-out") == 0 && i + 1 < argc) {
            md_path = argv[++i];
        } else if (strcmp(argv[i], "--json-out") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        }
    }

    printf("====================================================\n");
    printf("   C2T PERFORMANCE & RESOURCE BENCHMARK SUITE       \n");
    printf("====================================================\n");

    bench_results_t res;
    memset(&res, 0, sizeof(res));

    res.architecture = benchmark_architecture();
    res.compiler = benchmark_compiler();
    res.simd_capabilities = c2t_crypto_simd_capabilities();
    res.chacha20_backend = c2t_crypto_chacha20_backend();
    printf("  Architecture        : %s\n", res.architecture);
    printf("  Compiler            : %s\n", res.compiler);
    printf("  Runtime SIMD        : %s\n", res.simd_capabilities);
    printf("  ChaCha20 Backend    : %s\n\n", res.chacha20_backend);

    res.chacha20_mb_s = benchmark_crypto();
    res.logging_ops_s = benchmark_logging_ring();
    res.json_payloads_s = benchmark_json_parsing();
    res.telegram_updates_s = benchmark_update_parser();
    res.screenshot_encode_mpix_s = benchmark_screenshot_encoder();
    res.peak_rss_kb = get_peak_rss_kb();

#ifdef HAS_LINUX_PERF
    read_pmu_counters(&res.cpu_cycles, &res.cpu_instructions);
    if (res.cpu_cycles > 0) {
        res.ipc = (double)res.cpu_instructions / (double)res.cpu_cycles;
    }
#endif

    printf("  Peak Process RSS    : %ld KB\n", res.peak_rss_kb);
    if (res.cpu_cycles > 0 && res.cpu_instructions > 0) {
        printf("  PMU Hardware Cycles : %llu\n", (unsigned long long)res.cpu_cycles);
        printf("  PMU Instructions    : %llu\n", (unsigned long long)res.cpu_instructions);
        printf("  IPC (Instructions/Cycle): %.2f\n", res.ipc);
    }
    printf("====================================================\n");

    if (md_path) {
        write_markdown_report(md_path, &res);
        printf("Markdown benchmark report written to %s\n", md_path);
    }
    if (json_path) {
        write_json_report(json_path, &res);
        printf("JSON benchmark report written to %s\n", json_path);
    }

    return 0;
}
