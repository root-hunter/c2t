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

#ifndef _WIN32

#define _GNU_SOURCE
#include "shell_unix.h"
#include "../logging/logging.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define C2T_SHELL_CHUNK_SIZE 8192U
#define C2T_SESSION_IDLE_TIMEOUT_MS (15U * 60U * 1000U)

static uint64_t get_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
  }
  return (uint64_t)time(nullptr) * 1000ULL;
}

static int create_cloexec_pipe(int pipefd[2]) {
#if defined(__linux__) && defined(O_CLOEXEC)
  if (pipe2(pipefd, O_CLOEXEC) == 0) {
    return 0;
  }
#endif
  if (pipe(pipefd) < 0) {
    return -1;
  }
  (void)fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
  (void)fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
  return 0;
}

static void sanitize_child_environment_and_fds(void) {
  /* Unblock all signals and reset signal handlers to defaults */
  sigset_t sset;
  sigemptyset(&sset);
  (void)sigprocmask(SIG_SETMASK, &sset, nullptr);

  signal(SIGPIPE, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGCHLD, SIG_DFL);
  signal(SIGHUP, SIG_DFL);

  /* Close all inherited file descriptors beyond standard streams */
  int max_fd = (int)sysconf(_SC_OPEN_MAX);
  if (max_fd < 32) max_fd = 1024;
  if (max_fd > 4096) max_fd = 4096;
  for (int fd = 3; fd < max_fd; fd++) {
    (void)close(fd);
  }
}

int c2t_shell_unix_execute(const char *command, c2t_shell_result_t *result,
                           uint32_t timeout_ms) {
  c2t_shell_options_t opts = {
      .command = command,
      .shell_type = C2T_SHELL_AUTO,
      .stdin_data = nullptr,
      .stdin_data_len = 0,
      .timeout_ms = timeout_ms,
      .working_dir = nullptr,
  };
  return c2t_shell_unix_execute_ex(&opts, result);
}

int c2t_shell_unix_execute_ex(const c2t_shell_options_t *options,
                              c2t_shell_result_t *result) {
  if (!options || !options->command || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  uint32_t timeout_ms = options->timeout_ms;
  if (timeout_ms == 0) {
    timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS;
  }

  int stdout_pipe[2];
  if (create_cloexec_pipe(stdout_pipe) < 0) {
    c2t_log_error("shell", "Failed to create stdout pipe: %s", strerror(errno));
    result->execution_error = 1;
    return 0;
  }

  int stdin_pipe[2] = {-1, -1};
  if (options->stdin_data && options->stdin_data_len > 0) {
    if (create_cloexec_pipe(stdin_pipe) < 0) {
      c2t_log_error("shell", "Failed to create stdin pipe: %s", strerror(errno));
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      result->execution_error = 1;
      return 0;
    }
  }

  uint64_t start_time = get_monotonic_ms();

  pid_t pid = fork();
  if (pid < 0) {
    c2t_log_error("shell", "Failed to fork child process: %s", strerror(errno));
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    if (stdin_pipe[0] >= 0) {
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
    }
    result->execution_error = 1;
    return 0;
  }

  if (pid == 0) {
    /* Child process */
    close(stdout_pipe[0]);

    /* Setup STDIN */
    if (stdin_pipe[0] >= 0) {
      close(stdin_pipe[1]);
      (void)dup2(stdin_pipe[0], STDIN_FILENO);
      if (stdin_pipe[0] > STDERR_FILENO)
        close(stdin_pipe[0]);
    } else {
      int null_fd = open("/dev/null", O_RDONLY);
      if (null_fd >= 0) {
        (void)dup2(null_fd, STDIN_FILENO);
        if (null_fd > STDERR_FILENO)
          close(null_fd);
      }
    }

    /* Merge stdout and stderr into pipe */
    (void)dup2(stdout_pipe[1], STDOUT_FILENO);
    (void)dup2(stdout_pipe[1], STDERR_FILENO);
    if (stdout_pipe[1] > STDERR_FILENO)
      close(stdout_pipe[1]);

    /* Create isolated process group */
    (void)setpgid(0, 0);

    /* Change directory if requested */
    if (options->working_dir && *options->working_dir) {
      (void)chdir(options->working_dir);
    }

    /* Sanitize descriptors and signals before exec */
    sanitize_child_environment_and_fds();

    /* Execute using chosen shell */
    switch (options->shell_type) {
    case C2T_SHELL_BASH:
      execl("/bin/bash", "bash", "-c", options->command, (char *)nullptr);
      execl("/usr/bin/bash", "bash", "-c", options->command, (char *)nullptr);
      execl("/bin/sh", "sh", "-c", options->command, (char *)nullptr);
      break;
    case C2T_SHELL_ZSH:
      execl("/bin/zsh", "zsh", "-c", options->command, (char *)nullptr);
      execl("/usr/bin/zsh", "zsh", "-c", options->command, (char *)nullptr);
      execl("/bin/sh", "sh", "-c", options->command, (char *)nullptr);
      break;
    case C2T_SHELL_PYTHON:
      execlp("python3", "python3", "-c", options->command, (char *)nullptr);
      execlp("python", "python", "-c", options->command, (char *)nullptr);
      break;
    case C2T_SHELL_POWERSHELL:
      execlp("pwsh", "pwsh", "-NonInteractive", "-Command", options->command,
             (char *)nullptr);
      execl("/bin/sh", "sh", "-c", options->command, (char *)nullptr);
      break;
    case C2T_SHELL_SH:
    case C2T_SHELL_CMD:
    case C2T_SHELL_AUTO:
    default:
      execl("/bin/sh", "sh", "-c", options->command, (char *)nullptr);
      break;
    }

    _exit(127);
  }

  /* Parent process */
  close(stdout_pipe[1]);

  /* If stdin input was provided, write it and close write end to signal EOF */
  if (stdin_pipe[0] >= 0) {
    close(stdin_pipe[0]);
    if (options->stdin_data && options->stdin_data_len > 0) {
      size_t written = 0;
      while (written < options->stdin_data_len) {
        ssize_t w = write(stdin_pipe[1], options->stdin_data + written,
                          options->stdin_data_len - written);
        if (w <= 0)
          break;
        written += (size_t)w;
      }
    }
    close(stdin_pipe[1]);
  }

  /* Set read end to non-blocking */
  int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);
  }

  size_t capacity = 8192;
  char *buffer = malloc(capacity);
  if (!buffer) {
    c2t_log_error("shell", "Failed to allocate memory for shell output");
    close(stdout_pipe[0]);
    (void)kill(-pid, SIGKILL);
    (void)waitpid(pid, nullptr, 0);
    result->execution_error = 1;
    return 0;
  }
  size_t total_read = 0;
  buffer[0] = '\0';

  int pipe_open = 1;
  int timed_out = 0;

  while (pipe_open) {
    uint64_t now = get_monotonic_ms();
    uint64_t elapsed = now - start_time;
    if (elapsed >= (uint64_t)timeout_ms) {
      timed_out = 1;
      break;
    }

    int remaining_ms = (int)((uint64_t)timeout_ms - elapsed);
    if (remaining_ms > 50)
      remaining_ms = 50;

    struct pollfd pfd = {
        .fd = stdout_pipe[0],
        .events = POLLIN | POLLHUP | POLLERR,
        .revents = 0,
    };

    int ret = poll(&pfd, 1, remaining_ms);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
      while (1) {
        char chunk[C2T_SHELL_CHUNK_SIZE];
        ssize_t n = read(stdout_pipe[0], chunk, sizeof(chunk));
        if (n > 0) {
          if (total_read + (size_t)n < C2T_SHELL_MAX_OUTPUT_BYTES) {
            if (total_read + (size_t)n + 1 > capacity) {
              size_t new_cap = capacity * 2;
              while (new_cap < total_read + (size_t)n + 1)
                new_cap *= 2;
              char *new_buf = realloc(buffer, new_cap);
              if (new_buf) {
                buffer = new_buf;
                capacity = new_cap;
              }
            }
            if (total_read + (size_t)n + 1 <= capacity) {
              memcpy(buffer + total_read, chunk, (size_t)n);
              total_read += (size_t)n;
              buffer[total_read] = '\0';
            }
          }
        } else if (n == 0) {
          pipe_open = 0;
          break;
        } else {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            pipe_open = 0;
          }
          break;
        }
      }
    }
  }

  close(stdout_pipe[0]);

  if (timed_out) {
    result->timed_out = 1;
    result->exit_code = -1;
    c2t_log_warning("shell",
                    "Command '%s' timed out after %u ms, killing process group %d",
                    options->command, timeout_ms, pid);
    (void)kill(-pid, SIGTERM);
    usleep(25000);
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == 0) {
      (void)kill(-pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
    }
    /* Reap any remaining orphaned children in the group */
    while (waitpid(-pid, &status, WNOHANG) > 0) {
    }
  } else {
    int status = 0;
    int reaped = 0;
    uint64_t w_deadline = get_monotonic_ms() + 1000;
    while (get_monotonic_ms() < w_deadline) {
      pid_t w = waitpid(pid, &status, WNOHANG);
      if (w == pid) {
        reaped = 1;
        if (WIFEXITED(status)) {
          result->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
          result->exit_code = 128 + WTERMSIG(status);
        }
        break;
      }
      usleep(10000);
    }
    if (!reaped) {
      (void)kill(-pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
      result->exit_code = 137;
    }
    while (waitpid(-pid, &status, WNOHANG) > 0) {
    }
  }

  result->duration_ms = get_monotonic_ms() - start_time;
  result->output = buffer;
  result->output_len = total_read;
  buffer[total_read] = '\0';

  return 1;
}

/* ========================================================================= */
/* Interactive Shell Session Implementation (Unix)                           */
/* ========================================================================= */

typedef struct {
  pthread_mutex_t mutex;
  int is_active;
  pid_t pid;
  int stdin_fd;
  int stdout_fd;
  uint64_t start_time_ms;
  uint64_t last_activity_ms;
  uint64_t total_input_bytes;
  uint64_t total_output_bytes;
  c2t_shell_type_t shell_type;
  char shell_name[32];
} unix_shell_session_t;

static unix_shell_session_t g_unix_session = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .is_active = 0,
    .pid = -1,
    .stdin_fd = -1,
    .stdout_fd = -1,
    .start_time_ms = 0,
    .last_activity_ms = 0,
    .total_input_bytes = 0,
    .total_output_bytes = 0,
    .shell_type = C2T_SHELL_AUTO,
    .shell_name = "sh",
};

static void check_session_idle_watchdog(void) {
  if (g_unix_session.is_active && g_unix_session.pid > 0) {
    uint64_t now = get_monotonic_ms();
    if (now > g_unix_session.last_activity_ms + C2T_SESSION_IDLE_TIMEOUT_MS) {
      c2t_log_info("shell", "Interactive session PID %d timed out due to 15m inactivity",
                   (int)g_unix_session.pid);
      (void)kill(-g_unix_session.pid, SIGTERM);
      usleep(25000);
      (void)kill(-g_unix_session.pid, SIGKILL);
      int status = 0;
      (void)waitpid(g_unix_session.pid, &status, WNOHANG);
      if (g_unix_session.stdin_fd >= 0) close(g_unix_session.stdin_fd);
      if (g_unix_session.stdout_fd >= 0) close(g_unix_session.stdout_fd);
      g_unix_session.is_active = 0;
      g_unix_session.pid = -1;
      g_unix_session.stdin_fd = -1;
      g_unix_session.stdout_fd = -1;
    }
  }
}

int c2t_shell_unix_session_start(c2t_shell_type_t shell_type, char *out_msg,
                                 size_t out_msg_cap) {
  pthread_mutex_lock(&g_unix_session.mutex);

  check_session_idle_watchdog();

  if (g_unix_session.is_active) {
    int status = 0;
    if (waitpid(g_unix_session.pid, &status, WNOHANG) == 0) {
      if (out_msg && out_msg_cap > 0) {
        snprintf(out_msg, out_msg_cap,
                 "ℹ️ <b>Interactive Shell Session Already Active</b>\n\n"
                 "• <b>PID:</b> <code>%d</code>\n"
                 "• <b>Shell:</b> <code>%s</code>\n"
                 "• <b>Active Time:</b> %llu s\n\n"
                 "💡 <i>Use <code>/sh_in &lt;input&gt;</code> to send commands or <code>/sh_stop</code> to terminate.</i>",
                 (int)g_unix_session.pid, g_unix_session.shell_name,
                 (unsigned long long)((get_monotonic_ms() - g_unix_session.start_time_ms) / 1000ULL));
      }
      pthread_mutex_unlock(&g_unix_session.mutex);
      return 1;
    } else {
      if (g_unix_session.stdin_fd >= 0) close(g_unix_session.stdin_fd);
      if (g_unix_session.stdout_fd >= 0) close(g_unix_session.stdout_fd);
      g_unix_session.is_active = 0;
      g_unix_session.pid = -1;
      g_unix_session.stdin_fd = -1;
      g_unix_session.stdout_fd = -1;
    }
  }

  int pipe_in[2];
  int pipe_out[2];
  if (create_cloexec_pipe(pipe_in) < 0 || create_cloexec_pipe(pipe_out) < 0) {
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "❌ <b>Failed to create session pipes:</b> %s",
               strerror(errno));
    }
    pthread_mutex_unlock(&g_unix_session.mutex);
    return 0;
  }

  const char *name = "sh";

  switch (shell_type) {
  case C2T_SHELL_BASH:
    name = "bash";
    break;
  case C2T_SHELL_ZSH:
    name = "zsh";
    break;
  case C2T_SHELL_PYTHON:
    name = "python3";
    break;
  case C2T_SHELL_POWERSHELL:
    name = "pwsh";
    break;
  case C2T_SHELL_SH:
  case C2T_SHELL_CMD:
  case C2T_SHELL_AUTO:
  default:
    name = "sh";
    break;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_in[0]); close(pipe_in[1]);
    close(pipe_out[0]); close(pipe_out[1]);
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "❌ <b>Failed to spawn session process:</b> %s",
               strerror(errno));
    }
    pthread_mutex_unlock(&g_unix_session.mutex);
    return 0;
  }

  if (pid == 0) {
    /* Child interactive shell process */
    close(pipe_in[1]);
    close(pipe_out[0]);

    (void)dup2(pipe_in[0], STDIN_FILENO);
    (void)dup2(pipe_out[1], STDOUT_FILENO);
    (void)dup2(pipe_out[1], STDERR_FILENO);

    if (pipe_in[0] > STDERR_FILENO) close(pipe_in[0]);
    if (pipe_out[1] > STDERR_FILENO) close(pipe_out[1]);

    (void)setpgid(0, 0);

    sanitize_child_environment_and_fds();

    if (shell_type == C2T_SHELL_POWERSHELL) {
      execlp("pwsh", "pwsh", "-NoLogo", (char *)nullptr);
      execl("/bin/sh", "sh", "-s", (char *)nullptr);
    } else if (shell_type == C2T_SHELL_PYTHON) {
      execlp("python3", "python3", "-u", "-i", "-q", (char *)nullptr);
      execlp("python", "python", "-u", "-i", "-q", (char *)nullptr);
      execl("/bin/sh", "sh", "-s", (char *)nullptr);
    } else if (shell_type == C2T_SHELL_BASH) {
      execl("/bin/bash", "bash", "--noprofile", "--norc", "-s", (char *)nullptr);
      execl("/usr/bin/bash", "bash", "--noprofile", "--norc", "-s", (char *)nullptr);
      execl("/bin/sh", "sh", "-s", (char *)nullptr);
    } else if (shell_type == C2T_SHELL_ZSH) {
      execl("/bin/zsh", "zsh", "-s", (char *)nullptr);
      execl("/usr/bin/zsh", "zsh", "-s", (char *)nullptr);
      execl("/bin/sh", "sh", "-s", (char *)nullptr);
    } else {
      execl("/bin/sh", "sh", "-s", (char *)nullptr);
    }
    _exit(127);
  }

  /* Parent process */
  close(pipe_in[0]);
  close(pipe_out[1]);

  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);
  }

  g_unix_session.is_active = 1;
  g_unix_session.pid = pid;
  g_unix_session.stdin_fd = pipe_in[1];
  g_unix_session.stdout_fd = pipe_out[0];
  g_unix_session.start_time_ms = get_monotonic_ms();
  g_unix_session.last_activity_ms = g_unix_session.start_time_ms;
  g_unix_session.total_input_bytes = 0;
  g_unix_session.total_output_bytes = 0;
  g_unix_session.shell_type = shell_type;
  strncpy(g_unix_session.shell_name, name, sizeof(g_unix_session.shell_name) - 1);
  g_unix_session.shell_name[sizeof(g_unix_session.shell_name) - 1] = '\0';

  /* Short initial banner drain (100ms) */
  usleep(100000);
  char init_banner[2048] = {};
  ssize_t nb = read(g_unix_session.stdout_fd, init_banner, sizeof(init_banner) - 1);
  if (nb > 0) {
    init_banner[nb] = '\0';
    g_unix_session.total_output_bytes += (size_t)nb;
  }

  if (out_msg && out_msg_cap > 0) {
    if (nb > 0 && strlen(init_banner) > 0) {
      snprintf(out_msg, out_msg_cap,
               "🟢 <b>Interactive Shell Session Started</b>\n\n"
               "• <b>PID:</b> <code>%d</code>\n"
               "• <b>Shell:</b> <code>%s</code>\n\n"
               "<pre><code class=\"language-shell\">%s</code></pre>\n"
               "💡 <i>Send input using <code>/sh_in &lt;input&gt;</code> or stop with <code>/sh_stop</code>.</i>",
               (int)pid, name, init_banner);
    } else {
      snprintf(out_msg, out_msg_cap,
               "🟢 <b>Interactive Shell Session Started</b>\n\n"
               "• <b>PID:</b> <code>%d</code>\n"
               "• <b>Shell:</b> <code>%s</code>\n\n"
               "💡 <i>Send input using <code>/sh_in &lt;input&gt;</code> or stop with <code>/sh_stop</code>.</i>",
               (int)pid, name);
    }
  }

  pthread_mutex_unlock(&g_unix_session.mutex);
  return 1;
}

int c2t_shell_unix_session_write(const char *input, size_t input_len,
                                 c2t_shell_result_t *result, uint32_t wait_ms) {
  if (!input || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  if (wait_ms == 0)
    wait_ms = 1000;

  pthread_mutex_lock(&g_unix_session.mutex);

  check_session_idle_watchdog();

  if (!g_unix_session.is_active || g_unix_session.pid < 0) {
    pthread_mutex_unlock(&g_unix_session.mutex);
    result->execution_error = 1;
    return 0;
  }

  /* Check if child process is still alive */
  int status = 0;
  if (waitpid(g_unix_session.pid, &status, WNOHANG) != 0) {
    if (g_unix_session.stdin_fd >= 0) close(g_unix_session.stdin_fd);
    if (g_unix_session.stdout_fd >= 0) close(g_unix_session.stdout_fd);
    g_unix_session.is_active = 0;
    pthread_mutex_unlock(&g_unix_session.mutex);
    result->execution_error = 1;
    return 0;
  }

  uint64_t start_time = get_monotonic_ms();
  signal(SIGPIPE, SIG_IGN);

  /* Write input to session stdin */
  size_t written = 0;
  if (input_len > 0) {
    while (written < input_len) {
      ssize_t w = write(g_unix_session.stdin_fd, input + written, input_len - written);
      if (w <= 0)
        break;
      written += (size_t)w;
    }
    if (input[input_len - 1] != '\n') {
      (void)write(g_unix_session.stdin_fd, "\n", 1);
      written++;
    }
    g_unix_session.total_input_bytes += written;
  }
  g_unix_session.last_activity_ms = start_time;

  size_t capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    pthread_mutex_unlock(&g_unix_session.mutex);
    result->execution_error = 1;
    return 0;
  }
  size_t total_read = 0;
  buffer[0] = '\0';

  uint64_t deadline = start_time + (uint64_t)wait_ms;
  uint64_t last_data_ms = 0;
  const uint64_t quiet_threshold_ms = 120;

  while (1) {
    uint64_t now = get_monotonic_ms();
    if (now >= deadline) {
      break;
    }

    int rem = (int)(deadline - now);
    if (rem > 30) rem = 30;
    if (rem <= 0) break;

    struct pollfd pfd = {
        .fd = g_unix_session.stdout_fd,
        .events = POLLIN | POLLHUP | POLLERR,
        .revents = 0,
    };

    int ret = poll(&pfd, 1, rem);
    if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
      int got_data = 0;
      while (1) {
        char chunk[4096];
        ssize_t n = read(g_unix_session.stdout_fd, chunk, sizeof(chunk));
        if (n > 0) {
          got_data = 1;
          if (total_read + (size_t)n < C2T_SHELL_MAX_OUTPUT_BYTES) {
            if (total_read + (size_t)n + 1 > capacity) {
              size_t new_cap = capacity * 2;
              while (new_cap < total_read + (size_t)n + 1)
                new_cap *= 2;
              char *new_buf = realloc(buffer, new_cap);
              if (new_buf) {
                buffer = new_buf;
                capacity = new_cap;
              }
            }
            if (total_read + (size_t)n + 1 <= capacity) {
              memcpy(buffer + total_read, chunk, (size_t)n);
              total_read += (size_t)n;
              buffer[total_read] = '\0';
            }
          }
        } else {
          break;
        }
      }
      if (got_data) {
        last_data_ms = get_monotonic_ms();
        usleep(15000);
        continue;
      }
    }

    if (total_read > 0 && last_data_ms > 0) {
      if (now - last_data_ms >= quiet_threshold_ms) {
        break;
      }
    }
  }

  g_unix_session.total_output_bytes += total_read;
  result->duration_ms = get_monotonic_ms() - start_time;
  result->output = buffer;
  result->output_len = total_read;
  buffer[total_read] = '\0';

  pthread_mutex_unlock(&g_unix_session.mutex);
  return 1;
}

int c2t_shell_unix_session_stop(char *out_msg, size_t out_msg_cap) {
  pthread_mutex_lock(&g_unix_session.mutex);

  if (!g_unix_session.is_active) {
    if (out_msg && out_msg_cap > 0) {
      snprintf(out_msg, out_msg_cap, "⚪ <i>No interactive shell session is currently active.</i>");
    }
    pthread_mutex_unlock(&g_unix_session.mutex);
    return 1;
  }

  pid_t pid = g_unix_session.pid;
  uint64_t dur = (get_monotonic_ms() - g_unix_session.start_time_ms) / 1000ULL;
  uint64_t in_b = g_unix_session.total_input_bytes;
  uint64_t out_b = g_unix_session.total_output_bytes;

  (void)write(g_unix_session.stdin_fd, "exit\n", 5);
  usleep(25000);

  int status = 0;
  if (waitpid(pid, &status, WNOHANG) == 0) {
    (void)kill(-pid, SIGTERM);
    usleep(25000);
    if (waitpid(pid, &status, WNOHANG) == 0) {
      (void)kill(-pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
    }
  }
  while (waitpid(-pid, &status, WNOHANG) > 0) {
  }

  if (g_unix_session.stdin_fd >= 0) close(g_unix_session.stdin_fd);
  if (g_unix_session.stdout_fd >= 0) close(g_unix_session.stdout_fd);

  g_unix_session.is_active = 0;
  g_unix_session.pid = -1;
  g_unix_session.stdin_fd = -1;
  g_unix_session.stdout_fd = -1;

  if (out_msg && out_msg_cap > 0) {
    snprintf(out_msg, out_msg_cap,
             "🛑 <b>Interactive Shell Session Closed</b>\n\n"
             "• <b>PID:</b> <code>%d</code>\n"
             "• <b>Duration:</b> %llu s\n"
             "• <b>Total I/O:</b> %llu bytes in / %llu bytes out\n\n"
             "✅ <i>Session resources freed cleanly.</i>",
             (int)pid, (unsigned long long)dur, (unsigned long long)in_b,
             (unsigned long long)out_b);
  }

  pthread_mutex_unlock(&g_unix_session.mutex);
  return 1;
}

int c2t_shell_unix_session_get_info(c2t_shell_session_info_t *info) {
  if (!info)
    return 0;

  pthread_mutex_lock(&g_unix_session.mutex);

  check_session_idle_watchdog();

  if (g_unix_session.is_active) {
    int status = 0;
    if (waitpid(g_unix_session.pid, &status, WNOHANG) != 0) {
      if (g_unix_session.stdin_fd >= 0) close(g_unix_session.stdin_fd);
      if (g_unix_session.stdout_fd >= 0) close(g_unix_session.stdout_fd);
      g_unix_session.is_active = 0;
    }
  }

  info->is_active = g_unix_session.is_active;
  info->pid = (uint64_t)g_unix_session.pid;
  info->start_time_ms = g_unix_session.start_time_ms;
  info->last_activity_ms = g_unix_session.last_activity_ms;
  info->total_input_bytes = g_unix_session.total_input_bytes;
  info->total_output_bytes = g_unix_session.total_output_bytes;
  info->shell_type = g_unix_session.shell_type;
  strncpy(info->shell_name, g_unix_session.shell_name, sizeof(info->shell_name) - 1);
  info->shell_name[sizeof(info->shell_name) - 1] = '\0';

  pthread_mutex_unlock(&g_unix_session.mutex);
  return 1;
}

#endif /* !_WIN32 */
