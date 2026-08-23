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

#include "binding.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#if defined(__linux__)
#include <netpacket/packet.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__)
#include <net/if_dl.h>
#endif
#else
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iphlpapi.h>

typedef ULONG(WINAPI *pfn_GetAdaptersAddresses)(
    ULONG Family, ULONG Flags, PVOID Reserved,
    PIP_ADAPTER_ADDRESSES AdapterAddresses, PULONG SizePointer);
#endif

static void normalize_mac(const char *input, char *output, size_t capacity) {
  size_t out_index = 0;
  if (!input || !output || capacity == 0) {
    if (output && capacity > 0)
      output[0] = '\0';
    return;
  }

  for (size_t in_index = 0; input[in_index] != '\0' && out_index + 1 < capacity;
       ++in_index) {
    char ch = input[in_index];
    if (isxdigit((unsigned char)ch)) {
      output[out_index++] = (char)tolower((unsigned char)ch);
    }
  }
  output[out_index] = '\0';
}

int c2t_match_mac_string(const char *allowed_mac_list, const char *target_mac) {
  if (!allowed_mac_list || !*allowed_mac_list || !target_mac || !*target_mac)
    return 0;

  char target_norm[32] = {};
  normalize_mac(target_mac, target_norm, sizeof(target_norm));
  if (strlen(target_norm) < 12)
    return 0;

  const char *cursor = allowed_mac_list;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == ',' || *cursor == ';' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0')
      break;

    const char *token_start = cursor;
    while (*cursor != '\0' && *cursor != ',' && *cursor != ';' &&
           *cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
           *cursor != '\n') {
      ++cursor;
    }

    size_t token_len = (size_t)(cursor - token_start);
    if (token_len > 0) {
      char token_buf[128] = {};
      if (token_len < sizeof(token_buf)) {
        memcpy(token_buf, token_start, token_len);
        token_buf[token_len] = '\0';
        char token_norm[32] = {};
        normalize_mac(token_buf, token_norm, sizeof(token_norm));
        if (strlen(token_norm) >= 12 &&
            strcmp(token_norm, target_norm) == 0) {
          return 1;
        }
      }
    }
  }
  return 0;
}

int c2t_match_ip_string(const char *allowed_ip_list, const char *target_ip) {
  if (!allowed_ip_list || !*allowed_ip_list || !target_ip || !*target_ip)
    return 0;

  const char *cursor = allowed_ip_list;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == ',' || *cursor == ';' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0')
      break;

    const char *token_start = cursor;
    while (*cursor != '\0' && *cursor != ',' && *cursor != ';' &&
           *cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
           *cursor != '\n') {
      ++cursor;
    }

    size_t token_len = (size_t)(cursor - token_start);
    if (token_len > 0) {
      char token_buf[128] = {};
      if (token_len < sizeof(token_buf)) {
        memcpy(token_buf, token_start, token_len);
        token_buf[token_len] = '\0';

        /* Case-insensitive string comparison for IP strings */
        if (strlen(token_buf) == strlen(target_ip)) {
          size_t idx = 0;
          while (token_buf[idx] != '\0') {
            if (tolower((unsigned char)token_buf[idx]) !=
                tolower((unsigned char)target_ip[idx])) {
              break;
            }
            ++idx;
          }
          if (token_buf[idx] == '\0')
            return 1;
        }
      }
    }
  }
  return 0;
}

#ifndef _WIN32
static void check_posix_interfaces(const c2t_config_t *config, int *mac_matched,
                                    int *ip_matched) {
  struct ifaddrs *ifaddr = NULL;
  if (getifaddrs(&ifaddr) == -1 || !ifaddr)
    return;

  for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr)
      continue;

    /* Check IP address */
    if (config->allowed_ip && !*ip_matched) {
      int family = ifa->ifa_addr->sa_family;
      if (family == AF_INET || family == AF_INET6) {
        char host[INET6_ADDRSTRLEN] = {};
        if (family == AF_INET) {
          struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
          inet_ntop(AF_INET, &(sa->sin_addr), host, sizeof(host));
        } else {
          struct sockaddr_in6 *sa = (struct sockaddr_in6 *)ifa->ifa_addr;
          inet_ntop(AF_INET6, &(sa->sin6_addr), host, sizeof(host));
        }
        if (host[0] && c2t_match_ip_string(config->allowed_ip, host)) {
          *ip_matched = 1;
        }
      }
    }

    /* Check MAC address (ignore loopback) */
    if (config->allowed_mac && !*mac_matched && !(ifa->ifa_flags & IFF_LOOPBACK)) {
#if defined(__linux__)
      if (ifa->ifa_addr->sa_family == AF_PACKET) {
        struct sockaddr_ll *s = (struct sockaddr_ll *)(void *)ifa->ifa_addr;
        if (s->sll_halen == 6 &&
            (s->sll_addr[0] | s->sll_addr[1] | s->sll_addr[2] | s->sll_addr[3] |
             s->sll_addr[4] | s->sll_addr[5]) != 0) {
          char mac_str[32] = {};
          snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                   s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                   s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
          if (c2t_match_mac_string(config->allowed_mac, mac_str)) {
            *mac_matched = 1;
          }
        }
      }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__)
      if (ifa->ifa_addr->sa_family == AF_LINK) {
        struct sockaddr_dl *s = (struct sockaddr_dl *)(void *)ifa->ifa_addr;
        if (s->sdl_alen == 6) {
          unsigned char *mac = (unsigned char *)LLADDR(s);
          if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0) {
            char mac_str[32] = {};
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            if (c2t_match_mac_string(config->allowed_mac, mac_str)) {
              *mac_matched = 1;
            }
          }
        }
      }
#endif
    }
  }

  freeifaddrs(ifaddr);
}
#else
static void check_win32_interfaces(const c2t_config_t *config, int *mac_matched,
                                    int *ip_matched) {
  HMODULE hIphlpapi = LoadLibraryA("iphlpapi.dll");
  if (!hIphlpapi)
    return;

  pfn_GetAdaptersAddresses pGetAdaptersAddresses =
      (pfn_GetAdaptersAddresses)(uintptr_t)GetProcAddress(hIphlpapi,
                                                          "GetAdaptersAddresses");
  if (!pGetAdaptersAddresses) {
    FreeLibrary(hIphlpapi);
    return;
  }

  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER;
  ULONG bufLen = 15360;
  PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(bufLen);
  if (!pAddresses) {
    FreeLibrary(hIphlpapi);
    return;
  }

  ULONG dwRetVal =
      pGetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &bufLen);
  if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
    free(pAddresses);
    pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(bufLen);
    if (!pAddresses) {
      FreeLibrary(hIphlpapi);
      return;
    }
    dwRetVal =
        pGetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &bufLen);
  }

  if (dwRetVal == NO_ERROR) {
    for (PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses; pCurrAddresses;
         pCurrAddresses = pCurrAddresses->Next) {
      if (config->allowed_mac && !*mac_matched &&
          pCurrAddresses->PhysicalAddressLength == 6) {
        char mac_str[32] = {};
        snprintf(
            mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
            pCurrAddresses->PhysicalAddress[0],
            pCurrAddresses->PhysicalAddress[1],
            pCurrAddresses->PhysicalAddress[2],
            pCurrAddresses->PhysicalAddress[3],
            pCurrAddresses->PhysicalAddress[4],
            pCurrAddresses->PhysicalAddress[5]);
        if (c2t_match_mac_string(config->allowed_mac, mac_str)) {
          *mac_matched = 1;
        }
      }

      if (config->allowed_ip && !*ip_matched) {
        for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast =
                 pCurrAddresses->FirstUnicastAddress;
             pUnicast; pUnicast = pUnicast->Next) {
          if (!pUnicast->Address.lpSockaddr)
            continue;
          char host[64] = {};
          int family = pUnicast->Address.lpSockaddr->sa_family;
          if (family == AF_INET) {
            struct sockaddr_in *sa =
                (struct sockaddr_in *)(void *)pUnicast->Address.lpSockaddr;
            const unsigned char *b = (const unsigned char *)&sa->sin_addr;
            snprintf(host, sizeof(host), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
          } else if (family == AF_INET6) {
            struct sockaddr_in6 *sa =
                (struct sockaddr_in6 *)(void *)pUnicast->Address.lpSockaddr;
            const unsigned char *b = (const unsigned char *)&sa->sin6_addr;
            snprintf(host, sizeof(host),
                     "%x:%x:%x:%x:%x:%x:%x:%x",
                     ((unsigned int)b[0] << 8) | b[1], ((unsigned int)b[2] << 8) | b[3],
                     ((unsigned int)b[4] << 8) | b[5], ((unsigned int)b[6] << 8) | b[7],
                     ((unsigned int)b[8] << 8) | b[9], ((unsigned int)b[10] << 8) | b[11],
                     ((unsigned int)b[12] << 8) | b[13], ((unsigned int)b[14] << 8) | b[15]);
          }
          if (host[0] && c2t_match_ip_string(config->allowed_ip, host)) {
            *ip_matched = 1;
          }
        }
      }
    }
  }

  free(pAddresses);
  FreeLibrary(hIphlpapi);
}
#endif

int c2t_binding_verify(const c2t_config_t *config) {
  if (!config)
    return 1;
  if ((!config->allowed_mac || !*config->allowed_mac) &&
      (!config->allowed_ip || !*config->allowed_ip)) {
    return 1;
  }

  int mac_matched = (config->allowed_mac && *config->allowed_mac) ? 0 : 1;
  int ip_matched = (config->allowed_ip && *config->allowed_ip) ? 0 : 1;

#ifndef _WIN32
  check_posix_interfaces(config, &mac_matched, &ip_matched);
#else
  check_win32_interfaces(config, &mac_matched, &ip_matched);
#endif

  return (mac_matched && ip_matched);
}
