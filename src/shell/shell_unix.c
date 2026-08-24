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

#include "shell_unix.h"
#include "../logging/logging.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t get_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
  }
  return (uint64_t)time(nullptr) * 1000ULL;
}

int c2t_shell_unix_execute(const char *command, c2t_shell_result_t *result,
                           uint32_t timeout_ms) {
  if (!command || !result)
    return 0;

  memset(result, 0, sizeof(*result));
  if (timeout_ms == 0) {
    timeout_ms = C2T_SHELL_DEFAULT_TIMEOUT_MS;
  }

  int pipefd[2];
  if (pipe(pipefd) < 0) {
    c2t_log_error("shell", "Failed to create pipe: %s", strerror(errno));
    result->execution_error = 1;
    return 0;
  }

  uint64_t start_time = get_monotonic_ms();

  pid_t pid = fork();
  if (pid < 0) {
    c2t_log_error("shell", "Failed to fork child process: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    result->execution_error = 1;
    return 0;
  }

  if (pid == 0) {
    /* Child process */
    close(pipefd[0]);

    /* Redirect stdin from /dev/null */
    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
      (void)dup2(null_fd, STDIN_FILENO);
      if (null_fd > STDERR_FILENO)
        close(null_fd);
    }

    /* Merge stdout and stderr into pipe */
    (void)dup2(pipefd[1], STDOUT_FILENO);
    (void)dup2(pipefd[1], STDERR_FILENO);
    if (pipefd[1] > STDERR_FILENO)
      close(pipefd[1]);

    /* Create new process group so we can kill child tree cleanly */
    (void)setpgid(0, 0);

    /* Execute shell command */
    execl("/bin/sh", "sh", "-c", command, (char *)nullptr);
    _exit(127);
  }

  /* Parent process */
  close(pipefd[1]);

  /* Set read end to non-blocking */
  int flags = fcntl(pipefd[0], F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
  }

  size_t capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    c2t_log_error("shell", "Failed to allocate memory for shell output");
    close(pipefd[0]);
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
    if (remaining_ms > 100)
      remaining_ms = 100;

    struct pollfd pfd = {
        .fd = pipefd[0],
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
      char chunk[1024];
      ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
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
        /* EOF reached - child has finished writing and closed pipe */
        pipe_open = 0;
      } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          pipe_open = 0;
        }
      }
    }
  }

  close(pipefd[0]);

  if (timed_out) {
    result->timed_out = 1;
    result->exit_code = -1;
    c2t_log_warning("shell", "Command '%s' timed out after %u ms, killing pid %d",
                    command, timeout_ms, pid);
    (void)kill(-pid, SIGTERM);
    usleep(50000);
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == 0) {
      (void)kill(-pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
    }
  } else {
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    if (w == pid) {
      if (WIFEXITED(status)) {
        result->exit_code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result->exit_code = 128 + WTERMSIG(status);
      }
    }
  }

  result->duration_ms = get_monotonic_ms() - start_time;
  result->output = buffer;
  result->output_len = total_read;

  return 1;
}

#endif /* !_WIN32 */
