/*
 * Copyright (C) 2026 roothunter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef C2T_ENVIRONMENT_H
#define C2T_ENVIRONMENT_H

/* Returns the value of an environment variable, or nullptr when it is absent.
 * The returned string is read-only. On Windows it remains valid for at least
 * the next seven c2t_getenv calls made by the same thread. */
[[nodiscard]] const char *c2t_getenv(const char *name);

#endif /* C2T_ENVIRONMENT_H */
