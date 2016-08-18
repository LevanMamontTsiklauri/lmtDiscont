#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <bitstream/mpeg/ts.h>
#include <libconfig.h>
// #include "rtp.h"

#define BUF_SIZE (32 * 1024)
#define DEFAULT_CONFIG_FILENAME "discont.cfg"
#define READ_TIMEOUT 2
#define MAX_CSRC_COUNT 15
#define DEFAULT_LOG_FILE "discont.err"

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

u_int32_t bytes_to_uint32(const u_int8_t *bytes)
{
return
    (bytes[0] << 24) |
    (bytes[1] << 16) |
    (bytes[2] << 8)  |
     bytes[3];
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

int threadRetVal;

struct thread_params
{
    int id;
    const char* mcastAddr;
    unsigned short int port;
    const char* ifAddr;
};

void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <multicast_ip> <port> [<interval>] [<output filename>]\n", progname);
    exit(EXIT_FAILURE);
}

uint16_t lmt_bytes_to_uint16(const uint8_t* buf)
{
    return (buf[0] << 8) | buf[1];
}

int lmt_get_tscc(uint8_t* buf)
{
    return buf[3] & 0xF;
}

uint16_t lmt_get_program(uint8_t* buf)
{
    return (buf[13] << 8) | buf[14];
}

uint16_t lmt_pid_from_bytes(uint8_t byteOne, uint8_t byteTwo)
{
    return ((byteOne & 0x1F) << 8) | byteTwo;
}

long long usec_time()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
        {
        perror("gettimeofday");
        exit(EXIT_FAILURE);
        }
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int openDgramSocket(const char* mcastAddr, unsigned short int port, const char* ifAddr, int id)
{
    int fdes;
    unsigned int yes = 1;
    struct ip_mreq mreq;
    struct sockaddr_in sin;
    FILE *f = fopen(DEFAULT_LOG_FILE, "a");
    setbuf(f, NULL);

    sin.sin_family=AF_INET;
    sin.sin_addr.s_addr=inet_addr(mcastAddr);
    sin.sin_port=htons(port);

    if ((fdes = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            fprintf(stderr, "[ERROR] Channel: %d socket\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }
    if (setsockopt(fdes, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        {
            fprintf(f, "[ERROR] Channel: %d setsockopt (SO_REUSEADDR)\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }
    if (setsockopt(fdes, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0)
        {
            fprintf(f, "[ERROR] Channel: %d setsockopt (SO_REUSEPORT)\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }
    
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec = READ_TIMEOUT;
    if (setsockopt(fdes, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            fprintf(f, "[ERROR] Channel: %d setsockopt (SO_RCVTIMEO)\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }

    if (bind(fdes, (struct sockaddr *)&(sin), sizeof(sin)) < 0)
        {
            fprintf(f, "[ERROR] Channel: %d bind error\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }

    mreq.imr_multiaddr.s_addr=inet_addr(mcastAddr);
    mreq.imr_interface.s_addr=inet_addr(ifAddr);
    if (setsockopt(fdes, IPPROTO_IP,IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        {
            fprintf(f, "[ERROR] Channel: %d setsockopt (IP_ADD_MEMBERSHIP)\n", id);
            fclose(f);
            pthread_exit(&threadRetVal);
        }
    
    fclose(f);
    return fdes;
}


void *lmtParseStream(void* arg)
{
    struct thread_params *inArg = (struct thread_params*)arg;

    int id = inArg->id;
    const char *ip = inArg->mcastAddr;
    unsigned short int port = inArg->port;
    const char *ifAddr = inArg->ifAddr;

    char buf[BUF_SIZE];
    uint16_t pmtPid = 0, vPid = 0, aPid = 0, pPid = 0;
    // setbuf(stdout, NULL);

    int n;
    unsigned short pCounter;
    struct RTP_Packet *pPack;
    int sok = openDgramSocket(ip, port, ifAddr, id);

    printf("Starting monitoring of channel: %d Address: %s Port: %hu\n", id, ip, port);
    int packetCount = 0;
    while(1)
    {
        memset(buf, 0, BUF_SIZE);
        n = recv(sok, buf, BUF_SIZE, 0);
        // printf("%d\n", sok);
        if (n <= 0 && errno != EAGAIN)
            break;

        if (n <= 0){
            printf("Channel: %d Error: Receave timeout for 2 seconds\n", id);
            continue;
        }

        if ((n - 12) / 7 == 188)
        {
            struct RTP_Header tHeader;
            pPack = (struct RTP_Packet*)buf;
            RTP_Header_Parse(&tHeader, (u_int8_t*)pPack, 12);
            
            if (pCounter != tHeader.seq - 1)
            {
                printf("Channel: %d RTP CC Error: expected %d got %d\n", id, pCounter + 1, tHeader.seq);
            }
            pCounter = tHeader.seq;
            int tOffset = 12;
            uint16_t tPid;
            uint8_t tsBuf[188];

            if (pmtPid == 0)
            {
            
                for (int i = 0; i < 7; ++i)
                {
                    memset(tsBuf, 0, 188);
                    tPid = ts_get_pid((u_int8_t*)pPack + tOffset);
                    if (tPid == 0)
                    {
                        memcpy(&tsBuf, (void*)pPack + tOffset, 188);
                        pPid = lmt_get_program((uint8_t*)&tsBuf);
                        pmtPid = lmt_pid_from_bytes(tsBuf[15], tsBuf[16]);
                        printf("Channel: %d Program number is: %hu pmt pid is: %hu\n", id, pPid, pmtPid);
                    }
                    tOffset += 188;
                }
            }else if (vPid == 0 || aPid == 0)
            {
                for (int i = 0; i < 7; ++i)
                {
                    tPid = ts_get_pid((u_int8_t*)pPack + tOffset);
                    if (tPid == pmtPid)
                    {
                        printf("Channel: %d got PMT\n", id);
                    }
                    tOffset += 188;
                }
            }
        }else if (n /7 == 188)
        {
            printf("Channel: %d  Stream is UDP count is: %d\n", id, packetCount);
            usleep(500000);
            packetCount += 1;
            continue;
        }else {
            printf("Channel: %d Not an RTP or UDP TS stream, size is: %d\n", id, n);
            usleep(2000000);
            continue;
        }
    }
    close(sok);
    return 0;
}


int main(int argc, char *argv[])
{
    // FILE *f;
    // f = fopen(DEFAULT_LOG_FILE, "a");
    const char* cfg_file = DEFAULT_CONFIG_FILENAME;
    // struct thread_params inArgs;
    int chanCount, parsedChanCount = 0;

    /* Config parse*/
    config_t cfg;
    config_setting_t *channels;

    config_init(&cfg);

    if (!config_read_file(&cfg, cfg_file))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }

    channels = config_lookup(&cfg, "configs");
    chanCount = config_setting_length(channels);
    printf("found %d channel in config file: %s\n", chanCount, cfg_file);
    struct thread_params chanConfs[chanCount];
    
    for (int i = 0; i < chanCount; ++i)
    {
        int id = 0, prt = 0;
        const char *mcast, *ifaddr;
        
        config_setting_t *tmpConfStor = config_setting_get_elem(channels, i);

        if(!(config_setting_lookup_int(tmpConfStor, "id", &id) &&
            config_setting_lookup_string(tmpConfStor, "mcastip", &mcast) &&
            config_setting_lookup_int(tmpConfStor, "port", &prt) &&
            config_setting_lookup_string(tmpConfStor, "interface", &ifaddr)))
            continue;
        chanConfs[i].id = id;
        chanConfs[i].mcastAddr = mcast;
        chanConfs[i].port = prt;
        chanConfs[i].ifAddr = ifaddr;
        parsedChanCount += 1;
    }

    printf("%d good channels parsed out of %d in config file\n", parsedChanCount, chanCount);
    /* End of config parsing */
#ifdef NDEBUG
    for (int i = 0; i < chanCount; ++i)
    {
        printf("config %d\n chan id is: %d\n multicast IP is: %s\n port is: %d\n receave interface is: %s\n", i, chanConfs[i].id, chanConfs[i].mcastAddr,\
            chanConfs[i].port, chanConfs[i].ifAddr);
    }
#endif
    pthread_t lmtTrd[parsedChanCount];

    for (int i = 0; i < parsedChanCount; ++i)
    {
        pthread_create(&lmtTrd[i], NULL, lmtParseStream, &chanConfs[i]);
    }
    

    
    while(1)
    {
        usleep(1000000);
    }
    config_destroy(&cfg);
    return EXIT_SUCCESS;
}
