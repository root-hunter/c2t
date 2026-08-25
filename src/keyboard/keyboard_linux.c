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

#include "../config/config.h"
#include "../logging/logging.h"
#include "../runtime/runtime.h"
#include "keyboard.h"
#include "keyboard_output.h"
#include "../runtime/environment.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define test_bit(bit, array) ((array)[(bit) / 8] & (1 << ((bit) % 8)))
#define MAX_KEYBOARD_DEVICES 32

typedef struct {
  uint16_t key;
  const char *unshifted;
  const char *shifted;
  const char *altgr;
  const char *altgr_shifted;
} key_entry_t;

typedef struct {
  const char *code;
  const char *name;
  const char *flag_emoji;
  const key_entry_t *entries;
  size_t count;
} layout_def_t;

/* -------------------------------------------------------------------------
 * Layout definitions: Italian, US, UK, German, French, Spanish, Portuguese,
 * Swiss
 * ------------------------------------------------------------------------- */

static const key_entry_t layout_it_entries[] = {
    {KEY_GRAVE, "\\", "|", "", ""},
    {KEY_1, "1", "!", "", ""},
    {KEY_2, "2", "\"", "", ""},
    {KEY_3, "3", "£", "", ""},
    {KEY_4, "4", "$", "", ""},
    {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "&", "", ""},
    {KEY_7, "7", "/", "{", ""},
    {KEY_8, "8", "(", "[", ""},
    {KEY_9, "9", ")", "]", ""},
    {KEY_0, "0", "=", "}", ""},
    {KEY_MINUS, "'", "?", "`", ""},
    {KEY_EQUAL, "ì", "^", "~", ""},
    {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},
    {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},
    {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},
    {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},
    {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},
    {KEY_LEFTBRACE, "è", "é", "[", "{"},
    {KEY_RIGHTBRACE, "+", "*", "]", "}"},
    {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},
    {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},
    {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},
    {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},
    {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "ò", "ç", "@", ""},
    {KEY_APOSTROPHE, "à", "°", "#", ""},
    {KEY_BACKSLASH, "ù", "§", "", ""},
    {KEY_102ND, "<", ">", "", ""},
    {KEY_Z, "z", "Z", "", ""},
    {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},
    {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},
    {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},
    {KEY_COMMA, ",", ";", "", ""},
    {KEY_DOT, ".", ":", "", ""},
    {KEY_SLASH, "-", "_", "", ""},
};

static const key_entry_t layout_us_entries[] = {
    {KEY_GRAVE, "`", "~", "", ""},      {KEY_1, "1", "!", "", ""},
    {KEY_2, "2", "@", "", ""},          {KEY_3, "3", "#", "", ""},
    {KEY_4, "4", "$", "", ""},          {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "^", "", ""},          {KEY_7, "7", "&", "", ""},
    {KEY_8, "8", "*", "", ""},          {KEY_9, "9", "(", "", ""},
    {KEY_0, "0", ")", "", ""},          {KEY_MINUS, "-", "_", "", ""},
    {KEY_EQUAL, "=", "+", "", ""},      {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},          {KEY_E, "e", "E", "", ""},
    {KEY_R, "r", "R", "", ""},          {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},          {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},          {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},          {KEY_LEFTBRACE, "[", "{", "", ""},
    {KEY_RIGHTBRACE, "]", "}", "", ""}, {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},          {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},          {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},          {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},          {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, ";", ":", "", ""},  {KEY_APOSTROPHE, "'", "\"", "", ""},
    {KEY_BACKSLASH, "\\", "|", "", ""}, {KEY_102ND, "\\", "|", "", ""},
    {KEY_Z, "z", "Z", "", ""},          {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},          {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},          {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},          {KEY_COMMA, ",", "<", "", ""},
    {KEY_DOT, ".", ">", "", ""},        {KEY_SLASH, "/", "?", "", ""},
};

static const key_entry_t layout_uk_entries[] = {
    {KEY_GRAVE, "`", "¬", "¦", ""},     {KEY_1, "1", "!", "", ""},
    {KEY_2, "2", "\"", "", ""},         {KEY_3, "3", "£", "", ""},
    {KEY_4, "4", "$", "€", ""},         {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "^", "", ""},          {KEY_7, "7", "&", "", ""},
    {KEY_8, "8", "*", "", ""},          {KEY_9, "9", "(", "", ""},
    {KEY_0, "0", ")", "", ""},          {KEY_MINUS, "-", "_", "", ""},
    {KEY_EQUAL, "=", "+", "", ""},      {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},          {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},          {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},          {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},          {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},          {KEY_LEFTBRACE, "[", "{", "", ""},
    {KEY_RIGHTBRACE, "]", "}", "", ""}, {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},          {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},          {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},          {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},          {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, ";", ":", "", ""},  {KEY_APOSTROPHE, "'", "@", "", ""},
    {KEY_BACKSLASH, "#", "~", "", ""},  {KEY_102ND, "\\", "|", "", ""},
    {KEY_Z, "z", "Z", "", ""},          {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},          {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},          {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},          {KEY_COMMA, ",", "<", "", ""},
    {KEY_DOT, ".", ">", "", ""},        {KEY_SLASH, "/", "?", "", ""},
};

static const key_entry_t layout_de_entries[] = {
    {KEY_GRAVE, "^", "°", "", ""},       {KEY_1, "1", "!", "", ""},
    {KEY_2, "2", "\"", "²", ""},         {KEY_3, "3", "§", "³", ""},
    {KEY_4, "4", "$", "", ""},           {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "&", "", ""},           {KEY_7, "7", "/", "{", ""},
    {KEY_8, "8", "(", "[", ""},          {KEY_9, "9", ")", "]", ""},
    {KEY_0, "0", "=", "}", ""},          {KEY_MINUS, "ß", "?", "\\", ""},
    {KEY_EQUAL, "´", "`", "", ""},       {KEY_Q, "q", "Q", "@", ""},
    {KEY_W, "w", "W", "", ""},           {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},           {KEY_T, "t", "T", "", ""},
    {KEY_Y, "z", "Z", "", ""},           {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},           {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},           {KEY_LEFTBRACE, "ü", "Ü", "", ""},
    {KEY_RIGHTBRACE, "+", "*", "~", ""}, {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},           {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},           {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},           {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},           {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "ö", "Ö", "", ""},   {KEY_APOSTROPHE, "ä", "Ä", "", ""},
    {KEY_BACKSLASH, "#", "'", "", ""},   {KEY_102ND, "<", ">", "|", ""},
    {KEY_Z, "y", "Y", "", ""},           {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},           {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},           {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "µ", ""},          {KEY_COMMA, ",", ";", "", ""},
    {KEY_DOT, ".", ":", "", ""},         {KEY_SLASH, "-", "_", "", ""},
};

static const key_entry_t layout_fr_entries[] = {
    {KEY_GRAVE, "²", "", "", ""},        {KEY_1, "&", "1", "", ""},
    {KEY_2, "é", "2", "~", ""},          {KEY_3, "\"", "3", "#", ""},
    {KEY_4, "'", "4", "{", ""},          {KEY_5, "(", "5", "[", ""},
    {KEY_6, "-", "6", "|", ""},          {KEY_7, "è", "7", "`", ""},
    {KEY_8, "_", "8", "\\", ""},         {KEY_9, "ç", "9", "^", ""},
    {KEY_0, "à", "0", "@", ""},          {KEY_MINUS, ")", "°", "]", ""},
    {KEY_EQUAL, "=", "+", "}", ""},      {KEY_Q, "a", "A", "", ""},
    {KEY_W, "z", "Z", "", ""},           {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},           {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},           {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},           {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},           {KEY_LEFTBRACE, "^", "¨", "", ""},
    {KEY_RIGHTBRACE, "$", "£", "¤", ""}, {KEY_A, "q", "Q", "", ""},
    {KEY_S, "s", "S", "", ""},           {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},           {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},           {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},           {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "m", "M", "", ""},   {KEY_APOSTROPHE, "ù", "%", "", ""},
    {KEY_BACKSLASH, "*", "µ", "", ""},   {KEY_102ND, "<", ">", "", ""},
    {KEY_Z, "w", "W", "", ""},           {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},           {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},           {KEY_N, "n", "N", "", ""},
    {KEY_M, ",", "?", "", ""},           {KEY_COMMA, ";", ".", "", ""},
    {KEY_DOT, ":", "/", "", ""},         {KEY_SLASH, "!", "§", "", ""},
};

static const key_entry_t layout_es_entries[] = {
    {KEY_GRAVE, "\\", "|", "", ""},      {KEY_1, "1", "!", "|", ""},
    {KEY_2, "2", "\"", "@", ""},         {KEY_3, "3", "·", "#", ""},
    {KEY_4, "4", "$", "~", ""},          {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "&", "¬", ""},          {KEY_7, "7", "/", "", ""},
    {KEY_8, "8", "(", "", ""},           {KEY_9, "9", ")", "", ""},
    {KEY_0, "0", "=", "", ""},           {KEY_MINUS, "'", "?", "\\", ""},
    {KEY_EQUAL, "¡", "¿", "", ""},       {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},           {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},           {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},           {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},           {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},           {KEY_LEFTBRACE, "`", "^", "[", ""},
    {KEY_RIGHTBRACE, "+", "*", "]", ""}, {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},           {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},           {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},           {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},           {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "ñ", "Ñ", "", ""},   {KEY_APOSTROPHE, "´", "¨", "{", ""},
    {KEY_BACKSLASH, "ç", "Ç", "}", ""},  {KEY_102ND, "<", ">", "", ""},
    {KEY_Z, "z", "Z", "", ""},           {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},           {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},           {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},           {KEY_COMMA, ",", ";", "", ""},
    {KEY_DOT, ".", ":", "", ""},         {KEY_SLASH, "-", "_", "", ""},
};

static const key_entry_t layout_pt_entries[] = {
    {KEY_GRAVE, "\\", "|", "", ""},     {KEY_1, "1", "!", "", ""},
    {KEY_2, "2", "\"", "@", ""},        {KEY_3, "3", "#", "£", ""},
    {KEY_4, "4", "$", "§", ""},         {KEY_5, "5", "%", "€", ""},
    {KEY_6, "6", "&", "", ""},          {KEY_7, "7", "/", "{", ""},
    {KEY_8, "8", "(", "[", ""},         {KEY_9, "9", ")", "]", ""},
    {KEY_0, "0", "=", "}", ""},         {KEY_MINUS, "'", "?", "", ""},
    {KEY_EQUAL, "«", "»", "", ""},      {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},          {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},          {KEY_T, "t", "T", "", ""},
    {KEY_Y, "y", "Y", "", ""},          {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},          {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},          {KEY_LEFTBRACE, "+", "*", "", ""},
    {KEY_RIGHTBRACE, "´", "`", "", ""}, {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},          {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},          {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},          {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},          {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "ç", "Ç", "", ""},  {KEY_APOSTROPHE, "º", "ª", "", ""},
    {KEY_BACKSLASH, "~", "^", "", ""},  {KEY_102ND, "<", ">", "", ""},
    {KEY_Z, "z", "Z", "", ""},          {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},          {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},          {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},          {KEY_COMMA, ",", ";", "", ""},
    {KEY_DOT, ".", ":", "", ""},        {KEY_SLASH, "-", "_", "", ""},
};

static const key_entry_t layout_ch_entries[] = {
    {KEY_GRAVE, "§", "°", "", ""},
    {KEY_1, "1", "+", "¦", ""},
    {KEY_2, "2", "\"", "@", ""},
    {KEY_3, "3", "*", "#", ""},
    {KEY_4, "4", "ç", "°", ""},
    {KEY_5, "5", "%", "", ""},
    {KEY_6, "6", "&", "¬", ""},
    {KEY_7, "7", "/", "|", ""},
    {KEY_8, "8", "(", "¢", ""},
    {KEY_9, "9", ")", "", ""},
    {KEY_0, "0", "=", "", ""},
    {KEY_MINUS, "'", "?", "´", ""},
    {KEY_EQUAL, "^", "`", "~", ""},
    {KEY_Q, "q", "Q", "", ""},
    {KEY_W, "w", "W", "", ""},
    {KEY_E, "e", "E", "€", ""},
    {KEY_R, "r", "R", "", ""},
    {KEY_T, "t", "T", "", ""},
    {KEY_Y, "z", "Z", "", ""},
    {KEY_U, "u", "U", "", ""},
    {KEY_I, "i", "I", "", ""},
    {KEY_O, "o", "O", "", ""},
    {KEY_P, "p", "P", "", ""},
    {KEY_LEFTBRACE, "è", "ü", "[", "{"},
    {KEY_RIGHTBRACE, "¨", "!", "]", "}"},
    {KEY_A, "a", "A", "", ""},
    {KEY_S, "s", "S", "", ""},
    {KEY_D, "d", "D", "", ""},
    {KEY_F, "f", "F", "", ""},
    {KEY_G, "g", "G", "", ""},
    {KEY_H, "h", "H", "", ""},
    {KEY_J, "j", "J", "", ""},
    {KEY_K, "k", "K", "", ""},
    {KEY_L, "l", "L", "", ""},
    {KEY_SEMICOLON, "é", "ö", "", ""},
    {KEY_APOSTROPHE, "à", "ä", "{", ""},
    {KEY_BACKSLASH, "$", "£", "}", ""},
    {KEY_102ND, "<", ">", "\\", ""},
    {KEY_Z, "y", "Y", "", ""},
    {KEY_X, "x", "X", "", ""},
    {KEY_C, "c", "C", "", ""},
    {KEY_V, "v", "V", "", ""},
    {KEY_B, "b", "B", "", ""},
    {KEY_N, "n", "N", "", ""},
    {KEY_M, "m", "M", "", ""},
    {KEY_COMMA, ",", ";", "", ""},
    {KEY_DOT, ".", ":", "", ""},
    {KEY_SLASH, "-", "_", "", ""},
};

static const layout_def_t all_layouts[] = {
    {"it", "Italian (QWERTY IT)", "🇮🇹", layout_it_entries,
     sizeof(layout_it_entries) / sizeof(layout_it_entries[0])},
    {"us", "US English (QWERTY US)", "🇺🇸", layout_us_entries,
     sizeof(layout_us_entries) / sizeof(layout_us_entries[0])},
    {"uk", "UK English (QWERTY UK)", "🇬🇧", layout_uk_entries,
     sizeof(layout_uk_entries) / sizeof(layout_uk_entries[0])},
    {"de", "German (QWERTZ DE)", "🇩🇪", layout_de_entries,
     sizeof(layout_de_entries) / sizeof(layout_de_entries[0])},
    {"fr", "French (AZERTY FR)", "🇫🇷", layout_fr_entries,
     sizeof(layout_fr_entries) / sizeof(layout_fr_entries[0])},
    {"es", "Spanish (QWERTY ES)", "🇪🇸", layout_es_entries,
     sizeof(layout_es_entries) / sizeof(layout_es_entries[0])},
    {"pt", "Portuguese (QWERTY PT)", "🇵🇹", layout_pt_entries,
     sizeof(layout_pt_entries) / sizeof(layout_pt_entries[0])},
    {"ch", "Swiss (QWERTZ CH)", "🇨🇭", layout_ch_entries,
     sizeof(layout_ch_entries) / sizeof(layout_ch_entries[0])},
};

static const size_t layout_count = sizeof(all_layouts) / sizeof(all_layouts[0]);
static size_t current_layout_index = 0;
static const key_entry_t *active_direct_keymap[256];
static pthread_mutex_t layout_lock = PTHREAD_MUTEX_INITIALIZER;

static void rebuild_direct_keymap_locked(size_t layout_idx) {
  memset((void *)active_direct_keymap, 0, sizeof(active_direct_keymap));
  if (layout_idx >= layout_count)
    return;
  const layout_def_t *cur = &all_layouts[layout_idx];
  for (size_t i = 0; i < cur->count; i++) {
    if (cur->entries[i].key < 256) {
      active_direct_keymap[cur->entries[i].key] = &cur->entries[i];
    }
  }
}

static const char *detect_system_keyboard_layout(void) {
  /* 1. Check /etc/default/keyboard */
  FILE *f = fopen("/etc/default/keyboard", "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      char *p = line;
      while (isspace((unsigned char)*p))
        p++;
      if (strncmp(p, "XKBLAYOUT=", 10) == 0) {
        p += 10;
        while (*p == '"' || *p == '\'')
          p++;
        char layout_code[16] = {0};
        size_t i = 0;
        while (p[i] && !isspace((unsigned char)p[i]) && p[i] != '"' &&
               p[i] != '\'' && p[i] != ',' && i + 1 < sizeof(layout_code)) {
          layout_code[i] = (char)tolower((unsigned char)p[i]);
          i++;
        }
        layout_code[i] = '\0';
        fclose(f);
        if (strcmp(layout_code, "it") == 0)
          return "it";
        if (strcmp(layout_code, "us") == 0)
          return "us";
        if (strcmp(layout_code, "gb") == 0 || strcmp(layout_code, "uk") == 0)
          return "uk";
        if (strcmp(layout_code, "de") == 0)
          return "de";
        if (strcmp(layout_code, "fr") == 0)
          return "fr";
        if (strcmp(layout_code, "es") == 0)
          return "es";
        if (strcmp(layout_code, "pt") == 0)
          return "pt";
        if (strcmp(layout_code, "ch") == 0)
          return "ch";
      }
    }
    fclose(f);
  }

  /* 2. Check /etc/vconsole.conf */
  f = fopen("/etc/vconsole.conf", "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      char *p = line;
      while (isspace((unsigned char)*p))
        p++;
      if (strncmp(p, "KEYMAP=", 7) == 0) {
        p += 7;
        while (*p == '"' || *p == '\'')
          p++;
        char kmap[32] = {0};
        size_t i = 0;
        while (p[i] && !isspace((unsigned char)p[i]) && p[i] != '"' &&
               p[i] != '\'' && i + 1 < sizeof(kmap)) {
          kmap[i] = (char)tolower((unsigned char)p[i]);
          i++;
        }
        kmap[i] = '\0';
        fclose(f);
        if (strncmp(kmap, "it", 2) == 0)
          return "it";
        if (strncmp(kmap, "us", 2) == 0)
          return "us";
        if (strncmp(kmap, "uk", 2) == 0 || strncmp(kmap, "gb", 2) == 0)
          return "uk";
        if (strncmp(kmap, "de", 2) == 0)
          return "de";
        if (strncmp(kmap, "fr", 2) == 0)
          return "fr";
        if (strncmp(kmap, "es", 2) == 0)
          return "es";
        if (strncmp(kmap, "pt", 2) == 0)
          return "pt";
        if (strncmp(kmap, "ch", 2) == 0 || strncmp(kmap, "sg", 2) == 0)
          return "ch";
      }
    }
    fclose(f);
  }

  /* 3. Infer from environment locale */
  const char *lang = c2t_getenv("LC_ALL");
  if (!lang || !*lang)
    lang = c2t_getenv("LANG");
  if (!lang || !*lang)
    lang = c2t_getenv("LC_MESSAGES");
  if (lang && *lang) {
    if (strncasecmp(lang, "it", 2) == 0)
      return "it";
    if (strncasecmp(lang, "de", 2) == 0)
      return "de";
    if (strncasecmp(lang, "fr", 2) == 0)
      return "fr";
    if (strncasecmp(lang, "es", 2) == 0)
      return "es";
    if (strncasecmp(lang, "pt", 2) == 0)
      return "pt";
    if (strncasecmp(lang, "en_GB", 5) == 0 ||
        strncasecmp(lang, "en_UK", 5) == 0)
      return "uk";
  }

  return "it"; /* Default to Italian when unspecified on local host */
}

int keyboard_set_layout(const char *layout_name) {
  if (!layout_name || !*layout_name)
    return 0;
  while (isspace((unsigned char)*layout_name))
    layout_name++;

  size_t target_idx = layout_count;

  /* Fast O(1) resolution by 2-character code */
  if (layout_name[0] && layout_name[1] && (!layout_name[2] || isspace((unsigned char)layout_name[2]))) {
    uint16_t code_tag = (uint16_t)tolower((unsigned char)layout_name[0]) |
                        ((uint16_t)tolower((unsigned char)layout_name[1]) << 8);
    switch (code_tag) {
    case (('i') | ('t' << 8)): target_idx = 0; break; /* it */
    case (('u') | ('s' << 8)): target_idx = 1; break; /* us */
    case (('u') | ('k' << 8)): /* uk */
    case (('g') | ('b' << 8)): target_idx = 2; break; /* gb alias */
    case (('d') | ('e' << 8)): target_idx = 3; break; /* de */
    case (('f') | ('r' << 8)): target_idx = 4; break; /* fr */
    case (('e') | ('s' << 8)): target_idx = 5; break; /* es */
    case (('p') | ('t' << 8)): target_idx = 6; break; /* pt */
    case (('c') | ('h' << 8)): /* ch */
    case (('s') | ('g' << 8)): target_idx = 7; break; /* sg alias */
    default: break;
    }
  }

  pthread_mutex_lock(&layout_lock);
  if (target_idx < layout_count) {
    current_layout_index = target_idx;
    rebuild_direct_keymap_locked(target_idx);
    pthread_mutex_unlock(&layout_lock);
    c2t_log_info("keyboard", "Active keyboard layout changed to %s (%s)",
                 all_layouts[target_idx].name, all_layouts[target_idx].code);
    return 1;
  }

  /* Fallback for long descriptive layout names */
  for (size_t i = 0; i < layout_count; i++) {
    if (strcasecmp(all_layouts[i].name, layout_name) == 0 ||
        strcasecmp(all_layouts[i].code, layout_name) == 0) {
      current_layout_index = i;
      rebuild_direct_keymap_locked(i);
      pthread_mutex_unlock(&layout_lock);
      c2t_log_info("keyboard", "Active keyboard layout changed to %s (%s)",
                   all_layouts[i].name, all_layouts[i].code);
      return 1;
    }
  }
  pthread_mutex_unlock(&layout_lock);
  return 0;
}

void keyboard_get_layout(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;
  pthread_mutex_lock(&layout_lock);
  const layout_def_t *cur = &all_layouts[current_layout_index];
  snprintf(buffer, max_len, "%s %s (<code>%s</code>)", cur->flag_emoji,
           cur->name, cur->code);
  pthread_mutex_unlock(&layout_lock);
}

void keyboard_get_available_layouts(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;
  pthread_mutex_lock(&layout_lock);
  size_t off = (size_t)snprintf(buffer, max_len,
                                "🌐 <b>Supported Keyboard Layouts:</b>\n\n");
  for (size_t i = 0; i < layout_count && off + 128 < max_len; i++) {
    int is_cur = (i == current_layout_index);
    off += (size_t)snprintf(buffer + off, max_len - off,
                            "• %s <code>/keyboard_layout %s</code> — %s%s\n",
                            all_layouts[i].flag_emoji, all_layouts[i].code,
                            all_layouts[i].name,
                            is_cur ? " 🟢 <b>[CURRENT]</b>" : "");
  }
  pthread_mutex_unlock(&layout_lock);
}

/* -------------------------------------------------------------------------
 * Device management
 * ------------------------------------------------------------------------- */

typedef struct {
  int fd;
  char path[256];
  char name[256];
  int lshift_active;
  int rshift_active;
  int lctrl_active;
  int rctrl_active;
  int lalt_active;
  int ralt_active;
  int lmeta_active;
  int rmeta_active;
  int sync_dropped;
} keyboard_device_t;

static pthread_t listener_thread;
static int listener_started;
static volatile int stopping;
static pthread_mutex_t devices_lock = PTHREAD_MUTEX_INITIALIZER;
static char selected_target[256] = "all";
static int selected_index = -1; /* -1 = all, >= 0 = index, -2 = string */
static keyboard_device_t active_devices[MAX_KEYBOARD_DEVICES];
static int active_device_count;

static const char *c2t_strcasestr(const char *haystack, const char *needle) {
  if (!haystack || !needle)
    return nullptr;
  if (!*needle)
    return haystack;
  size_t needle_len = strlen(needle);
  for (; *haystack; haystack++) {
    if (strncasecmp(haystack, needle, needle_len) == 0) {
      return haystack;
    }
  }
  return nullptr;
}

static int is_device_selected_locked(int index, const char *path,
                                     const char *name) {
  if (selected_index == -1 || strcmp(selected_target, "all") == 0 ||
      strcmp(selected_target, "*") == 0)
    return 1;
  if (selected_index >= 0 && selected_index == index)
    return 1;
  if (path && strcmp(selected_target, path) == 0)
    return 1;
  if (path) {
    const char *slash = strrchr(path, '/');
    if (slash && strcmp(slash + 1, selected_target) == 0)
      return 1;
  }
  if (name && c2t_strcasestr(name, selected_target) != nullptr)
    return 1;
  return 0;
}

static int lshift_active;
static int rshift_active;
static int shift_active;
static int caps_lock_active;
static int lctrl_active;
static int rctrl_active;
static int ctrl_active;
static int lalt_active;
static int ralt_active;
static int alt_active;
static int altgr_active;
static int lmeta_active;
static int rmeta_active;
static int meta_active;

static void recompute_modifier_state(const keyboard_device_t *devices,
                                     int count) {
  lshift_active = 0;
  rshift_active = 0;
  lctrl_active = 0;
  rctrl_active = 0;
  lalt_active = 0;
  ralt_active = 0;
  lmeta_active = 0;
  rmeta_active = 0;

  for (int i = 0; i < count; ++i) {
    lshift_active |= devices[i].lshift_active;
    rshift_active |= devices[i].rshift_active;
    lctrl_active |= devices[i].lctrl_active;
    rctrl_active |= devices[i].rctrl_active;
    lalt_active |= devices[i].lalt_active;
    ralt_active |= devices[i].ralt_active;
    lmeta_active |= devices[i].lmeta_active;
    rmeta_active |= devices[i].rmeta_active;
  }

  shift_active = lshift_active || rshift_active;
  ctrl_active = lctrl_active || rctrl_active;
  alt_active = lalt_active;
  altgr_active = ralt_active;
  meta_active = lmeta_active || rmeta_active;
}

static void query_device_state(keyboard_device_t *device) {
  if (!device || device->fd < 0)
    return;

  unsigned char keys[KEY_MAX / 8 + 1] = {0};
  if (ioctl(device->fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
    device->lshift_active = !!test_bit(KEY_LEFTSHIFT, keys);
    device->rshift_active = !!test_bit(KEY_RIGHTSHIFT, keys);
    device->lctrl_active = !!test_bit(KEY_LEFTCTRL, keys);
    device->rctrl_active = !!test_bit(KEY_RIGHTCTRL, keys);
    device->lalt_active = !!test_bit(KEY_LEFTALT, keys);
    device->ralt_active = !!test_bit(KEY_RIGHTALT, keys);
    device->lmeta_active = !!test_bit(KEY_LEFTMETA, keys);
    device->rmeta_active = !!test_bit(KEY_RIGHTMETA, keys);
  }

  unsigned char leds[LED_MAX / 8 + 1] = {0};
  if (ioctl(device->fd, EVIOCGLED(sizeof(leds)), leds) >= 0)
    caps_lock_active = !!test_bit(LED_CAPSL, leds);
}

static int is_keyboard(const char *devpath) {
  int fd = open(devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return 0;

  unsigned char evbits[EV_MAX / 8 + 1] = {0};
  unsigned char keybits[KEY_MAX / 8 + 1] = {0};

  if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
    close(fd);
    return 0;
  }

  int result = 0;
  if (test_bit(EV_KEY, evbits)) {
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
      if (test_bit(KEY_A, keybits) && test_bit(KEY_Z, keybits) &&
          test_bit(KEY_ENTER, keybits) && test_bit(KEY_SPACE, keybits)) {
        result = 1;
      }
    }
  }

  close(fd);
  return result;
}

static int is_keyboard_state_key(uint32_t key) {
  return key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT ||
         key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL ||
         key == KEY_LEFTALT || key == KEY_RIGHTALT || key == KEY_LEFTMETA ||
         key == KEY_RIGHTMETA || key == KEY_CAPSLOCK;
}

static void translate_and_emit_key(keyboard_device_t *devices, int count,
                                   int device_index, uint32_t key,
                                   int ev_value) {
  keyboard_device_t *device = &devices[device_index];
  int pressed = (ev_value != 0);
  int is_initial_press = (ev_value == 1);

  if (key == KEY_LEFTSHIFT) {
    device->lshift_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_RIGHTSHIFT) {
    device->rshift_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_LEFTCTRL) {
    device->lctrl_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_RIGHTCTRL) {
    device->rctrl_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_LEFTALT) {
    device->lalt_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_RIGHTALT) {
    device->ralt_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_LEFTMETA) {
    device->lmeta_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_RIGHTMETA) {
    device->rmeta_active = pressed;
    recompute_modifier_state(devices, count);
    return;
  }
  if (key == KEY_CAPSLOCK) {
    if (is_initial_press) {
      caps_lock_active = !caps_lock_active;
    }
    return;
  }

  if (!pressed)
    return;

  char key_label[64] = {0};
  int is_special = 0;
  int is_printable = 0;

  /* Handle special & function keys */
  if (key >= KEY_F1 && key <= KEY_F10) {
    snprintf(key_label, sizeof(key_label), "F%u", key - KEY_F1 + 1);
    is_special = 1;
  } else if (key == KEY_F11) {
    snprintf(key_label, sizeof(key_label), "F11");
    is_special = 1;
  } else if (key == KEY_F12) {
    snprintf(key_label, sizeof(key_label), "F12");
    is_special = 1;
  } else if (key >= KEY_F13 && key <= KEY_F24) {
    snprintf(key_label, sizeof(key_label), "F%u", key - KEY_F13 + 13);
    is_special = 1;
  } else {
    switch (key) {
    case KEY_ENTER:
    case KEY_KPENTER:
      key_label[0] = '\n';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_SPACE:
      key_label[0] = ' ';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_TAB:
      key_label[0] = '\t';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_BACKSPACE:
      keyboard_output_backspace();
      return;
    case KEY_ESC:
      snprintf(key_label, sizeof(key_label), "ESC");
      is_special = 1;
      break;
    case KEY_DELETE:
      snprintf(key_label, sizeof(key_label), "Del");
      is_special = 1;
      break;
    case KEY_INSERT:
      snprintf(key_label, sizeof(key_label), "Ins");
      is_special = 1;
      break;
    case KEY_HOME:
      snprintf(key_label, sizeof(key_label), "Home");
      is_special = 1;
      break;
    case KEY_END:
      snprintf(key_label, sizeof(key_label), "End");
      is_special = 1;
      break;
    case KEY_PAGEUP:
      snprintf(key_label, sizeof(key_label), "PgUp");
      is_special = 1;
      break;
    case KEY_PAGEDOWN:
      snprintf(key_label, sizeof(key_label), "PgDn");
      is_special = 1;
      break;
    case KEY_UP:
      snprintf(key_label, sizeof(key_label), "Up");
      is_special = 1;
      break;
    case KEY_DOWN:
      snprintf(key_label, sizeof(key_label), "Down");
      is_special = 1;
      break;
    case KEY_LEFT:
      snprintf(key_label, sizeof(key_label), "Left");
      is_special = 1;
      break;
    case KEY_RIGHT:
      snprintf(key_label, sizeof(key_label), "Right");
      is_special = 1;
      break;
    case KEY_PRINT:
    case KEY_SYSRQ:
      snprintf(key_label, sizeof(key_label), "PrtScn");
      is_special = 1;
      break;
    case KEY_PAUSE:
      snprintf(key_label, sizeof(key_label), "Pause");
      is_special = 1;
      break;
    case KEY_SCROLLLOCK:
      snprintf(key_label, sizeof(key_label), "ScrollLock");
      is_special = 1;
      break;
    case KEY_NUMLOCK:
      snprintf(key_label, sizeof(key_label), "NumLock");
      is_special = 1;
      break;
    case KEY_KP0:
    case KEY_KP1:
    case KEY_KP2:
    case KEY_KP3:
    case KEY_KP4:
    case KEY_KP5:
    case KEY_KP6:
    case KEY_KP7:
    case KEY_KP8:
    case KEY_KP9:
      key_label[0] = (char)('0' + (key - KEY_KP0));
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPDOT:
    case KEY_KPCOMMA:
      key_label[0] = '.';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPPLUS:
      key_label[0] = '+';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPMINUS:
      key_label[0] = '-';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPASTERISK:
      key_label[0] = '*';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPSLASH:
      key_label[0] = '/';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    case KEY_KPEQUAL:
      key_label[0] = '=';
      key_label[1] = '\0';
      is_printable = 1;
      break;
    default: {
      /* Map through active keyboard layout (O(1) direct lookup) */
      pthread_mutex_lock(&layout_lock);
      const key_entry_t *found_entry =
          (key < 256) ? active_direct_keymap[key] : nullptr;

      if (found_entry) {
        const char *glyph = nullptr;

        if (altgr_active) {
          if (shift_active && found_entry->altgr_shifted &&
              *found_entry->altgr_shifted) {
            glyph = found_entry->altgr_shifted;
          } else if (found_entry->altgr && *found_entry->altgr) {
            glyph = found_entry->altgr;
          }
        }

        if (!glyph) {
          if (found_entry->unshifted && found_entry->unshifted[0] >= 'a' &&
              found_entry->unshifted[0] <= 'z' &&
              found_entry->unshifted[1] == '\0') {
            int uppercase = shift_active ^ caps_lock_active;
            glyph = uppercase ? found_entry->shifted : found_entry->unshifted;
          } else {
            glyph =
                shift_active ? found_entry->shifted : found_entry->unshifted;
          }
        }

        if (glyph && *glyph) {
          snprintf(key_label, sizeof(key_label), "%s", glyph);
          is_printable = 1;
        }
      } else {
        /* Unknown keycode fallback */
        snprintf(key_label, sizeof(key_label), "Key_%u", key);
        is_special = 1;
      }
      pthread_mutex_unlock(&layout_lock);
      break;
    }
    }
  }

  if (!is_special && !is_printable)
    return;

  int is_altgr_symbol = altgr_active && !alt_active && is_printable;
  int has_modifier =
      (ctrl_active || (alt_active && !altgr_active) || meta_active) &&
      !is_altgr_symbol;
  if (has_modifier && key_label[0] != '\n') {
    if (keyboard_get_shortcuts_enabled()) {
      char mod_buf[96];
      int offset = snprintf(mod_buf, sizeof(mod_buf), "[");
      if (ctrl_active)
        offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Ctrl+");
      if (alt_active)
        offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Alt+");
      if (meta_active)
        offset +=
            snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Super+");
      if (shift_active && !is_printable)
        offset +=
            snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "Shift+");
      offset += snprintf(mod_buf + offset, sizeof(mod_buf) - offset, "%s]",
                         key_label);
      if (offset > 0 && (size_t)offset < sizeof(mod_buf)) {
        keyboard_output_append(mod_buf, (size_t)offset);
      }
    }
  } else if (is_special) {
    if (keyboard_get_shortcuts_enabled()) {
      char spec_buf[48];
      int spec_len = snprintf(spec_buf, sizeof(spec_buf), "[%s]", key_label);
      if (spec_len > 0) {
        keyboard_output_append(spec_buf, (size_t)spec_len);
      }
    }
  } else if (is_printable || is_altgr_symbol) {
    keyboard_output_append(key_label, strlen(key_label));
  }
}

static void add_device(keyboard_device_t *devices, int *count,
                       const char *path) {
  if (*count >= MAX_KEYBOARD_DEVICES)
    return;

  for (int i = 0; i < *count; i++) {
    if (strcmp(devices[i].path, path) == 0)
      return;
  }

  int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    c2t_log_debug("keyboard", "Unable to open %s: %s", path, strerror(errno));
    return;
  }

  char name[256] = "Unknown";
  (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);

  memset(&devices[*count], 0, sizeof(devices[*count]));
  devices[*count].fd = fd;
  snprintf(devices[*count].path, sizeof(devices[*count].path), "%s", path);
  snprintf(devices[*count].name, sizeof(devices[*count].name), "%s", name);
  query_device_state(&devices[*count]);
  (*count)++;
  recompute_modifier_state(devices, *count);

  pthread_mutex_lock(&devices_lock);
  if (*count <= MAX_KEYBOARD_DEVICES) {
    active_devices[*count - 1] = devices[*count - 1];
    active_device_count = *count;
  }
  pthread_mutex_unlock(&devices_lock);

  c2t_log_info("keyboard", "Listening on keyboard device: %s (%s)", path, name);
}

static void remove_device(keyboard_device_t *devices, int *count, int index) {
  if (index < 0 || index >= *count)
    return;

  c2t_log_info("keyboard", "Keyboard device disconnected: %s",
               devices[index].path);
  close(devices[index].fd);

  for (int i = index; i < *count - 1; i++) {
    devices[i] = devices[i + 1];
  }
  (*count)--;
  recompute_modifier_state(devices, *count);

  pthread_mutex_lock(&devices_lock);
  for (int i = 0; i < *count; i++) {
    active_devices[i] = devices[i];
  }
  active_device_count = *count;
  pthread_mutex_unlock(&devices_lock);
}

static int scan_and_attach_keyboards(keyboard_device_t *devices, int *count) {
  const char *dir = "/dev/input";
  DIR *d = opendir(dir);
  if (!d) {
    c2t_log_error("keyboard", "Unable to open %s: %s", dir, strerror(errno));
    return 0;
  }

  struct dirent *entry;
  char path[256];

  while ((entry = readdir(d)) != nullptr) {
    if (strncmp(entry->d_name, "event", 5) != 0)
      continue;

    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

    if (is_keyboard(path)) {
      add_device(devices, count, path);
    }
  }

  closedir(d);
  return *count;
}

static void handle_hotplug(keyboard_device_t *devices, int *count,
                           int inotify_fd) {
  char buf[1024] __attribute__((aligned(__alignof__(struct inotify_event))));
  ssize_t len = read(inotify_fd, buf, sizeof(buf));
  if (len <= 0)
    return;

  const struct inotify_event *event;
  for (char *ptr = buf; ptr < buf + len;
       ptr += sizeof(struct inotify_event) + event->len) {
    event = (const struct inotify_event *)ptr;
    if (event->len > 0 && strncmp(event->name, "event", 5) == 0) {
      char path[256];
      snprintf(path, sizeof(path), "/dev/input/%s", event->name);
      if (is_keyboard(path)) {
        add_device(devices, count, path);
      }
    }
  }
}

int keyboard_listen(void) {
  keyboard_device_t devices[MAX_KEYBOARD_DEVICES];
  int device_count = 0;

  scan_and_attach_keyboards(devices, &device_count);
  if (device_count == 0) {
    c2t_log_warning("keyboard",
                    "No keyboard devices currently found in /dev/input");
  }

  int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd >= 0) {
    (void)inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE | IN_ATTRIB);
  }

  int stop_fd = c2t_runtime_stop_descriptor();

  while (!stopping && !c2t_runtime_stop_requested()) {
    struct pollfd pfds[MAX_KEYBOARD_DEVICES + 2];
    int nfds = 0;

    for (int i = 0; i < device_count; i++) {
      pfds[nfds].fd = devices[i].fd;
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      nfds++;
    }

    int inotify_idx = -1;
    if (inotify_fd >= 0) {
      inotify_idx = nfds++;
      pfds[inotify_idx].fd = inotify_fd;
      pfds[inotify_idx].events = POLLIN;
      pfds[inotify_idx].revents = 0;
    }

    int stop_idx = -1;
    if (stop_fd >= 0) {
      stop_idx = nfds++;
      pfds[stop_idx].fd = stop_fd;
      pfds[stop_idx].events = POLLIN;
      pfds[stop_idx].revents = 0;
    }

    int poll_device_count = device_count;
    int n = poll(pfds, (nfds_t)nfds, 1000);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      c2t_log_error("keyboard", "poll() failed: %s", strerror(errno));
      break;
    }

    if (stopping || c2t_runtime_stop_requested())
      break;

    if (n == 0)
      continue;

    if (stop_idx >= 0 && (pfds[stop_idx].revents & POLLIN))
      break;

    if (inotify_idx >= 0 && (pfds[inotify_idx].revents & POLLIN)) {
      handle_hotplug(devices, &device_count, inotify_fd);
    }

    for (int i = poll_device_count - 1; i >= 0; i--) {
      if (i >= device_count)
        continue;

      if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
        remove_device(devices, &device_count, i);
        continue;
      }

      if (pfds[i].revents & POLLIN) {
        struct input_event events[64];
        ssize_t bytes;
        while ((bytes = read(devices[i].fd, events, sizeof(events))) > 0) {
          pthread_mutex_lock(&devices_lock);
          int selected =
              is_device_selected_locked(i, devices[i].path, devices[i].name);
          pthread_mutex_unlock(&devices_lock);

          size_t count = (size_t)bytes / sizeof(struct input_event);
          for (size_t j = 0; j < count; j++) {
            if (events[j].type == EV_SYN &&
                events[j].code == SYN_DROPPED) {
              devices[i].sync_dropped = 1;
              continue;
            }
            if (devices[i].sync_dropped) {
              if (events[j].type == EV_SYN &&
                  events[j].code == SYN_REPORT) {
                query_device_state(&devices[i]);
                recompute_modifier_state(devices, device_count);
                devices[i].sync_dropped = 0;
              }
              continue;
            }
            if (events[j].type == EV_KEY &&
                (selected || is_keyboard_state_key(events[j].code)))
              translate_and_emit_key(devices, device_count, i,
                                     events[j].code, events[j].value);
          }
        }
        if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          remove_device(devices, &device_count, i);
        }
      }
    }
  }

  for (int i = 0; i < device_count; i++) {
    close(devices[i].fd);
  }

  if (inotify_fd >= 0) {
    close(inotify_fd);
  }

  pthread_mutex_lock(&devices_lock);
  active_device_count = 0;
  pthread_mutex_unlock(&devices_lock);

  keyboard_output_flush();
  return 0;
}

static void *listener_worker([[maybe_unused]] void *context) {
  (void)keyboard_listen();
  return nullptr;
}

int keyboard_listener_init(void) {
  if (listener_started)
    return 1;

  stopping = 0;
  shift_active = 0;
  caps_lock_active = 0;
  ctrl_active = 0;
  alt_active = 0;
  altgr_active = 0;
  meta_active = 0;

  pthread_mutex_lock(&layout_lock);
  rebuild_direct_keymap_locked(0);
  pthread_mutex_unlock(&layout_lock);

  const c2t_config_t *cfg = c2t_config_get();
  if (cfg->keyboard_layout && *cfg->keyboard_layout) {
    (void)keyboard_set_layout(cfg->keyboard_layout);
  } else {
    const char *detected = detect_system_keyboard_layout();
    (void)keyboard_set_layout(detected);
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);
  listener_started =
      (pthread_create(&listener_thread, &attr, listener_worker, nullptr) == 0);
  pthread_attr_destroy(&attr);

  if (!listener_started) {
    c2t_log_error("keyboard", "Unable to start keyboard listener thread");
  }
  return listener_started;
}

void keyboard_listener_cleanup(void) {
  if (!listener_started)
    return;

  stopping = 1;
  (void)pthread_join(listener_thread, nullptr);
  listener_started = 0;
}

int keyboard_get_device_list(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return 0;

  pthread_mutex_lock(&devices_lock);

  keyboard_device_t temp_devs[MAX_KEYBOARD_DEVICES];
  int count = 0;

  if (active_device_count > 0) {
    count = active_device_count;
    for (int i = 0; i < count; i++) {
      temp_devs[i] = active_devices[i];
    }
  } else {
    const char *dir = "/dev/input";
    DIR *d = opendir(dir);
    if (d) {
      struct dirent *entry;
      char path[256];
      while ((entry = readdir(d)) != nullptr && count < MAX_KEYBOARD_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) != 0)
          continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if (is_keyboard(path)) {
          int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
          if (fd >= 0) {
            char name[256] = "Unknown";
            (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            close(fd);
            snprintf(temp_devs[count].path, sizeof(temp_devs[count].path), "%s",
                     path);
            snprintf(temp_devs[count].name, sizeof(temp_devs[count].name), "%s",
                     name);
            temp_devs[count].fd = -1;
            count++;
          }
        }
      }
      closedir(d);
    }
  }

  if (count == 0) {
    snprintf(buffer, max_len,
             "⌨️ <b>Keyboard Devices:</b>\n\n"
             "⚠️ <i>No keyboard devices found in /dev/input.</i>\n"
             "(Check read permissions for /dev/input/event*)");
    pthread_mutex_unlock(&devices_lock);
    return 1;
  }

  size_t offset = (size_t)snprintf(
      buffer, max_len, "⌨️ <b>Detected Keyboard Devices (%d):</b>\n\n", count);

  for (int i = 0; i < count && offset + 128 < max_len; i++) {
    int active =
        is_device_selected_locked(i, temp_devs[i].path, temp_devs[i].name);
    offset += (size_t)snprintf(buffer + offset, max_len - offset,
                               "• <b>[%d]</b> <code>%s</code>\n"
                               "  🏷️ <i>%s</i> — %s\n",
                               i, temp_devs[i].path,
                               temp_devs[i].name[0] ? temp_devs[i].name
                                                    : "Standard Keyboard",
                               active ? "🟢 <b>ACTIVE</b>" : "⚪ <i>MUTED</i>");
  }

  if (offset + 128 < max_len) {
    snprintf(buffer + offset, max_len - offset,
             "\n🎯 <b>Current Target:</b> <code>%s</code>\n"
             "💡 <i>Select device with <code>/keyboard_select "
             "&lt;id|all&gt;</code></i>",
             selected_target);
  }

  pthread_mutex_unlock(&devices_lock);
  return 1;
}

int keyboard_select_device(const char *target) {
  pthread_mutex_lock(&devices_lock);
  if (!target || !*target || strcmp(target, "all") == 0 ||
      strcmp(target, "*") == 0) {
    selected_index = -1;
    snprintf(selected_target, sizeof(selected_target), "all");
  } else {
    int is_num = 1;
    for (const char *p = target; *p; p++) {
      if (!isdigit((unsigned char)*p)) {
        is_num = 0;
        break;
      }
    }
    if (is_num) {
      selected_index = atoi(target);
      snprintf(selected_target, sizeof(selected_target), "%d", selected_index);
    } else {
      selected_index = -2;
      snprintf(selected_target, sizeof(selected_target), "%s", target);
    }
  }
  pthread_mutex_unlock(&devices_lock);
  return 1;
}

void keyboard_get_selected_target(char *buffer, size_t max_len) {
  if (!buffer || max_len == 0)
    return;
  pthread_mutex_lock(&devices_lock);
  snprintf(buffer, max_len, "%s", selected_target);
  pthread_mutex_unlock(&devices_lock);
}

int keyboard_get_device_count(void) {
  pthread_mutex_lock(&devices_lock);
  int count = active_device_count;
  pthread_mutex_unlock(&devices_lock);
  return count;
}
