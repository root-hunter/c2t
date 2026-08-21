#ifndef C2T_TELEGRAM_PLATFORM_H
#define C2T_TELEGRAM_PLATFORM_H

#include <stddef.h>

int telegram_http_init(void);
int telegram_http_post(const char *token, const char *method,
                       const char *content_type, const void *body,
                       size_t body_length);
void telegram_http_cleanup(void);

#endif
