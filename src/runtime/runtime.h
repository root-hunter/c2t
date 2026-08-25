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
  unsigned long supervisor_pid;
  unsigned long last_heartbeat;
} c2t_runtime_status_t;

enum {
  C2T_BACKGROUND_ERROR = -1,
  C2T_BACKGROUND_PARENT = 0,
  C2T_BACKGROUND_CHILD = 1
};

[[nodiscard]] int c2t_runtime_acquire(void);
void c2t_runtime_mark_running(void);
void c2t_runtime_heartbeat(void);
void c2t_runtime_release(void);
[[nodiscard]] int c2t_runtime_stop_requested(void);
[[nodiscard]] int c2t_runtime_stop_descriptor(void);
void c2t_runtime_request_stop(void);

[[nodiscard]] int c2t_runtime_get_status(c2t_runtime_status_t *status);
[[nodiscard]] int c2t_runtime_stop(unsigned int timeout_ms, int force);
[[nodiscard]] int c2t_runtime_start_background(int argc, char **argv,
                                               unsigned int timeout_ms);
[[nodiscard]] int c2t_runtime_run_supervisor(int argc, char **argv);
void c2t_runtime_hide_console(void);
void c2t_runtime_set_process_name(const char *name, int argc, char **argv);
[[nodiscard]] const char *c2t_runtime_log_path(void);
void c2t_runtime_save_args(int argc, char **argv);
[[nodiscard]] int c2t_runtime_trigger_restart(void);
void c2t_runtime_start_worker_watchdog(int argc, char **argv);
void c2t_runtime_stop_worker_watchdog(void);
[[nodiscard]] int c2t_runtime_is_c2t_process(unsigned long pid);

[[nodiscard]] int c2t_runtime_is_elevated(void);
[[nodiscard]] const char *c2t_runtime_get_privilege_str(void);
void c2t_runtime_get_username(char *out, size_t cap);
[[nodiscard]] int c2t_runtime_request_elevation(char *out_msg, size_t out_msg_cap);

#endif
