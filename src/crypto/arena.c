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
  if (!arena)
    return 0;

  arena->buffer = nullptr;
  arena->capacity = 0;
  arena->offset = 0;
  arena->is_locked = 0;
  if (capacity == 0)
    return 0;

  arena->buffer = malloc(capacity);
  if (!arena->buffer)
    return 0;
  arena->capacity = capacity;

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

  const size_t alignment = _Alignof(max_align_t);
  if (size > SIZE_MAX - (alignment - 1U))
    return nullptr;
  size_t aligned_size = (size + alignment - 1U) & ~(alignment - 1U);
  if (arena->offset > arena->capacity ||
      aligned_size > arena->capacity - arena->offset)
    return nullptr;

  void *ptr = arena->buffer + arena->offset;
  arena->offset += aligned_size;
  return ptr;
}

int c2t_arena_contains(const c2t_arena_t *arena, const void *pointer) {
  if (!arena || !arena->buffer || !pointer)
    return 0;

  uintptr_t start = (uintptr_t)arena->buffer;
  uintptr_t candidate = (uintptr_t)pointer;
  return candidate >= start && candidate - start < arena->capacity;
}

void c2t_arena_reset(c2t_arena_t *arena) {
  if (!arena || !arena->buffer)
    return;

  size_t used = arena->offset < arena->capacity ? arena->offset
                                                : arena->capacity;
  c2t_secure_zero(arena->buffer, used);
  arena->offset = 0;
}

void c2t_arena_destroy(c2t_arena_t *arena) {
  if (!arena)
    return;

  if (!arena->buffer) {
    arena->capacity = 0;
    arena->offset = 0;
    arena->is_locked = 0;
    return;
  }

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
