#include "bufdata.h"

void uint32_to_bytes(u_int32_t val, u_int8_t *bytes)
    {
    bytes[0] = (val & 0xFF000000) >> 24;
    bytes[1] = (val & 0x00FF0000) >> 16;
    bytes[2] = (val & 0x0000FF00) >> 8;
    bytes[3] =  val & 0x000000FF;
    }

u_int32_t bytes_to_uint32(const u_int8_t *bytes)
    {
    return
        (bytes[0] << 24) |
        (bytes[1] << 16) |
        (bytes[2] << 8)  |
         bytes[3];
    }

void uint16_to_bytes(u_int16_t val, u_int8_t *bytes)
    {
    bytes[0] = (val & 0xFF00) >> 8;
    bytes[1] = val & 0x00FF;
    }

u_int16_t bytes_to_uint16(const u_int8_t *bytes)
    {
    return
        (bytes[0] << 8) | bytes[1];
    }

