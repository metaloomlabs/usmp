#pragma once
#include "usmp.h"

int usmp_send(usmp_t* ctx, const uint8_t* data, uint16_t len);
int usmp_recv(usmp_t* ctx, uint8_t* out, uint16_t max_len);
