#pragma once
#include "usmp.h"

int usmp_send(usmp_t* ctx, const uint8_t* data, uint16_t len);
int usmp_recv(usmp_t* ctx, uint8_t* out, uint16_t max_len);

/*
 * Send a best-effort graceful BYE control frame. Internal to the library
 * (used by usmp_close()); not part of the public API. Returns 0 on success,
 * negative if the session is not established or the transport write failed.
 */
int usmp_send_bye(usmp_t* ctx);
