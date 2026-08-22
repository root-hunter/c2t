/*
 * Copyright (C) 2026 Antonio Ricciardi
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

#ifndef C2T_EMBEDDED_CONFIG_H
#define C2T_EMBEDDED_CONFIG_H

#include <stddef.h>

#define C2T_EMBEDDED_PAYLOAD_CAPACITY 4096U

/* Copies an embedded value to output and returns 1 when it is present. */
[[nodiscard]] int c2t_embedded_config_get(const char *name, char *output,
                                         size_t output_capacity);

#endif
