#ifndef BUFDATA_H
#define BUFDATA_H

#include <stdint.h>
#include <sys/types.h>

#ifdef WIN32
  #include "wintypes.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void uint32_to_bytes(u_int32_t val, u_int8_t *bytes);
u_int32_t bytes_to_uint32(const u_int8_t *bytes);
void uint16_to_bytes(u_int16_t val, u_int8_t *bytes);
u_int16_t bytes_to_uint16(const u_int8_t *bytes);

#ifdef __cplusplus
}
#endif

#endif
