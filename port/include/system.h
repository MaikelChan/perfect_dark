#ifndef _IN_SYSTEM_H
#define _IN_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

enum LogLevel {
  LOG_NOTE,
  LOG_WARNING,
  LOG_ERROR,
};

void sysInit(s32 argc, const char **argv);

s32 sysArgCheck(const char *arg);
const char *sysArgGetString(const char *arg);
s32 sysArgGetInt(const char *arg, s32 defval);

u64 sysGetMicroseconds(void);

void sysFatalError(const char *fmt, ...) __attribute__((noreturn));

void sysLogPrintf(s32 level, const char *fmt, ...);

void sysGetExecutablePath(char *outPath, const u32 outLen);

void *sysMemAlloc(const u32 size);
void *sysMemZeroAlloc(const u32 size);
void sysMemFree(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
