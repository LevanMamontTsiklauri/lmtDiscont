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
#include <stdbool.h>
#include <libconfig.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>

#define BUF_SIZE (32 * 1024)
#define DEFAULT_CONFIG_FILENAME "discont.cfg"
#define READ_TIMEOUT 2
#define MAX_CSRC_COUNT 15
#define DEFAULT_LOG_FILE "discont.err"
#define MAX_APIDS 10
#define VERSION 1.0

static char g_buffer[512];

typedef struct lmtPidInfo
    {
    uint16_t pid;
    int cc;
    const char* pFormat;
    } lmtPidInfo;

typedef struct RTP_Header
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
    } RTP_Header;

typedef struct RTP_Packet
    {
    RTP_Header header;
    const u_int8_t *rawData;
    int rawDataSize;
    const u_int8_t payload;
    int payloadSize;
    } RTP_Packet;

typedef struct lmtChanInfo
    {   
        int sId;
        uint16_t sSid;
        uint16_t sPmt;
        int sPmtVer;
        lmtPidInfo sApid[MAX_APIDS];
        lmtPidInfo sVpid;
        uint16_t sPcr;
        const char* sStreamType;
        bool pmtParsed;
        bool sPatParsed;
        int aPidCnt;
        double sBitrate;
        int cCerrors;
        int cCArray[60];
    } lmtChanInfo;

typedef struct thread_params
    {
    int id;
    const char* mcastAddr;
    unsigned short int port;
    const char* ifAddr;
    const char* outFolder;
    lmtChanInfo chanInfo;
    bool firstRtp;
    bool isStream;
    bool logToFile;
    } thread_params;

void logWithTime(const char* tolog, ...){
    time_t date;
    time(&date);
    struct tm *ltime = localtime(&date);
    va_list args;
    va_start (args, tolog);
    vsprintf (g_buffer, tolog, args);
    va_end (args);
    printf("%4d-%02d-%2d %0.2d:%0.2d:%0.2d: %s\n", ltime->tm_year + 1900, ltime->tm_mon + 1, ltime->tm_mday, ltime->tm_hour, ltime->tm_min, ltime->tm_sec,  g_buffer);
}

void greating()
{
    logWithTime("===============================");
    logWithTime("lmtDiscont Version %0.1f", VERSION);
    // logWithTime(("lmtDiscont Version "));
    logWithTime("MPEG-TS channel monitoring tool");
    logWithTime("===============================");
}

long long getUsecs(void) 
{
    struct timeval tm;
    if (gettimeofday(&tm, NULL) < 0)
    {
        logWithTime("Usec: gettimeofday failed");
        return -1;
    }
    long long timestamp = ((long long) tm.tv_sec) * 1000000 + tm.tv_usec;
    return timestamp;
}

char *trimStr(char* str)
{
    char* end;

    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    *(end+1) = 0;

    return str;
}

static inline uint16_t lmtTs_get_pid(const uint8_t *p_ts)
{
    return ((p_ts[1] & 0x1f) << 8) | p_ts[2];
}

static inline int lmt_get_tscc(const uint8_t* tsBuf)
{
    return tsBuf[3] & 0x0f;
}

static inline uint32_t bytes_to_uint32(const uint8_t *bytes)
{
    return
        (bytes[0] << 24) |
        (bytes[1] << 16) |
        (bytes[2] << 8)  |
         bytes[3];
}

int RTP_Header_Parse(RTP_Header *rtpHeader, const uint8_t *buf, int len)
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

void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <multicast_ip> <port> [<interval>] [<output filename>]\n", progname);
    exit(EXIT_FAILURE);
}

uint16_t lmt_get_program(uint8_t* p_ts)
{
    if (((p_ts[3] >> 4) & 3) > 1)
    {
        int adaptation_len = p_ts[4];
        return ((p_ts[14 + adaptation_len] & 0x1F) << 8) | p_ts[15 + adaptation_len];
    }
    return (p_ts[13] << 8) | p_ts[14];
}

uint16_t lmt_get_pmt(uint8_t *p_ts)
{
    if (((p_ts[3] >> 4) & 3) > 1)
    {
        int adaptation_len = p_ts[4];
        return ((p_ts[16 + adaptation_len] & 0x1F) << 8) | p_ts[17 + adaptation_len];
    }
    return ((p_ts[15] & 0x1F) << 8) | p_ts[16];
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

static inline const char *lmt_get_streamtype_txt(uint8_t i_stream_type) 
{
    /* ISO/IEC 13818-1 | Table 2-36 - Stream type assignments */
    if (i_stream_type == 0)
        return "Reserved";
    switch (i_stream_type) {
        case 0x01: return "11172-2 video (MPEG-1)";
        case 0x02: return "13818-2 video (MPEG-2)";
        case 0x03: return "11172-3 audio (MPEG-1)";
        case 0x04: return "13818-3 audio (MPEG-2)";
        case 0x05: return "13818-1 private sections";
        case 0x06: return "13818-1 PES private data";
        case 0x07: return "13522 MHEG";
        case 0x08: return "H.222.0/13818-1 Annex A - DSM CC";
        case 0x09: return "H.222.1";
        case 0x0A: return "13818-6 type A";
        case 0x0B: return "13818-6 type B";
        case 0x0C: return "13818-6 type C";
        case 0x0D: return "13818-6 type D";
        case 0x0E: return "H.222.0/13818-1 auxiliary";
        case 0x0F: return "13818-7 Audio with ADTS transport syntax";
        case 0x10: return "14496-2 Visual (MPEG-4 part 2 video)";
        case 0x11: return "14496-3 Audio with LATM transport syntax (14496-3/AMD 1)";
        case 0x12: return "14496-1 SL-packetized or FlexMux stream in PES packets";
        case 0x13: return "14496-1 SL-packetized or FlexMux stream in 14496 sections";
        case 0x14: return "ISO/IEC 13818-6 Synchronized Download Protocol";
        case 0x15: return "Metadata in PES packets";
        case 0x16: return "Metadata in metadata_sections";
        case 0x17: return "Metadata in 13818-6 Data Carousel";
        case 0x18: return "Metadata in 13818-6 Object Carousel";
        case 0x19: return "Metadata in 13818-6 Synchronized Download Protocol";
        case 0x1A: return "13818-11 MPEG-2 IPMP stream";
        case 0x1B: return "H.264/14496-10 video (MPEG-4/AVC)";
        case 0x24: return "H.265 and ISO/IEC 23008-2 UHD/4K Video";
        case 0x42: return "AVS Video";
        case 0x7F: return "IPMP stream";
        case 0x81: return "ATSC A/52";
        case 0x86: return "SCTE 35 Splice Information Table";
        default  : return "Unknown";
    }
}

static inline int lmt_get_streamtype(uint8_t i_stream_type) 
{

    switch (i_stream_type) {
        case 0x01: 
        case 0x02: 
        case 0x08: 
        case 0x09: 
        case 0x10: 
        case 0x1B:
        case 0x24: 
        case 0x42: 
            return 0; // Video

        case 0x03: 
        case 0x04: 
        case 0x0f: 
        case 0x11: 
            return 1; // Audio

        default  :
            return -1; // Other
    }
}

static inline int lmt_get_adaptationLen(uint8_t* p_ts)
{
    if (((p_ts[3] >> 4) & 3) > 1)
    {
        return p_ts[4];
    }
    return 0;
}

int getOneMinuteCC(lmtChanInfo* chanInfo)
{
    int sum = 0;
    for (int i = 0; i < 60; ++i)
    {
        sum += chanInfo->cCArray[i];
    }
    return sum;
}

int openDgramSocket(const char* mcastAddr, unsigned short int port, const char* ifAddr, const int id)
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
            // fprintf(stderr, "[ERROR] Channel: %d socket\n", id);
            logWithTime("[ERROR] Channel: %d socket", id);
            fclose(f);
            pthread_exit(NULL);
        }
    if (setsockopt(fdes, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        {
            // fprintf("[ERROR] Channel: %d setsockopt (SO_REUSEADDR)\n", id);
            logWithTime("[ERROR] Channel: %d setsockopt (SO_REUSEADDR)", id);
            fclose(f);
            pthread_exit(NULL);
        }
    if (setsockopt(fdes, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0)
        {
            // fprintf(f, "[ERROR] Channel: %d setsockopt (SO_REUSEPORT)\n", id);
            logWithTime("[ERROR] Channel: %d setsockopt (SO_REUSEPORT)", id);
            fclose(f);
            pthread_exit(NULL);
        }
    
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec = READ_TIMEOUT;
    if (setsockopt(fdes, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            // fprintf(f, "[ERROR] Channel: %d setsockopt (SO_RCVTIMEO)\n", id);
            logWithTime("[ERROR] Channel: %d setsockopt (SO_RCVTIMEO)", id);
            fclose(f);
            pthread_exit(NULL);
        }

    if (bind(fdes, (struct sockaddr *)&(sin), sizeof(sin)) < 0)
        {
            // fprintf(f, "[ERROR] Channel: %d bind error\n", id);
            logWithTime("[ERROR] Channel: %d bind error", id);
            fclose(f);
            pthread_exit(NULL);
        }

    mreq.imr_multiaddr.s_addr=inet_addr(mcastAddr);
    mreq.imr_interface.s_addr=inet_addr(ifAddr);
    if (setsockopt(fdes, IPPROTO_IP,IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        {
            // fprintf(f, "[ERROR] Channel: %d setsockopt (IP_ADD_MEMBERSHIP)\n", id);
            logWithTime("[ERROR] Channel: %d setsockopt (IP_ADD_MEMBERSHIP)", id);
            fclose(f);
            pthread_exit(NULL);
        }
    
    fclose(f);
    return fdes;
}

void *lmtParseStream(void* arg)
{
    struct thread_params *inArg = (struct thread_params*)arg;

    uint16_t tmpPid;
    int tmpCc;

    int id = inArg->id;
    const char *ip = inArg->mcastAddr;
    unsigned short int port = inArg->port;
    const char *ifAddr = inArg->ifAddr;
    // char *filename = (char*)inArg->outFolder;
    
    int size = strlen(inArg->outFolder);
    char tmpfilename[size+4];
    memset(tmpfilename, 0, sizeof(tmpfilename));
    strncpy(tmpfilename, inArg->outFolder, size);

    char *filename = &tmpfilename[0];
    char *slash = "/";
    FILE *fp;
    /*Making sure the folder address ends in /(slash) and no whitespaces*/
    filename = trimStr(filename);

    if (filename[strlen(filename) - 1] != '/')
    {
        strcat(filename, slash);
    }
    sprintf(filename, "%s%d.out", filename, id);
    /////////////////////////////////////////////////////////////////////
    long long lasTime, timeDiff, bigTime;

    char buf[BUF_SIZE];
    bool saidstreamtype = false;

    int n, tOffset;
    unsigned short pCounter;
    struct RTP_Packet *pPack;
    int sok = openDgramSocket(ip, port, ifAddr, id);

    // printf("Starting monitoring of channel: %d Address: %s Port: %hu\n", id, ip, port);
    int packetCount = 0, ccIndex = 0;
    lasTime = getUsecs();
    bigTime = getUsecs();
    while(1)
    {

        memset(buf, 0, BUF_SIZE);
        n = recv(sok, buf, BUF_SIZE, 0);

        if (n <= 0 && errno != EAGAIN)
            break;

        if (n <= 0){
            // printf("Channel: %d Error: Receave timeout for 2 seconds\n", id);
            memset(&inArg->chanInfo, 0, sizeof(lmtChanInfo));
            inArg->id = id;
            saidstreamtype = false;
            inArg->isStream = false;
            continue;
        }

        if ((n - 12) / 7 == 188 || n / 7 == 188) 
        {
            if ((n - 12) / 7 == 188)
            {
                tOffset = 12;
                if (saidstreamtype == false)
                {
                   inArg->chanInfo.sStreamType = "RTP";
                }
                struct RTP_Header tHeader;
                pPack = (struct RTP_Packet*)buf;
                RTP_Header_Parse(&tHeader, (u_int8_t*)pPack, 12);

                if ((pCounter + 1) % 65536 != tHeader.seq)
                {
                    logWithTime("Channel: %d RTP CC Error: expected %d got %d", id, (pCounter + 1) % 65535, tHeader.seq);
                    inArg->chanInfo.cCerrors++;
                }
                pCounter = tHeader.seq;
            }else
            {
                tOffset = 0;
                pPack = (struct RTP_Packet*)buf;
                if (saidstreamtype == false)
                 {
                    inArg->chanInfo.sStreamType = "UDP";
                 }
            }
            
            saidstreamtype = true;
            uint16_t tPid;

            if (inArg->chanInfo.sPmt == 0)
            {
            
                for (int i = 0; i < 7; ++i)
                {
                    tPid = lmtTs_get_pid((uint8_t*)pPack + tOffset);
                    if (tPid == 0)
                    {
                        inArg->chanInfo.sSid = lmt_get_program((uint8_t*)pPack + tOffset);
                        inArg->chanInfo.sPmt = lmt_get_pmt((uint8_t*)pPack + tOffset);
                        inArg->chanInfo.sPatParsed = true;
                    }
                    tOffset += 188;
                }
            }else if (inArg->chanInfo.pmtParsed == 0)
            {
                for (int i = 0; i < 7; ++i)
                {
                    tPid = lmtTs_get_pid((uint8_t*)pPack + tOffset);
                    if (tPid == inArg->chanInfo.sPmt)
                    {
                        uint8_t* p_pmt;
                        int af = lmt_get_adaptationLen((uint8_t*)pPack + tOffset); // get adaptation field
                        if (af > 0)
                        {
                            p_pmt = (uint8_t*)pPack + tOffset + 5 + af;
                        }else p_pmt = (uint8_t*)pPack + tOffset + 4; 

                        int data_length = ((p_pmt[2] & 0xF) << 8)| (p_pmt[3] - 4);
                        int descriptor_len = ((p_pmt[11] & 0xF) << 8)| p_pmt[12];
                        int j = 13 + descriptor_len;

                        while(j < data_length)
                        {
                            int secLen = ((p_pmt[j + 3] & 0xf) << 8)| p_pmt[j + 4];
                            switch (lmt_get_streamtype(p_pmt[j])){
                                case 0:
                                    inArg->chanInfo.sVpid.pid = ((p_pmt[j + 1] & 0x1f) << 8)| p_pmt[j + 2];
                                    inArg->chanInfo.sVpid.pFormat = lmt_get_streamtype_txt(p_pmt[j]);
                                    j += secLen + 5;
                                    break;
                                case 1:
                                    inArg->chanInfo.sApid[inArg->chanInfo.aPidCnt].pid = ((p_pmt[j + 1] & 0x1f) << 8)| p_pmt[j + 2];
                                    inArg->chanInfo.sApid[inArg->chanInfo.aPidCnt].pFormat = lmt_get_streamtype_txt(p_pmt[j]);
                                    inArg->chanInfo.aPidCnt = inArg->chanInfo.aPidCnt + 1;
                                    j += secLen + 5;
                                    break;
                                default: 
                                    j += secLen + 5;
                                    break;
                            }
                        }
                        inArg->chanInfo.pmtParsed = 1;
                    }
                    tOffset += 188;
                }
            }else
            { 
                for (int i = 0; i < 7; ++i)
                {
                    tmpPid = lmtTs_get_pid((uint8_t*)pPack + tOffset);
                    tmpCc = lmt_get_tscc((uint8_t*)pPack + tOffset);

                    if (inArg->chanInfo.sVpid.pid == tmpPid)
                    {
                        if ((inArg->chanInfo.sVpid.cc + 1) % 16 != tmpCc)
                        {
                            logWithTime("%d CC Error: vPID: %d expected %d got %d", id, tmpPid, (inArg->chanInfo.sVpid.cc + 1) % 15, tmpCc);
                            inArg->chanInfo.cCerrors++;                            
                        }
                        inArg->chanInfo.sVpid.cc = tmpCc;
                    }else{
                        for (int i = 0; i < inArg->chanInfo.aPidCnt; i++)
                        {
                            if (inArg->chanInfo.sApid[i].pid == tmpPid)
                            {
                                if ((inArg->chanInfo.sApid[i].cc + 1) % 16 != tmpCc)
                                {
                                    logWithTime("%d CC Error: aPID: %d expected %d got %d", id, tmpPid, (inArg->chanInfo.sApid[i].cc + 1) % 15, tmpCc);
                                    inArg->chanInfo.cCerrors++;
                                }
                                inArg->chanInfo.sApid[i].cc = tmpCc;
                            }
                        }
                    }
                    tOffset += 188;
                }
            }

            if ((getUsecs() - lasTime) > 1000000)
            {
                timeDiff = getUsecs() - lasTime;
                long long bits = strcmp(inArg->chanInfo.sStreamType, "UDP") ? (packetCount * 1316 * 8) : (packetCount * 1328 * 8);
                long long bitTimeRatio = bits * 1000000 / timeDiff;
                inArg->chanInfo.sBitrate = (double)bitTimeRatio / 1000000;
                inArg->isStream = true;
                saidstreamtype = false;
                packetCount = 0;
                lasTime = getUsecs();
                inArg->chanInfo.cCArray[ccIndex] = inArg->chanInfo.cCerrors;
                inArg->chanInfo.cCerrors = 0;
                ccIndex = (ccIndex < 60) ? ccIndex + 1 : 0;
            }
            
            if((getUsecs() - bigTime) > 10000000 && inArg->logToFile)
            {
                fp = fopen((const char*)filename, "w");
                if (fp <= 0)
                {
                    logWithTime("Channel: %d Error opening file: %s: %s", id, filename, strerror(errno));
                    break;
                }
                fprintf(fp, "%d", getOneMinuteCC(&inArg->chanInfo));
                fclose(fp);
                bigTime = getUsecs();
            }

            packetCount++;
        }else 
        {
            logWithTime("Channel: %d Not an RTP or UDP TS stream, packet size is: %d", id, n);
            inArg->chanInfo.sStreamType = "Error";
            usleep(2000000);
            continue;
        }
    }
    close(sok);
    return 0;
  }



int main(int argc, char *argv[])
{
    greating();
    const char* cfg_file = DEFAULT_CONFIG_FILENAME;
    const char* outputFolder = "./";
    int mLogTofile;
    int chanCount, parsedChanCount = 0;
    int thrd_created;

    /* Config parse*/
    config_t cfg;
    config_setting_t *channels;
    // config_setting_t *global;

    config_init(&cfg);

    if (!config_read_file(&cfg, cfg_file))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }

    /*Global Config*/

    config_lookup_string(&cfg, "outputFolder", &outputFolder);
    config_lookup_bool(&cfg, "logToFile", &mLogTofile);

    /*Channel Config*/

    channels = config_lookup(&cfg, "configs");
    chanCount = config_setting_length(channels);
    thread_params chanConfs[chanCount];

    for (int i = 0; i < chanCount; ++i)
    {
        memset(&chanConfs[i], 0, sizeof(thread_params));
    }
    
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
        chanConfs[i].outFolder = outputFolder;
        chanConfs[i].logToFile = mLogTofile;
        parsedChanCount += 1;
    }

    /* End of config parsing */
    #ifdef NDEBUG
        for (int i = 0; i < chanCount; ++i)
        {
            logWithTime("config %d\n chan id is: %d\n multicast IP is: %s\n port is: %d\n receave interface is: %s", i, chanConfs[i].id, chanConfs[i].mcastAddr,\
                chanConfs[i].port, chanConfs[i].ifAddr);
        }
    #endif

    logWithTime("found %d channel in config file: %s with following ID's", parsedChanCount, cfg_file);

    pthread_t lmtTrd[parsedChanCount];

    for (int i = 0; i < parsedChanCount; ++i)
    {
        thrd_created = pthread_create(&lmtTrd[i], NULL, lmtParseStream, &chanConfs[i]);
        if (thrd_created)
        {
            logWithTime("ERROR Creating thread");
            exit(-1);
        }
        logWithTime("%d", chanConfs[i].id);
    }


    logWithTime("===============================");

    int clearCounters = 0;

    usleep(2000000);
    while(1)
    {
        for (int i = 0; i < chanCount; ++i)
        {
            // fprintf(stdout, "id: %d, hasData: %d, PAT: %d, SID: %hu, pmt: %hu, vPid: %hu, vFormat: %s, AudioCnt: %d, aPid: %hu, aFormat: %s, streamType: %s, Bitrate: %.2f, Errors: %d\n", \
            //         chanConfs[i].id, chanConfs[i].isStream, chanConfs[i].chanInfo.sPatParsed, chanConfs[i].chanInfo.sSid ,chanConfs[i].chanInfo.sPmt, chanConfs[i].chanInfo.sVpid.pid, chanConfs[i].chanInfo.sVpid.pFormat, \
            //         chanConfs[i].chanInfo.aPidCnt , chanConfs[i].chanInfo.sApid[0].pid, chanConfs[i].chanInfo.sApid[0].pFormat, chanConfs[i].chanInfo.sStreamType, chanConfs[i].chanInfo.sBitrate, getOneMinuteCC(&chanConfs[i].chanInfo));
            logWithTime("id: %d, hasData: %d, PAT: %d, SID: %hu, pmt: %hu, vPid: %hu, vFormat: %s, AudioCnt: %d, aPid: %hu, aFormat: %s, streamType: %s, Bitrate: %.2f, Errors: %d", \
                    chanConfs[i].id, chanConfs[i].isStream, chanConfs[i].chanInfo.sPatParsed, chanConfs[i].chanInfo.sSid ,chanConfs[i].chanInfo.sPmt, chanConfs[i].chanInfo.sVpid.pid, chanConfs[i].chanInfo.sVpid.pFormat, \
                    chanConfs[i].chanInfo.aPidCnt , chanConfs[i].chanInfo.sApid[0].pid, chanConfs[i].chanInfo.sApid[0].pFormat, chanConfs[i].chanInfo.sStreamType, chanConfs[i].chanInfo.sBitrate, getOneMinuteCC(&chanConfs[i].chanInfo));
            // for (int j = 0; j < 60; ++j)
            // {
            //     printf("[%d]", chanConfs[i].chanInfo.cCArray[j]);
            // }            
        }



        printf("\n");
        usleep(2000000);
        clearCounters++;
        if (clearCounters == 30)
        {
            for (int i = 0; i < chanCount; ++i)
            {
                chanConfs[i].chanInfo.cCerrors = 0;
            }
            clearCounters = 0;
        }
    }
    config_destroy(&cfg);
    return EXIT_SUCCESS;
}






