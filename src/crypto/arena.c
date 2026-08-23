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

#include "arena.h"
#include "crypto.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

int c2t_arena_init(c2t_arena_t *arena, size_t capacity) {
  if (!arena || capacity == 0)
    return 0;

  arena->capacity = capacity;
  arena->offset = 0;
  arena->buffer = malloc(capacity);
  if (!arena->buffer) {
    arena->capacity = 0;
    return 0;
  }

  c2t_secure_lock(arena->buffer, capacity);
  arena->is_locked = 1;

#if defined(__linux__) && defined(MADV_DONTDUMP)
  (void)madvise(arena->buffer, capacity, MADV_DONTDUMP);
#endif
#if defined(__linux__) && defined(MADV_WIPEONFORK)
  (void)madvise(arena->buffer, capacity, MADV_WIPEONFORK);
#endif

  c2t_secure_zero(arena->buffer, capacity);
  return 1;
}

void *c2t_arena_alloc(c2t_arena_t *arena, size_t size) {
  if (!arena || !arena->buffer || size == 0)
    return nullptr;

  size_t aligned_size = (size + 15U) & ~15U;
  if (arena->offset + aligned_size > arena->capacity)
    return nullptr;

  void *ptr = arena->buffer + arena->offset;
  arena->offset += aligned_size;
  return ptr;
}

void c2t_arena_reset(c2t_arena_t *arena) {
  if (!arena || !arena->buffer)
    return;

  if (arena->offset > 0) {
    c2t_secure_zero(arena->buffer, arena->offset);
    arena->offset = 0;
  }
}

void c2t_arena_destroy(c2t_arena_t *arena) {
  if (!arena || !arena->buffer)
    return;

  c2t_secure_zero(arena->buffer, arena->capacity);
  if (arena->is_locked) {
    c2t_secure_unlock(arena->buffer, arena->capacity);
    arena->is_locked = 0;
  }
  free(arena->buffer);
  arena->buffer = nullptr;
  arena->capacity = 0;
  arena->offset = 0;
}
