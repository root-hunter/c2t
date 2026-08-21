#ifndef C2T_RUNTIME_H
#define C2T_RUNTIME_H

#include <stddef.h>

typedef enum {
    C2T_RUNTIME_STOPPED = 0,
    C2T_RUNTIME_STARTING,
    C2T_RUNTIME_RUNNING
} c2t_runtime_state_t;

typedef struct {
    c2t_runtime_state_t state;
    unsigned long process_id;
} c2t_runtime_status_t;

enum {
    C2T_BACKGROUND_ERROR = -1,
    C2T_BACKGROUND_PARENT = 0,
    C2T_BACKGROUND_CHILD = 1
};

int c2t_runtime_acquire(void);
void c2t_runtime_mark_running(void);
void c2t_runtime_release(void);
int c2t_runtime_stop_requested(void);

int c2t_runtime_get_status(c2t_runtime_status_t *status);
int c2t_runtime_stop(unsigned int timeout_ms, int force);
int c2t_runtime_start_background(int argc, char **argv,
                                 unsigned int timeout_ms);
const char *c2t_runtime_log_path(void);

#endif
