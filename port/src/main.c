#include <stdlib.h>
#include <stdio.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "config.h"
#include "system.h"

u32 g_OsMemSize = 0;
u8 g_Is4Mb = 0;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

extern s32 g_StageNum;

s32 bootGetMemSize(void)
{
	return (s32)g_OsMemSize;
}

void *bootAllocateStack(s32 threadid, s32 size)
{
	static u8 bruh[0x1000];
	return bruh;
}

void bootCreateSched(void)
{
	osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
	if (osTvType == OS_TV_MPAL) {
		osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
	} else {
		osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
	}
}

static void cleanup(void)
{
	sysLogPrintf(LOG_NOTE, "shutdown");
	inputSaveConfig();
	configSave(CONFIG_FNAME);
	crashShutdown();
	// TODO: actually shut down all subsystems
}

static void gameLoadConfig(void)
{
	osMemSize = configGetIntClamped("Game.MemorySize", 16, 4, 2048) * 1024 * 1024;
	g_PlayerCrosshairSway = configGetFloatClamped("Game.CrosshairSway", g_PlayerCrosshairSway, 0.f, 10.f);
	g_PlayerDefaultFovY = configGetFloatClamped("Game.FovY", g_PlayerDefaultFovY, 5.f, 175.f);
	g_PlayerMouseAimMode = configGetIntClamped("Game.MouseAimMode", g_PlayerMouseAimMode, 0, 1);
	g_PlayerMouseAimSpeedX = configGetFloatClamped("Game.MouseAimSpeedX", g_PlayerMouseAimSpeedX, 0.f, 10.f);
	g_PlayerMouseAimSpeedY = configGetFloatClamped("Game.MouseAimSpeedY", g_PlayerMouseAimSpeedY, 0.f, 10.f);
	g_ViShakeIntensityMult = configGetFloatClamped("Game.ScreenShakeIntensity", 1.f, 0.f, 100.f);
}

int main(int argc, const char **argv)
{
	crashInit();
	sysInit(argc, argv);
	fsInit();
	configInit();
	videoInit();
	inputInit();
	audioInit();
	romdataInit();

	gameLoadConfig();

	atexit(cleanup);

	bootCreateSched();

	g_OsMemSize = osGetMemSize();

	g_MempHeapSize = g_OsMemSize;
	g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
	if (!g_MempHeap) {
		sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
	}

	sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
	sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);

	g_SndDisabled = sysArgCheck("--no-sound");
	g_StageNum = sysArgGetInt("--boot-stage", STAGE_TITLE);
	if (g_StageNum != STAGE_TITLE) {
		sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
	}

	mainProc();

	return 0;
}
