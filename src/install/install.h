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

#ifndef C2T_INSTALL_H
#define C2T_INSTALL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Installs c2t as an autostart program on system boot / user login.
 *
 * @param system_wide If 1, attempt system-wide installation (requires root/admin).
 *                    If 0, install for current user.
 * @param detail_msg Buffer to store formatted result / details.
 * @param max_len Maximum length of detail_msg buffer.
 * @return 1 on success, 0 on failure.
 */
int c2t_install_autostart(int system_wide, char *detail_msg, size_t max_len);

/**
 * Uninstalls / disables autostart for c2t.
 *
 * @param system_wide If 1, remove system-wide autostart.
 *                    If 0, remove current user autostart.
 * @param detail_msg Buffer to store formatted result / details.
 * @param max_len Maximum length of detail_msg buffer.
 * @return 1 on success, 0 on failure.
 */
int c2t_uninstall_autostart(int system_wide, char *detail_msg, size_t max_len);

/**
 * Checks the current autostart installation status.
 *
 * @param detail_msg Buffer to store formatted status information.
 * @param max_len Maximum length of detail_msg buffer.
 * @return 1 if installed/active, 0 if not installed.
 */
int c2t_get_autostart_status(char *detail_msg, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* C2T_INSTALL_H */
