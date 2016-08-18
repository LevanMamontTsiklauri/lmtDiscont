#ifndef RTP_H
#define RTP_H

#include <sys/types.h>

#ifdef WIN32
  #include "wintypes.h"
#endif


/*
 * The type definitions below are valid for 32-bit architectures and
 * may have to be adjusted for 16- or 64-bit architectures.
 */

/*
 * Current protocol version.
 */
#define RTP_VERSION    2

#define RTP_SEQ_MOD (1<<16)
#define MAX_CSRC_COUNT 15

#define RTP_PAYLOADTYPE_MP2T 33

#define RTP_SEQ_NEXT(seq) (seq == 0xFFFF ? 0 : seq + 1)
#define RTP_SEQ_PREV(seq) (seq == 0 ? 0xFFFF : seq - 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RTP_Header RTP_Header;
typedef struct RTP_Packet RTP_Packet;

struct RTP_Header
    {
    unsigned int version:2;   /* protocol version */
    unsigned int p:1;         /* padding flag */
    unsigned int x:1;         /* header extension flag */
    unsigned int cc:4;        /* CSRC count */
    unsigned int m:1;         /* marker bit */
    unsigned int pt:7;        /* payload type */
    unsigned int seq:16;      /* sequence number */
    u_int32_t ts;               /* timestamp */
    u_int32_t ssrc;             /* synchronization source */
    u_int32_t csrc[MAX_CSRC_COUNT];     /* optional CSRC list */
    };

struct RTP_Packet
    {
    RTP_Header header;
    const u_int8_t *rawData;
    int rawDataSize;
    const u_int8_t payload;
    int payloadSize;
    };

u_int32_t RTP_Timestamp_90khz(void);
void RTP_Header_ComposeSimple(RTP_Header *rtpHeader, unsigned int payloadType,
        unsigned int sequenceNumber, u_int32_t timestamp, u_int32_t ssrc);
int RTP_Header_Render(const RTP_Header *rtpHeader, u_int8_t *buf, int len);
int RTP_Header_Parse(RTP_Header *rtpHeader, const u_int8_t *buf, int len);

int RTP_SeqMinDistance(u_int16_t from, u_int16_t to,
        int *leftdist, int *rightdist);

#ifdef __cplusplus
}
#endif

#endif

