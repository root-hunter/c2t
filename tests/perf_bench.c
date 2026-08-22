#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

#include "../src/crypto/crypto.h"
#include "../src/logging/logging.h"

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
        return usage.ru_maxrss;
    }
    return 0;
}

static void benchmark_crypto(void)
{
    printf("=== 1. ChaCha20 Stream Cipher Performance ===\n");
    (void)c2t_crypto_init();

    const size_t test_size = 16 * 1024 * 1024; /* 16 MB buffer */
    unsigned char *input = malloc(test_size);
    unsigned char *output = malloc(test_size);
    unsigned char nonce[C2T_CRYPTO_NONCE_SIZE] = {1,2,3,4,5,6,7,8,9,10,11,12};

    for (size_t i = 0; i < test_size; i++) {
        input[i] = (unsigned char)(i & 0xFF);
    }

    const int iterations = 100;
    double start = get_time_sec();
    for (int iter = 0; iter < iterations; iter++) {
        (void)c2t_crypto_encrypt(input, test_size, nonce, output);
        __asm__ volatile("" : "+r"(output) : : "memory");
    }
    double elapsed = get_time_sec() - start;
    double total_mb = (double)(test_size * iterations) / (1024.0 * 1024.0);
    double mb_per_sec = total_mb / elapsed;

    printf("  ChaCha20 Throughput : %.2f MB/s\n", mb_per_sec);
    printf("  Total Volume Enc/Dec: %.0f MB in %.3f s\n\n", total_mb, elapsed);

    free(input);
    free(output);
}

static void benchmark_logging_ring(void)
{
    printf("=== 2. Circular Log Ring Buffer Throughput ===\n");
    c2t_log_init();

    const int iterations = 100000;
    char sample_data[256];
    snprintf(sample_data, sizeof(sample_data),
             "User logged in from remote session, clipboard captured 2048 bytes of text payload");

    double start = get_time_sec();
    for (int i = 0; i < iterations; i++) {
        /* Write to memory circular ring buffer */
        size_t dummy_len = 0;
        char *unread = c2t_log_get_unread(&dummy_len);
        if (unread) {
            c2t_log_advance_read_offset(dummy_len);
            free(unread);
        }
    }
    double elapsed = get_time_sec() - start;

    printf("  Ring Buffer Ops/sec : %.2f ops/sec (%.3f s for %d get_unread+advance cycles)\n\n",
           (double)iterations / elapsed, elapsed, iterations);
    c2t_log_cleanup();
}

static void benchmark_json_parsing(void)
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

    /* Token search simulation */
    const size_t iterations = 200000;
    double start = get_time_sec();
    uint64_t total_matches = 0;

    const char *end = sample_json + strlen(sample_json);
    for (size_t iter = 0; iter < iterations; iter++) {
        /* Parse key fields */
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
        __asm__ volatile("" : "+r"(total_matches) : : "memory");
    }
    double elapsed = get_time_sec() - start;

    printf("  JSON Token Scanner  : %.2f JSON payloads/sec (%.3f s for %zu updates)\n",
           (double)iterations / elapsed, elapsed, iterations);
    printf("  Total Fields Found  : %llu\n\n", (unsigned long long)total_matches);
}

int main(void)
{
    printf("====================================================\n");
    printf("   C2T PERFORMANCE & RESOURCE BENCHMARK SUITE       \n");
    printf("====================================================\n");
    benchmark_crypto();
    benchmark_logging_ring();
    benchmark_json_parsing();
    printf("  Peak Process RSS    : %ld KB\n", get_peak_rss_kb());
    printf("====================================================\n");
    return 0;
}
