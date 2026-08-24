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

#ifndef C2T_SHELL_WINDOWS_H
#define C2T_SHELL_WINDOWS_H

#ifdef _WIN32

#include "shell.h"
#include <stdint.h>

[[nodiscard]] int c2t_shell_windows_execute(const char *command,
                                            c2t_shell_result_t *result,
                                            uint32_t timeout_ms);

#endif /* _WIN32 */

#endif /* C2T_SHELL_WINDOWS_H */
