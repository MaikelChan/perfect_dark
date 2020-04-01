#include <ultra64.h>
#include "constants.h"
#include "game/data/data_000000.h"
#include "game/data/data_0083d0.h"
#include "game/data/data_00e460.h"
#include "game/data/data_0160b0.h"
#include "game/data/data_01a3a0.h"
#include "game/data/data_020df0.h"
#include "game/data/data_02da90.h"
#include "game/game_131060.h"
#include "gvars/gvars.h"
#include "lib/lib_0d520.h"
#include "types.h"

void weatherTick(void)
{
	if (!g_WeatherData
			|| (g_StageIndex == STAGEINDEX_AIRBASE && g_Vars.currentplayer->cam_pos.z < -2000.0f)
			|| (g_StageIndex == STAGEINDEX_G5BUILDING && g_Vars.tickmode != TICKMODE_6)) {
		return;
	}

	func0000db30("wettick", &var80062954);
	func0000db30("windspeed", &var80062950);

	if (var80062954) {
		switch (g_WeatherData->unk20) {
		case 0:
			func0f131a30(g_WeatherData);
			break;
		case 1:
			func0f1321d0(g_WeatherData);
			break;
		}
	}
}
