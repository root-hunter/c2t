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

#ifndef C2T_BINDING_H
#define C2T_BINDING_H

#include "../config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Checks whether the current machine's network interfaces match
 * the configured binding rules (config->allowed_mac and config->allowed_ip).
 *
 * Returns 1 if verification passes (or if no binding is configured).
 * Returns 0 if verification fails (execution should be blocked).
 */
[[nodiscard]] int c2t_binding_verify(const c2t_config_t *config);

/*
 * Helper functions exported for testing MAC/IP matching logic.
 */
[[nodiscard]] int c2t_match_mac_string(const char *allowed_mac_list,
                                       const char *target_mac);
[[nodiscard]] int c2t_match_ip_string(const char *allowed_ip_list,
                                      const char *target_ip);

#ifdef __cplusplus
}
#endif

#endif /* C2T_BINDING_H */
