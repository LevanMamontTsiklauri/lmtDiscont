#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "dbug.h"
#include "nlog.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

int g_logCenterInitialized = 0;
NLOG_LogCenter g_logCenter;
int g_nlogEnabled = 1;


void NLOG_FormatMessage(const char *format, ...)
    {
    int r;
    va_list ap;
    va_start(ap, format);
    r = vsprintf(g_logCenter.msgBuf, format, ap);
    if (r < 0)
        {
        DBUG("NLOG_FormatMessage: vsprintf failure, return code=%d", r);
        strcpy(g_logCenter.msgBuf, NLOG_MSG_SYSTEM_FAILURE);
        }
    va_end(ap);
    }


void NLOG_DoLog(NLOG_LogCenter *logCenter,
                NLOG_LogType logType,
                const char *sourceFilename,
                int sourceLineNumber,
                const char *functionName,
                int entryId)
    {
    const char *logTypeStr = NULL;
    switch (logType)
        {
        case NLOG_LogType_Info:
            if (!g_nlogEnabled)
                return;
            logTypeStr = "INFO";
            break;
        case NLOG_LogType_Warn:
            if (!g_nlogEnabled)
                return;
            logTypeStr = "WARN";
            break;
        case NLOG_LogType_Error:
            if (!g_nlogEnabled)
                return;
            logTypeStr = "ERROR";
            break;
        case NLOG_LogType_Panic:
            logTypeStr = "PANIC";
            break;
        case NLOG_LogType_Alarm:
            if (!g_nlogEnabled)
                return;
            logTypeStr = "ALARM";
            break;
        };

    if (functionName == NULL)
    {
#ifdef __ANDROID__
		__android_log_print(ANDROID_LOG_INFO, "QarvaOTTJNI", "NLOG_%s<%s:%d> %s\n", logTypeStr, sourceFilename, sourceLineNumber, logCenter->msgBuf);
#else
        fprintf(stderr, "NLOG_%s<%s:%d> %s\n", logTypeStr, sourceFilename, sourceLineNumber, logCenter->msgBuf);
#endif
    }
    else
    {
#ifdef __ANDROID__
    	__android_log_print(ANDROID_LOG_INFO, "QarvaOTTJNI", "NLOG_%s<%s:%d> %s: %s\n", logTypeStr, sourceFilename, sourceLineNumber, functionName, logCenter->msgBuf);
#else
    	fprintf(stderr, "NLOG_%s<%s:%d> %s: %s\n", logTypeStr, sourceFilename, sourceLineNumber, functionName, logCenter->msgBuf);
#endif
    }
    switch (logType)
        {
        case NLOG_LogType_Panic:
            {
            int pid = getpid();
            int exitCode = 1;
            fprintf(stderr, "NLOG starting emergency exit due to panic\n");
            fprintf(stderr, "Pid: %d\n", pid);
            fprintf(stderr, "Exit code: %d\n", exitCode);
            fprintf(stderr, "NLOG panic completed. (Good luck!)\n");
            exit(exitCode);
            break;
            }
        case NLOG_LogType_Info:
        case NLOG_LogType_Warn:
        case NLOG_LogType_Error:
        case NLOG_LogType_Alarm:
            break;
        };
    }


void NLOG_SetEnabled(int enabled)
    {
    g_nlogEnabled = enabled;
    }

