/*
 * Copyright (C) 2026 roothunter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "environment.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "../win32/win32_api.h"

#define C2T_ENVIRONMENT_SLOT_COUNT 8U

typedef struct {
  char *value;
  size_t capacity;
} c2t_environment_slot_t;

#if defined(_MSC_VER)
#define C2T_THREAD_LOCAL __declspec(thread)
#else
#define C2T_THREAD_LOCAL _Thread_local
#endif

static C2T_THREAD_LOCAL c2t_environment_slot_t
    environment_slots[C2T_ENVIRONMENT_SLOT_COUNT];
static C2T_THREAD_LOCAL size_t next_environment_slot;

static char *environment_slot_reserve(c2t_environment_slot_t *slot,
                                      size_t capacity) {
  if (slot->capacity >= capacity)
    return slot->value;
  char *resized = realloc(slot->value, capacity);
  if (!resized)
    return nullptr;
  slot->value = resized;
  slot->capacity = capacity;
  return resized;
}

const char *c2t_getenv(const char *name) {
  if (!name || !*name || strchr(name, '='))
    return nullptr;

  c2t_win32_api_init();
  if (!g_c2t_win32.GetEnvironmentVariableA || !g_c2t_win32.GetLastError ||
      !g_c2t_win32.SetLastError)
    return nullptr;

  g_c2t_win32.SetLastError(ERROR_SUCCESS);
  DWORD required = g_c2t_win32.GetEnvironmentVariableA(name, NULL, 0);
  if (required == 0 && g_c2t_win32.GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    return nullptr;

  c2t_environment_slot_t *slot =
      &environment_slots[next_environment_slot++ %
                         C2T_ENVIRONMENT_SLOT_COUNT];
  size_t capacity = required > 0 ? (size_t)required : 1U;

  for (unsigned int attempt = 0; attempt < 3; ++attempt) {
    char *value = environment_slot_reserve(slot, capacity);
    if (!value)
      return nullptr;

    g_c2t_win32.SetLastError(ERROR_SUCCESS);
    DWORD written = g_c2t_win32.GetEnvironmentVariableA(
        name, value, (DWORD)slot->capacity);
    if (written == 0) {
      if (g_c2t_win32.GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        return nullptr;
      value[0] = '\0';
      return value;
    }
    if ((size_t)written < slot->capacity)
      return value;
    capacity = (size_t)written;
  }

  return nullptr;
}

#else

const char *c2t_getenv(const char *name) {
  if (!name || !*name || strchr(name, '='))
    return nullptr;
  return getenv(name);
}

#endif
