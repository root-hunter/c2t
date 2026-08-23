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

#ifndef C2T_ARENA_H
#define C2T_ARENA_H

#include <stddef.h>

typedef struct {
  unsigned char *buffer;
  size_t capacity;
  size_t offset;
  int is_locked;
} c2t_arena_t;

[[nodiscard]] int c2t_arena_init(c2t_arena_t *arena, size_t capacity);
void *c2t_arena_alloc(c2t_arena_t *arena, size_t size);
[[nodiscard]] int c2t_arena_contains(const c2t_arena_t *arena,
                                     const void *pointer);
void c2t_arena_reset(c2t_arena_t *arena);
void c2t_arena_destroy(c2t_arena_t *arena);

#endif
