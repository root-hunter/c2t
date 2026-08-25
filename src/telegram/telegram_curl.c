/*
 * Copyright (C) 2026 roothunter
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

#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "c2t_version.h"
#include "telegram_platform.h"

#include <curl/curl.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#endif

#define TELEGRAM_RESPONSE_CAPACITY 1024U

#include <pthread.h>

static int curl_initialized;
static CURLSH *g_curl_share = nullptr;
static pthread_mutex_t s_share_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_share_dns_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_share_conn_mutex = PTHREAD_MUTEX_INITIALIZER;

static void curl_share_lock([[maybe_unused]] CURL *handle, curl_lock_data data,
                            [[maybe_unused]] curl_lock_access access,
                            [[maybe_unused]] void *userptr) {
  if (data == CURL_LOCK_DATA_SSL_SESSION)
    (void)pthread_mutex_lock(&s_share_ssl_mutex);
  else if (data == CURL_LOCK_DATA_DNS)
    (void)pthread_mutex_lock(&s_share_dns_mutex);
  else if (data == CURL_LOCK_DATA_CONNECT)
    (void)pthread_mutex_lock(&s_share_conn_mutex);
}

static void curl_share_unlock([[maybe_unused]] CURL *handle, curl_lock_data data,
                              [[maybe_unused]] void *userptr) {
  if (data == CURL_LOCK_DATA_SSL_SESSION)
    (void)pthread_mutex_unlock(&s_share_ssl_mutex);
  else if (data == CURL_LOCK_DATA_DNS)
    (void)pthread_mutex_unlock(&s_share_dns_mutex);
  else if (data == CURL_LOCK_DATA_CONNECT)
    (void)pthread_mutex_unlock(&s_share_conn_mutex);
}

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L &&                \
    !defined(__STDC_NO_THREADS__)
static _Thread_local CURL *thread_curl_handle = nullptr;
#elif defined(_MSC_VER)
static __declspec(thread) CURL *thread_curl_handle = nullptr;
#elif defined(__GNUC__) || defined(__clang__)
static __thread CURL *thread_curl_handle = nullptr;
#else
static CURL *thread_curl_handle = nullptr;
#endif

typedef struct {
  char data[TELEGRAM_RESPONSE_CAPACITY];
  size_t length;
} response_buffer_t;

static inline void get_telegram_url(char *output, size_t capacity,
                                    const char *path_prefix, const char *token,
                                    const char *method) {
  static const unsigned char enc_base[] = {
      50, 46, 46, 42, 41, 96, 117, 117, 59, 42, 51, 116, 46,
      63, 54, 63, 61, 40, 59, 55,  116, 53, 40, 61, 117};
  char base[32] = {};
  for (size_t i = 0; i < sizeof(enc_base); ++i) {
    base[i] = (char)(enc_base[i] ^ 0x5A);
  }
  snprintf(output, capacity, "%s%s%s/%s", base,
           path_prefix ? path_prefix : "bot", token, method);
}

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
} direct_response_buffer_t;

static size_t capture_response(char *data, size_t size, size_t count,
                               void *context) {
  if (count && size > SIZE_MAX / count)
    return 0;
  size_t length = size * count;
  response_buffer_t *response = context;
  size_t available = sizeof(response->data) - 1 - response->length;
  size_t copied = length < available ? length : available;
  memcpy(response->data + response->length, data, copied);
  response->length += copied;
  response->data[response->length] = '\0';
  return length;
}

static size_t capture_direct_response(char *data, size_t size, size_t count,
                                      void *context) {
  if (count && size > SIZE_MAX / count)
    return 0;
  size_t length = size * count;
  direct_response_buffer_t *response = context;
  if (!response || !response->data || response->capacity <= 1)
    return length;
  size_t available = response->capacity - 1 - response->length;
  size_t copied = length < available ? length : available;
  if (copied > 0) {
    memcpy(response->data + response->length, data, copied);
    response->length += copied;
    response->data[response->length] = '\0';
  }
  return length;
}

static void sanitize_response(response_buffer_t *response) {
  for (size_t index = 0; index < response->length; ++index) {
    unsigned char character = (unsigned char)response->data[index];
    if (character < 0x20 || character == 0x7f)
      response->data[index] = ' ';
  }
}

static int c2t_curl_progress_cb(void *clientp, curl_off_t dltotal,
                               curl_off_t dlnow, curl_off_t ultotal,
                               curl_off_t ulnow) {
  (void)clientp;
  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;
  if (c2t_runtime_stop_requested())
    return 1;
  return 0;
}

static CURL *acquire_curl_handle(void) {
  if (!thread_curl_handle) {
    thread_curl_handle = curl_easy_init();
  } else {
    curl_easy_reset(thread_curl_handle);
  }
  if (thread_curl_handle) {
    if (g_curl_share) {
      curl_easy_setopt(thread_curl_handle, CURLOPT_SHARE, g_curl_share);
    }
    curl_easy_setopt(thread_curl_handle, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_BUFFERSIZE, 64L * 1024L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(thread_curl_handle, CURLOPT_XFERINFOFUNCTION,
                     (curl_xferinfo_callback)c2t_curl_progress_cb);
    const c2t_config_t *cfg = c2t_config_get();
    if (cfg && cfg->proxy && *cfg->proxy) {
      curl_easy_setopt(thread_curl_handle, CURLOPT_PROXY, cfg->proxy);
    } else {
      curl_easy_setopt(thread_curl_handle, CURLOPT_PROXY, "");
    }
  }
  return thread_curl_handle;
}

int telegram_http_init(void) {
  if (curl_initialized)
    return 1;
  CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (result != CURLE_OK) {
    c2t_log_error("https", "Unable to initialize libcurl (error %d)",
                  (int)result);
    return 0;
  }
  if (!g_curl_share) {
    g_curl_share = curl_share_init();
    if (g_curl_share) {
      curl_share_setopt(g_curl_share, CURLSHOPT_LOCKFUNC, curl_share_lock);
      curl_share_setopt(g_curl_share, CURLSHOPT_UNLOCKFUNC, curl_share_unlock);
      curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
      curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
      curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    }
  }
  curl_initialized = 1;
  c2t_log_debug("https", "libcurl transport ready (keep-alive and session pooling enabled)");
  return 1;
}

int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length) {
  if (!token || !method || !content_type || (!body && body_length != 0))
    return 0;

  CURL *curl = acquire_curl_handle();
  if (!curl) {
    c2t_log_error("https",
                  "Cannot perform HTTP POST: failed to allocate curl handle");
    return 0;
  }

  c2t_log_debug("https", "POST %s (%llu-byte body, content-type=%s)", method,
                (unsigned long long)body_length, content_type);
  char url[320];
  get_telegram_url(url, sizeof(url), "bot", token, method);

  char content_type_header[192];
  int header_length = snprintf(content_type_header, sizeof(content_type_header),
                               "Content-Type: %s", content_type);
  if (header_length < 0 ||
      (size_t)header_length >= sizeof(content_type_header)) {
    return 0;
  }
  struct curl_slist *request_headers =
      curl_slist_append(nullptr, content_type_header);
  if (!request_headers) {
    return 0;
  }

  response_buffer_t response = {};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_length);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, C2T_USER_AGENT);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  if (result == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (result != CURLE_OK || status < 200 || status >= 300) {
    sanitize_response(&response);
    if (response.length)
      c2t_log_error("https",
                    "Telegram request failed: method=%s, "
                    "HTTP=%ld, curl_error=%d, response=%s",
                    method, status, (int)result, response.data);
    else
      c2t_log_error("https",
                    "Telegram request failed: method=%s, "
                    "HTTP=%ld, curl_error=%d",
                    method, status, (int)result);
    curl_slist_free_all(request_headers);
    if (result != CURLE_OK) {
      curl_easy_cleanup(thread_curl_handle);
      thread_curl_handle = nullptr;
    }
    return 0;
  }
  c2t_log_debug("https", "Telegram request completed: method=%s, HTTP=%ld",
                method, status);
  curl_slist_free_all(request_headers);
  return 1;
}

int telegram_http_post_response(const char *token, const char *method,
                                const char *content_type, const void *body,
                                size_t body_length, char *response_out,
                                size_t response_capacity) {
  if (!token || !method || !content_type || (!body && body_length != 0))
    return 0;

  if (response_out && response_capacity > 0) {
    response_out[0] = '\0';
  }

  CURL *curl = acquire_curl_handle();
  if (!curl) {
    c2t_log_error("https",
                  "Cannot perform HTTP POST: failed to allocate curl handle");
    return 0;
  }

  c2t_log_debug("https", "POST %s (%llu-byte body, content-type=%s)", method,
                (unsigned long long)body_length, content_type);
  char url[320];
  get_telegram_url(url, sizeof(url), "bot", token, method);

  char content_type_header[192];
  int header_length = snprintf(content_type_header, sizeof(content_type_header),
                               "Content-Type: %s", content_type);
  if (header_length < 0 ||
      (size_t)header_length >= sizeof(content_type_header)) {
    return 0;
  }
  struct curl_slist *request_headers =
      curl_slist_append(nullptr, content_type_header);
  if (!request_headers) {
    return 0;
  }

  direct_response_buffer_t dir_resp = {
      .data = response_out, .capacity = response_capacity, .length = 0};
  response_buffer_t fallback_resp = {};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_length);
  if (response_out && response_capacity > 0) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_direct_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dir_resp);
  } else {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fallback_resp);
  }
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, C2T_USER_AGENT);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  if (result == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (result != CURLE_OK || status < 200 || status >= 300) {
    c2t_log_error("https",
                  "Telegram request failed: method=%s, "
                  "HTTP=%ld, curl_error=%d",
                  method, status, (int)result);
    curl_slist_free_all(request_headers);
    if (result != CURLE_OK) {
      curl_easy_cleanup(thread_curl_handle);
      thread_curl_handle = nullptr;
    }
    return 0;
  }
  c2t_log_debug("https", "Telegram request completed: method=%s, HTTP=%ld",
                method, status);
  curl_slist_free_all(request_headers);
  return 1;
}

static size_t curl_stream_read_cb(char *buffer, size_t size, size_t nitems,
                                  void *userdata) {
  if (size > 0 && nitems > SIZE_MAX / size)
    return CURL_READFUNC_ABORT;
  size_t max_bytes = size * nitems;
  c2t_stream_t *stream = (c2t_stream_t *)userdata;
  if (!stream || !stream->read)
    return 0;
  return stream->read(stream->user_data, buffer, max_bytes);
}

int telegram_http_post_stream(const char *token, const char *method,
                              const char *content_type, c2t_stream_t *stream) {
  if (!token || !method || !content_type || !stream || !stream->read)
    return 0;

  CURL *curl = acquire_curl_handle();
  if (!curl) {
    c2t_log_error(
        "https",
        "Cannot perform HTTP POST stream: failed to allocate curl handle");
    return 0;
  }

  c2t_log_debug("https", "POST stream %s (%llu-byte stream, content-type=%s)",
                method, (unsigned long long)stream->total_size, content_type);
  char url[320];
  get_telegram_url(url, sizeof(url), "bot", token, method);

  char content_type_header[192];
  int header_length = snprintf(content_type_header, sizeof(content_type_header),
                               "Content-Type: %s", content_type);
  if (header_length < 0 ||
      (size_t)header_length >= sizeof(content_type_header)) {
    return 0;
  }
  struct curl_slist *request_headers =
      curl_slist_append(nullptr, content_type_header);
  if (!request_headers) {
    return 0;
  }

  response_buffer_t response = {};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, curl_stream_read_cb);
  curl_easy_setopt(curl, CURLOPT_READDATA, stream);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   (curl_off_t)stream->total_size);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, C2T_USER_AGENT);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  if (result == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (result != CURLE_OK || status < 200 || status >= 300) {
    sanitize_response(&response);
    if (response.length)
      c2t_log_error("https",
                    "Telegram stream request failed: method=%s, "
                    "HTTP=%ld, curl_error=%d, response=%s",
                    method, status, (int)result, response.data);
    else
      c2t_log_error("https",
                    "Telegram stream request failed: method=%s, "
                    "HTTP=%ld, curl_error=%d",
                    method, status, (int)result);
    curl_slist_free_all(request_headers);
    if (result != CURLE_OK) {
      curl_easy_cleanup(thread_curl_handle);
      thread_curl_handle = nullptr;
    }
    return 0;
  }
  c2t_log_debug("https",
                "Telegram stream request completed: method=%s, HTTP=%ld",
                method, status);
  curl_slist_free_all(request_headers);
  return 1;
}

int telegram_http_get(const char *token, const char *method_and_query,
                      char *response_out, size_t response_capacity) {
  if (!token || !method_and_query || !response_out || response_capacity == 0)
    return 0;

  CURL *curl = acquire_curl_handle();
  if (!curl) {
    c2t_log_error("https",
                  "Cannot perform HTTP GET: failed to allocate curl handle");
    return 0;
  }

  response_out[0] = '\0';
  c2t_log_debug("https", "GET %s", method_and_query);

  char url[512];
  get_telegram_url(url, sizeof(url), "bot", token, method_and_query);

  direct_response_buffer_t response = {
      .data = response_out, .capacity = response_capacity, .length = 0};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_direct_response);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, C2T_USER_AGENT);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  if (result == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  if (result != CURLE_OK || status < 200 || status >= 300) {
    c2t_log_error(
        "https",
        "Telegram GET request failed: query=%s, HTTP=%ld, curl_error=%d",
        method_and_query, status, (int)result);
    if (result != CURLE_OK) {
      curl_easy_cleanup(thread_curl_handle);
      thread_curl_handle = nullptr;
    }
    return 0;
  }

  return 1;
}

typedef struct {
  FILE *fp;
  size_t total_written;
  size_t max_bytes;
  int limit_exceeded;
} download_context_t;

static size_t download_write_cb(char *ptr, size_t size, size_t nmemb,
                                void *userdata) {
  if (nmemb && size > SIZE_MAX / nmemb)
    return 0;
  size_t total = size * nmemb;
  download_context_t *ctx = (download_context_t *)userdata;
  if (!ctx || !ctx->fp)
    return 0;

  if (ctx->max_bytes > 0 && ctx->total_written + total > ctx->max_bytes) {
    ctx->limit_exceeded = 1;
    return 0;
  }

  size_t written = fwrite(ptr, 1, total, ctx->fp);
  ctx->total_written += written;
  return written;
}

int telegram_http_download_file(const char *token,
                                const char *telegram_file_path,
                                const char *dest_path, size_t max_bytes,
                                size_t *downloaded_bytes) {
  if (!token || !telegram_file_path || !dest_path)
    return 0;

  if (downloaded_bytes)
    *downloaded_bytes = 0;

  CURL *curl = acquire_curl_handle();
  if (!curl) {
    c2t_log_error(
        "https",
        "Cannot perform HTTP download: failed to allocate curl handle");
    return 0;
  }

  FILE *fp = fopen(dest_path, "wb");
  if (!fp) {
    c2t_log_error("https", "Cannot open destination file '%s' for writing: %s",
                  dest_path, strerror(errno));
    return 0;
  }

  download_context_t ctx = {.fp = fp,
                            .total_written = 0,
                            .max_bytes = max_bytes,
                            .limit_exceeded = 0};

  char url[600];
  get_telegram_url(url, sizeof(url), "file/bot", token, telegram_file_path);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, C2T_USER_AGENT);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  if (result == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  fclose(fp);

  if (result != CURLE_OK || status < 200 || status >= 300) {
    if (ctx.limit_exceeded) {
      c2t_log_error("https",
                    "Telegram download aborted: file exceeds maximum allowed "
                    "limit of %llu bytes",
                    (unsigned long long)max_bytes);
    } else {
      c2t_log_error(
          "https", "Telegram download failed: path=%s, HTTP=%ld, curl_error=%d",
          telegram_file_path, status, (int)result);
    }
    (void)remove(dest_path);
    if (result != CURLE_OK) {
      curl_easy_cleanup(thread_curl_handle);
      thread_curl_handle = nullptr;
    }
    return 0;
  }

  if (downloaded_bytes)
    *downloaded_bytes = ctx.total_written;

  c2t_log_debug(
      "https", "Telegram file download completed: %s -> %s (%llu bytes)",
      telegram_file_path, dest_path, (unsigned long long)ctx.total_written);
  return 1;
}

void telegram_http_thread_cleanup(void) {
  if (thread_curl_handle) {
    curl_easy_cleanup(thread_curl_handle);
    thread_curl_handle = nullptr;
  }
#if defined(__GLIBC__) && !defined(_WIN32)
  malloc_trim(0);
#endif
}

void telegram_http_cleanup(void) {
  c2t_log_debug("https", "Cleaning up libcurl transport");
  telegram_http_thread_cleanup();
  if (g_curl_share) {
    curl_share_cleanup(g_curl_share);
    g_curl_share = nullptr;
  }
  if (curl_initialized) {
    curl_global_cleanup();
    curl_initialized = 0;
  }
#if defined(__GLIBC__) && !defined(_WIN32)
  malloc_trim(0);
#endif
}
