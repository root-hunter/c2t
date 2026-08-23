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

#ifndef C2T_LOG_SENDER_H
#define C2T_LOG_SENDER_H

#include <stdint.h>

[[nodiscard]] int c2t_log_sender_init(void);
int c2t_log_sender_dispatch_now(void);
[[nodiscard]] uint64_t c2t_log_sender_get_total_bytes(void);
[[nodiscard]] uint64_t c2t_log_sender_get_total_dispatches(void);
void c2t_log_sender_cleanup(void);

#endif
