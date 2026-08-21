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

#ifndef C2T_CLIPBOARD_SOURCE_H
#define C2T_CLIPBOARD_SOURCE_H

#include <stdint.h>

/* Fixed-size, owned metadata keeps clipboard delivery synchronous and avoids
 * platform-specific allocations crossing module boundaries. */
#define C2T_SOURCE_APPLICATION_CAPACITY 256
#define C2T_SOURCE_TITLE_CAPACITY 768

typedef struct {
    char application[C2T_SOURCE_APPLICATION_CAPACITY];
    char title[C2T_SOURCE_TITLE_CAPACITY];
    uint32_t process_id;
} c2t_clipboard_source_t;

#endif
