#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"

#define LOG_FNAME "pd.log"
#define USEC_IN_SEC 1000000ULL

static u64 startTick = 0;
static char logPath[2048];

static s32 sysArgc;
static const char **sysArgv;

void sysInitArgs(s32 argc, const char **argv)
{
	sysArgc = argc;
	sysArgv = argv;
}

void sysInit(void)
{
	startTick = sysGetMicroseconds();

	if (sysArgCheck("--log")) {
		// figure out where the log is and clear it
		// try working dir first
		snprintf(logPath, sizeof(logPath), "./%s", LOG_FNAME);
		FILE *f = fopen(logPath, "wb");
		if (!f) {
			// try home dir
			sysGetHomePath(logPath, sizeof(logPath) - 1);
			strncat(logPath, "/" LOG_FNAME, sizeof(logPath) - 1);
			f = fopen(logPath, "wb");
		}
		if (f) {
			fclose(f);
		}
	}

#ifdef VERSION_HASH
	sysLogPrintf(LOG_NOTE, "version: " VERSION_HASH " (" VERSION_TARGET ")");
#endif

	char timestr[256];
	const time_t curtime = time(NULL);
	strftime(timestr, sizeof(timestr), "%d %b %Y %H:%M:%S", localtime(&curtime));
	sysLogPrintf(LOG_NOTE, "startup date: %s", timestr);
}

s32 sysArgCheck(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			return 1;
		}
	}
	return 0;
}

const char *sysArgGetString(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return sysArgv[i + 1];
			}
		}
	}
	return NULL;
}

s32 sysArgGetInt(const char *arg, s32 defval)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return strtol(sysArgv[i + 1], NULL, 0);
			}
		}
	}
	return defval;
}

u64 sysGetMicroseconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((u64)tv.tv_sec * USEC_IN_SEC + (u64)tv.tv_usec) - startTick;
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	static const char *prefix[3] = {
		"", "WARNING: ", "ERROR: "
	};

	char logmsg[2048];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(logmsg, sizeof(logmsg), fmt, ap);
	va_end(ap);

	if (logPath[0]) {
		FILE *f = fopen(logPath, "ab");
		if (f) {
			fprintf(f, "%s%s\n", prefix[level], logmsg);
			fclose(f);
		}
	}

	FILE *fout = (level == LOG_NOTE) ? stdout : stderr;
	fprintf(fout, "%s%s\n", prefix[level], logmsg);
}

void sysFatalError(const char *fmt, ...)
{
	static s32 alreadyCrashed = 0;

	if (alreadyCrashed) {
		abort();
	}

	char errmsg[2048] = { 0 };

	alreadyCrashed = 1;

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errmsg, sizeof(errmsg), fmt, ap);
	va_end(ap);

	sysLogPrintf(LOG_ERROR, "FATAL: %s", errmsg);

	fflush(stdout);
	fflush(stderr);

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", errmsg, NULL);

	exit(1);
}

void sysGetExecutablePath(char *outPath, const u32 outLen)
{
	// try asking SDL
	char *sdlPath = SDL_GetBasePath();

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (sysArgc && sysArgv[0] && sysArgv[0][0]) {
		// get exe path from argv[0]
		strncpy(outPath, sysArgv[0], outLen - 1);
		outPath[outLen - 1] = '\0';
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void sysGetHomePath(char *outPath, const u32 outLen)
{
	// try asking SDL
	char *sdlPath = SDL_GetPrefPath("", "perfectdark");

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void *sysMemAlloc(const u32 size)
{
	return malloc(size);
}

void *sysMemZeroAlloc(const u32 size)
{
	return calloc(1, size);
}

void sysMemFree(void *ptr)
{
	free(ptr);
}
