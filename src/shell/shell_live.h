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

#ifndef C2T_SHELL_LIVE_H
#define C2T_SHELL_LIVE_H

#include <stddef.h>
#include <stdint.h>

[[nodiscard]] int c2t_shell_live_is_active(void);
[[nodiscard]] int c2t_shell_live_start(const char *shell_name);
[[nodiscard]] int c2t_shell_live_handle_input(const char *input_text);
[[nodiscard]] int c2t_shell_live_handle_callback(const char *callback_query_id,
                                                 const char *callback_data);
[[nodiscard]] int c2t_shell_live_stop(void);
void c2t_shell_live_reset(void);

#endif /* C2T_SHELL_LIVE_H */
