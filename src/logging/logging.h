#ifndef C2T_LOGGING_H
#define C2T_LOGGING_H

void c2t_log_init(void);
int c2t_log_is_verbose(void);

#if defined(__GNUC__) || defined(__clang__)
#define C2T_PRINTF_FORMAT(format_index, arguments_index) \
    __attribute__((format(printf, format_index, arguments_index)))
#else
#define C2T_PRINTF_FORMAT(format_index, arguments_index)
#endif

void c2t_log_error(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_warning(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_info(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);
void c2t_log_debug(const char *component, const char *format, ...)
    C2T_PRINTF_FORMAT(2, 3);

#endif
