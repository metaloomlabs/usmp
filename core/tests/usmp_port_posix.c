#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usmp_port.h"

#ifdef _WIN32
/* windows.h must precede wincrypt.h: wincrypt.h uses base Win32 types
   (BOOL, DWORD, WINAPI, ...) that windows.h defines (S8). */
#include <wincrypt.h>
#include <windows.h>

#else
#include <time.h>
#include <unistd.h>

#endif

int usmp_port_get_device_id(uint8_t* out, size_t len) {
  if (len < 6) return -1;
  // Mock MAC address: 00:1A:2B:3C:4D:5E
  out[0] = 0x00;
  out[1] = 0x1A;
  out[2] = 0x2B;
  out[3] = 0x3C;
  out[4] = 0x4D;
  out[5] = 0x5E;
  return 0;
}

#ifdef _WIN32
int usmp_port_random(uint8_t* out, size_t len) {
  HCRYPTPROV hCryptProv;
  if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    if (CryptGenRandom(hCryptProv, (DWORD)len, out)) {
      CryptReleaseContext(hCryptProv, 0);
      return 0;
    }
    CryptReleaseContext(hCryptProv, 0);
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)(rand() & 0xFF);
  }
  return 0;
}

void usmp_port_delay_ms(uint32_t ms) { Sleep(ms); }

uint32_t usmp_port_millis(void) { return GetTickCount(); }
#else
int usmp_port_random(uint8_t* out, size_t len) {
  FILE* f = fopen("/dev/urandom", "rb");
  if (!f) return -1;
  size_t read_bytes = fread(out, 1, len, f);
  fclose(f);
  return (read_bytes == len) ? 0 : -1;
}

void usmp_port_delay_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

uint32_t usmp_port_millis(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

char g_last_log_level = 0;
char g_last_log_tag[64] = {0};
char g_last_log_msg[256] = {0};
char g_last_formatted_log[512] = {0};

void usmp_port_log(char level, const char* tag, const char* msg) {
  g_last_log_level = level;
  strncpy(g_last_log_tag, tag, sizeof(g_last_log_tag) - 1);
  g_last_log_tag[sizeof(g_last_log_tag) - 1] = '\0';
  strncpy(g_last_log_msg, msg, sizeof(g_last_log_msg) - 1);
  g_last_log_msg[sizeof(g_last_log_msg) - 1] = '\0';

  if (level == 'E') {
    char lower_tag[64];
    size_t i;
    for (i = 0; i < sizeof(lower_tag) - 1 && tag[i] != '\0'; i++) {
      char c = tag[i];
      if (c >= 'A' && c <= 'Z') {
        lower_tag[i] = (char)(c + ('a' - 'A'));
      } else {
        lower_tag[i] = c;
      }
    }
    lower_tag[i] = '\0';
    snprintf(g_last_formatted_log, sizeof(g_last_formatted_log), "[usmp] [%s]: %s", lower_tag, msg);
    printf("%s\n", g_last_formatted_log);
  } else {
    snprintf(g_last_formatted_log, sizeof(g_last_formatted_log), "[%c][%s] %s", level, tag, msg);
    printf("%s\n", g_last_formatted_log);
  }
}
