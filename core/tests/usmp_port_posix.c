#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usmp_port.h"

#ifdef _WIN32
/* windows.h must precede wincrypt.h: wincrypt.h uses base Win32 types
   (BOOL, DWORD, WINAPI, ...) that windows.h defines (S8). */
#include <windows.h>
#include <wincrypt.h>

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

static uint32_t g_wdt_feed_count = 0;
void usmp_port_wdt_feed(void) {
  g_wdt_feed_count++;
}

uint32_t usmp_test_get_wdt_feed_count(void) {
  return g_wdt_feed_count;
}

void usmp_test_reset_wdt_feed_count(void) {
  g_wdt_feed_count = 0;
}

#ifdef _WIN32
static volatile LONG g_mutex_lock_count = 0;
static volatile LONG g_mutex_unlock_count = 0;
#define ATOMIC_INC(var) InterlockedIncrement(&(var))

int usmp_port_mutex_create(usmp_mutex_t* mutex) {
  if (!mutex) return -1;
  CRITICAL_SECTION* cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
  if (!cs) return -1;
  InitializeCriticalSection(cs);
  *mutex = (usmp_mutex_t)cs;
  return 0;
}

int usmp_port_mutex_lock(usmp_mutex_t mutex) {
  if (!mutex) return 0;
  ATOMIC_INC(g_mutex_lock_count);
  EnterCriticalSection((CRITICAL_SECTION*)mutex);
  return 0;
}

int usmp_port_mutex_unlock(usmp_mutex_t mutex) {
  if (!mutex) return 0;
  ATOMIC_INC(g_mutex_unlock_count);
  LeaveCriticalSection((CRITICAL_SECTION*)mutex);
  return 0;
}

void usmp_port_mutex_destroy(usmp_mutex_t mutex) {
  if (!mutex) return;
  DeleteCriticalSection((CRITICAL_SECTION*)mutex);
  free(mutex);
}
#else
#include <pthread.h>
static volatile long g_mutex_lock_count = 0;
static volatile long g_mutex_unlock_count = 0;
#define ATOMIC_INC(var) __sync_fetch_and_add(&(var), 1)

int usmp_port_mutex_create(usmp_mutex_t* mutex) {
  if (!mutex) return -1;
  pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
  if (!m) return -1;
  if (pthread_mutex_init(m, NULL) != 0) {
    free(m);
    return -1;
  }
  *mutex = (usmp_mutex_t)m;
  return 0;
}

int usmp_port_mutex_lock(usmp_mutex_t mutex) {
  if (!mutex) return 0;
  ATOMIC_INC(g_mutex_lock_count);
  return pthread_mutex_lock((pthread_mutex_t*)mutex) == 0 ? 0 : -1;
}

int usmp_port_mutex_unlock(usmp_mutex_t mutex) {
  if (!mutex) return 0;
  ATOMIC_INC(g_mutex_unlock_count);
  return pthread_mutex_unlock((pthread_mutex_t*)mutex) == 0 ? 0 : -1;
}

void usmp_port_mutex_destroy(usmp_mutex_t mutex) {
  if (!mutex) return;
  pthread_mutex_destroy((pthread_mutex_t*)mutex);
  free(mutex);
}
#endif

uint32_t usmp_test_get_mutex_lock_count(void) { return (uint32_t)g_mutex_lock_count; }
uint32_t usmp_test_get_mutex_unlock_count(void) { return (uint32_t)g_mutex_unlock_count; }
void usmp_test_reset_mutex_counts(void) {
  g_mutex_lock_count = 0;
  g_mutex_unlock_count = 0;
}

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
    snprintf(g_last_formatted_log, sizeof(g_last_formatted_log), "[%s]: %s", lower_tag, msg);
    printf("%s\n", g_last_formatted_log);
  } else {
    snprintf(g_last_formatted_log, sizeof(g_last_formatted_log), "[%c][%s] %s", level, tag, msg);
    printf("%s\n", g_last_formatted_log);
  }
}
