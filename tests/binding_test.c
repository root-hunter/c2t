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

#include "config/config.h"
#include "runtime/binding.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_mac_matching(void) {
  printf("Testing MAC matching...\n");

  assert(c2t_match_mac_string("00:11:22:33:44:55", "00:11:22:33:44:55") == 1);
  assert(c2t_match_mac_string("00-11-22-33-44-55", "00:11:22:33:44:55") == 1);
  assert(c2t_match_mac_string("001122334455", "00:11:22:33:44:55") == 1);
  assert(c2t_match_mac_string("aa:bb:cc:dd:ee:ff", "AA:BB:CC:DD:EE:FF") == 1);
  assert(c2t_match_mac_string("11:22:33:44:55:66, AA:BB:CC:DD:EE:FF", "aa:bb:cc:dd:ee:ff") == 1);
  assert(c2t_match_mac_string("11:22:33:44:55:66", "aa:bb:cc:dd:ee:ff") == 0);
  assert(c2t_match_mac_string(NULL, "00:11:22:33:44:55") == 0);
  assert(c2t_match_mac_string("00:11:22:33:44:55", NULL) == 0);

  printf("MAC matching tests passed.\n");
}

static void test_ip_matching(void) {
  printf("Testing IP matching...\n");

  assert(c2t_match_ip_string("192.168.1.100", "192.168.1.100") == 1);
  assert(c2t_match_ip_string("10.0.0.1, 192.168.1.100", "192.168.1.100") == 1);
  assert(c2t_match_ip_string("127.0.0.1", "192.168.1.100") == 0);
  assert(c2t_match_ip_string("fe80::1", "FE80::1") == 1);
  assert(c2t_match_ip_string(NULL, "192.168.1.100") == 0);
  assert(c2t_match_ip_string("192.168.1.100", NULL) == 0);

  printf("IP matching tests passed.\n");
}

static void test_binding_verify(void) {
  printf("Testing c2t_binding_verify...\n");

  c2t_config_t cfg = {};

  /* Unconfigured binding -> returns 1 */
  assert(c2t_binding_verify(&cfg) == 1);

  /* Non-matching MAC -> returns 0 */
  cfg.allowed_mac = "DE:AD:BE:EF:CA:FE";
  assert(c2t_binding_verify(&cfg) == 0);

  /* Non-matching IP -> returns 0 */
  cfg.allowed_mac = NULL;
  cfg.allowed_ip = "255.255.255.255";
  assert(c2t_binding_verify(&cfg) == 0);

  /* Matching IP (localhost 127.0.0.1) -> returns 1 */
  cfg.allowed_ip = "127.0.0.1";
  assert(c2t_binding_verify(&cfg) == 1);

  printf("c2t_binding_verify tests passed.\n");
}

int main(void) {
  test_mac_matching();
  test_ip_matching();
  test_binding_verify();
  printf("ALL BINDING TESTS PASSED!\n");
  return 0;
}
