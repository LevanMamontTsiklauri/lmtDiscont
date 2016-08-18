#ifndef NLOG_H
#define NLOG_H

#include <stdio.h>

#define NLOG_MAX_MESSAGE 1024
#define NLOG_MSG_SYSTEM_FAILURE "NLOG SYSTEM FAILURE!"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLOG_LogType
    {
    NLOG_LogType_Info,
    NLOG_LogType_Warn,
    NLOG_LogType_Error,
    NLOG_LogType_Panic,
    NLOG_LogType_Alarm
    } NLOG_LogType;

typedef struct NLOG_LogCenter
    {
    int fdes; // UDP socket
    FILE *outputStream;
    char msgBuf[NLOG_MAX_MESSAGE];
    } NLOG_LogCenter;

extern NLOG_LogCenter g_logCenter;
extern int g_nlogEnabled;

void NLOG_DoLog(NLOG_LogCenter *logCenter,
                NLOG_LogType logType,
                const char *sourceFilename,
                int sourceLineNumber,
                const char *functionName,
                int entryId);

void NLOG_FormatMessage(const char *format, ...);

void NLOG_SetEnabled(int enabled);

#ifdef __cplusplus
}
#endif

#define NLOG_INFO(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Info, __FILE__, __LINE__, NULL, 0); }
#define NLOG_WARN(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Warn, __FILE__, __LINE__, NULL, 0); }
#define NLOG_ERROR(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Error, __FILE__, __LINE__, NULL, 0); }
#define NLOG_PANIC(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Panic, __FILE__, __LINE__, NULL, 0); }
#define NLOG_ALARM(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Alarm, __FILE__, __LINE__, NULL, 0); }

#define NLOG_F_INFO(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Info, __FILE__, __LINE__, __FUNCTION__, 0); }
#define NLOG_F_WARN(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Warn, __FILE__, __LINE__, __FUNCTION__, 0); }
#define NLOG_F_ERROR(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Error, __FILE__, __LINE__, __FUNCTION__, 0); }
#define NLOG_F_PANIC(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Panic, __FILE__, __LINE__, __FUNCTION__, 0); }
#define NLOG_F_ALARM(args...) { NLOG_FormatMessage(args); NLOG_DoLog(&g_logCenter, NLOG_LogType_Alarm, __FILE__, __LINE__, __FUNCTION__, 0); }


#define NLOG_OFF NLOG_SetEnabled(0);
#define NLOG_ON NLOG_SetEnabled(1);


#endif
