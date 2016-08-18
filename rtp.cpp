#include <sys/time.h>
#include "bufdata.h"
#include "nlog.h"
#include "rtp.h"

/*
void uint32_to_bytes(u_int32_t val, u_int8_t *bytes)
    {
    bytes[0] = (val & 0xFF000000) >> 24;
    bytes[1] = (val & 0x00FF0000) >> 16;
    bytes[2] = (val & 0x0000FF00) >> 8;
    bytes[3] =  val & 0x000000FF;
    }

u_int32_t bytes_to_uint32(u_int8_t *bytes)
    {
    return
        (bytes[0] << 24) |
        (bytes[1] << 16) |
        (bytes[2] << 8)  |
         bytes[3];
    }
*/

u_int32_t RTP_Timestamp_90khz(void)
    {
    // actual time resolution is about 10 khz, due to linux clock resolution
    struct timeval tm;
    if (gettimeofday(&tm, NULL) < 0)
        NLOG_PANIC("RTP_Timestamp_90khz: gettimeofday failed");
    u_int32_t timestamp = ((long long)tm.tv_sec * 1000000 + tm.tv_usec) / 1000;
    return timestamp;
    }

void RTP_Header_ComposeSimple(RTP_Header *rtpHeader, unsigned int payloadType,
        unsigned int sequenceNumber, u_int32_t timestamp, u_int32_t ssrc)
    {
    rtpHeader->version = RTP_VERSION;
    rtpHeader->p = 0;
    rtpHeader->x = 0;
    rtpHeader->cc = 0;
    rtpHeader->m = 0;
    rtpHeader->pt = payloadType;
    rtpHeader->seq = sequenceNumber;
    rtpHeader->ts = timestamp;
    rtpHeader->ssrc = ssrc;
    }

int RTP_Header_Render(const RTP_Header *rtpHeader, u_int8_t *buf, int len)
    {
    int i, n;
    int reqSize = 12 + (rtpHeader->cc * 4);
    if (reqSize >= len)
        NLOG_PANIC("RTP_Header_Render: buffer too small to render RTP header");
    buf[0] = (rtpHeader->version << 6) |
             (rtpHeader->p << 5) |
             (rtpHeader->x << 4) |
             rtpHeader->cc;
    buf[1] = (rtpHeader->m << 7) | rtpHeader->pt;
    buf[2] = (rtpHeader->seq & 0xFF00) >> 8;
    buf[3] = rtpHeader->seq & 0x00FF;
    uint32_to_bytes(rtpHeader->ts, &buf[4]);
    uint32_to_bytes(rtpHeader->ssrc, &buf[8]);
    for (i = 0, n = 12; i < rtpHeader->cc; i++, n += 4)
        uint32_to_bytes(rtpHeader->csrc[i], &buf[n]);
    return reqSize;
    }

int RTP_Header_Parse(RTP_Header *rtpHeader, const u_int8_t *buf, int len)
    {
    if (len < 12)
        return -1;
    
    rtpHeader->version = (buf[0] & 0xC0) >> 6;
    rtpHeader->p = (buf[0] & 0x20) >> 5;
    rtpHeader->x = (buf[0] & 0x10) >> 4;
    rtpHeader->cc = buf[0] & 0x0F;
    rtpHeader->m = (buf[1] & 0x80) >> 7;
    rtpHeader->pt = buf[1] & 0x7F;
    rtpHeader->seq = (buf[2] << 8) | buf[3];
    rtpHeader->ts = bytes_to_uint32(&buf[4]);
    rtpHeader->ssrc = bytes_to_uint32(&buf[8]);

    if (rtpHeader->cc > 0)
        {
        if (len < 12 + rtpHeader->cc * 4)
            return -1;
    
        int i;
        for (i = 0; i < rtpHeader->cc; i++)
            rtpHeader->csrc[i] = bytes_to_uint32(&buf[12 + i * 4]);

        return 12 + rtpHeader->cc * 4;
        }
    else
        return 12;
    }

int RTP_SeqMinDistance(u_int16_t from, u_int16_t to,
        int *leftdist, int *rightdist)
    {
    if (from > to)
        {
        *leftdist = from - to;
        *rightdist = 0xFFFF + 1 - *leftdist;
        }
    else if (from < to)
        {
        *rightdist = to - from;
        *leftdist = 0xFFFF + 1 - *rightdist;
        }
    else
        {
        *rightdist = 0;
        *leftdist = 0xFFFF + 1;
        }
    return *leftdist < *rightdist ? *leftdist : *rightdist;
    }


