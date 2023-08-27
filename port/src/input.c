#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#include "platform.h"
#include "input.h"
#include "video.h"
#include "config.h"

#define MAX_BINDS 4
#define TRIG_THRESHOLD (30 * 256)
#define DEFAULT_DEADZONE 4096
#define DEFAULT_DEADZONE_RY 6144

#define WHEEL_UP_MASK SDL_BUTTON(VK_MOUSE_WHEEL_UP - VK_MOUSE_BEGIN + 1)
#define WHEEL_DN_MASK SDL_BUTTON(VK_MOUSE_WHEEL_DN - VK_MOUSE_BEGIN + 1)

static SDL_GameController *pads[INPUT_MAX_CONTROLLERS];
static s32 rumbleSupported[INPUT_MAX_CONTROLLERS];

static u32 binds[MAXCONTROLLERS][CK_TOTAL_COUNT][MAX_BINDS]; // [i][CK_][b] = [VK_]

static s32 connectedMask = 0;

static s32 mouseEnabled = 1;
static s32 mouseLocked = 0;
static s32 mouseX, mouseY;
static s32 mouseDX, mouseDY;
static u32 mouseButtons;
static s32 mouseWheel = 0;

static f32 mouseSensX = 1.5f;
static f32 mouseSensY = 1.5f;

static f32 rumbleScale = 0.333f;

// NOTE: by default this gets inverted for 1.2
static u32 axisMap[2][2] = {
	{ SDL_CONTROLLER_AXIS_LEFTX,  SDL_CONTROLLER_AXIS_LEFTY  },
	{ SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY },
};

static f32 stickSens[4] = {
	// index == SDL_CONTROLLER_AXIS_*
	1.f, 1.f, 1.f, 1.f
};

static s32 deadzone[4] = {
	// index == SDL_CONTROLLER_AXIS_*
	DEFAULT_DEADZONE, DEFAULT_DEADZONE,
	DEFAULT_DEADZONE, DEFAULT_DEADZONE_RY,
};

static s32 stickCButtons = 1;

static const char *ckNames[CK_TOTAL_COUNT] = {
	"R_CBUTTONS",
	"L_CBUTTONS",
	"D_CBUTTONS",
	"U_CBUTTONS",
	"R_TRIG",
	"L_TRIG",
	"X_BUTTON",
	"Y_BUTTON",
	"R_JPAD",
	"L_JPAD",
	"D_JPAD",
	"U_JPAD",
	"START_BUTTON",
	"Z_TRIG",
	"B_BUTTON",
	"A_BUTTON",
	"STICK_XNEG",
	"STICK_XPOS",
	"STICK_YNEG",
	"STICK_YPOS",
};

static const char *vkPunctNames[] = {
	"MINUS", "EQUALS", "LEFTBRACKET", "RIGHTBRACKET", "BACKSLASH",
	"HASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "PERIOD", "SLASH"
};

static const char *vkMouseNames[] = {
	"MOUSE_LEFT",
	"MOUSE_MIDDLE",
	"MOUSE_RIGHT",
	"MOUSE_X1",
	"MOUSE_X2",
	"MOUSE_WHEEL_UP",
	"MOUSE_WHEEL_DN",
};

static const char *vkJoyNames[] = {
	"JOY1_A",
	"JOY1_B",
	"JOY1_X",
	"JOY1_Y",
	"JOY1_BACK",
	"JOY1_GUIDE",
	"JOY1_START",
	"JOY1_LSTICK",
	"JOY1_RSTICK",
	"JOY1_LSHOULDER",
	"JOY1_RSHOULDER",
	"JOY1_DPAD_UP",
	"JOY1_DPAD_DOWN",
	"JOY1_DPAD_LEFT",
	"JOY1_DPAD_RIGHT",
	"JOY1_BUTTON_15",
	"JOY1_BUTTON_16",
	"JOY1_BUTTON_17",
	"JOY1_BUTTON_18",
	"JOY1_BUTTON_19",
	"JOY1_TOUCHPAD",
	"JOY1_BUTTON_21",
	"JOY1_BUTTON_22",
	"JOY1_BUTTON_23",
	"JOY1_BUTTON_24",
	"JOY1_BUTTON_25",
	"JOY1_BUTTON_26",
	"JOY1_BUTTON_27",
	"JOY1_BUTTON_28",
	"JOY1_BUTTON_29",
	"JOY1_LTRIGGER",
	"JOY1_RTRIGGER",
};

static char vkNames[VK_TOTAL_COUNT][64];

void inputSetDefaultKeyBinds(void)
{
	// TODO: make VK constants for all these
	static const u32 kbbinds[][3] = {
		{ CK_B,             SDL_SCANCODE_E,      0                   },
		{ CK_X,             SDL_SCANCODE_R,      0                   },
		{ CK_RTRIG,         VK_MOUSE_RIGHT,      SDL_SCANCODE_Z      },
		{ CK_LTRIG,         SDL_SCANCODE_F,      SDL_SCANCODE_X      },
		{ CK_ZTRIG,         VK_MOUSE_LEFT,       SDL_SCANCODE_SPACE  },
		{ CK_START,         SDL_SCANCODE_RETURN, SDL_SCANCODE_TAB    },
		{ CK_DPAD_D,        SDL_SCANCODE_Q,      VK_MOUSE_MIDDLE     },
		{ CK_DPAD_U,        0,                   0                   },
		{ CK_Y,             VK_MOUSE_WHEEL_DN,   0                   },
		{ CK_DPAD_L,        VK_MOUSE_WHEEL_UP,   0                   },
		{ CK_C_D,           SDL_SCANCODE_S,      0                   },
		{ CK_C_U,           SDL_SCANCODE_W,      0                   },
		{ CK_C_R,           SDL_SCANCODE_D,      0                   },
		{ CK_C_L,           SDL_SCANCODE_A,      0                   },
		{ CK_STICK_XNEG,    SDL_SCANCODE_LEFT,   0                   },
		{ CK_STICK_XPOS,    SDL_SCANCODE_RIGHT,  0                   },
		{ CK_STICK_YNEG,    SDL_SCANCODE_DOWN,   0                   },
		{ CK_STICK_YPOS,    SDL_SCANCODE_UP,     0                   },
	};

	static const u32 joybinds[][2] = {
		{ CK_B,      SDL_CONTROLLER_BUTTON_A             },
		{ CK_X,      SDL_CONTROLLER_BUTTON_X             },
		{ CK_Y,      SDL_CONTROLLER_BUTTON_Y             },
		{ CK_DPAD_L, SDL_CONTROLLER_BUTTON_B,            },
		{ CK_DPAD_D, SDL_CONTROLLER_BUTTON_LEFTSHOULDER  },
		{ CK_LTRIG,  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
		{ CK_RTRIG,  VK_JOY1_LTRIG - VK_JOY1_BEGIN       },
		{ CK_ZTRIG,  VK_JOY1_RTRIG - VK_JOY1_BEGIN       },
		{ CK_START,  SDL_CONTROLLER_BUTTON_START         },
		{ CK_C_D,    SDL_CONTROLLER_BUTTON_DPAD_DOWN     },
		{ CK_C_U,    SDL_CONTROLLER_BUTTON_DPAD_UP       },
		{ CK_C_R,    SDL_CONTROLLER_BUTTON_DPAD_RIGHT    },
		{ CK_C_L,    SDL_CONTROLLER_BUTTON_DPAD_LEFT     },
	};

	for (u32 i = 0; i < sizeof(kbbinds) / sizeof(kbbinds[0]); ++i) {
		for (s32 j = 1; j < 3; ++j) {
			if (kbbinds[i][j]) {
				inputKeyBind(0, kbbinds[i][0], j - 1, kbbinds[i][j]);
			}
		}
	}

	for (u32 i = 0; i < sizeof(joybinds) / sizeof(joybinds[0]); ++i) {
		for (s32 j = 0; j < INPUT_MAX_CONTROLLERS; ++j) {
			inputKeyBind(j, joybinds[i][0], -1, VK_JOY_BEGIN + j * INPUT_MAX_CONTROLLER_BUTTONS + joybinds[i][1]);
		}
	}
}

static inline void inputInitController(const s32 cidx)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// SDL_GameControllerHasRumble() appeared in 2.0.18 even though SDL_GameControllerRumble() is in 2.0.9
	rumbleSupported[cidx] = SDL_GameControllerHasRumble(pads[cidx]);
#else
	// assume that all joysticks with haptic feedback support will support rumble
	rumbleSupported[cidx] = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[cidx]));
	if (!rumbleSupported[cidx]) {
		// at least on Windows some controllers will report no haptics, but rumble will still function
		// just assume it's supported if the controller is of known type
		const SDL_GameControllerType ctype = SDL_GameControllerGetType(pads[cidx]);
		rumbleSupported[cidx] = ctype && (ctype != SDL_CONTROLLER_TYPE_VIRTUAL);
	}
#endif

	// make the LEDs on the controller indicate which player it's for
	SDL_GameControllerSetPlayerIndex(pads[cidx], cidx);

	connectedMask |= (1 << cidx);
}

static inline void inputCloseController(const s32 cidx)
{
	SDL_GameControllerClose(pads[cidx]);
	pads[cidx] = NULL;
	rumbleSupported[cidx] = 0;
	if (cidx) {
		connectedMask &= ~(1 << cidx);
	}
}

static int inputEventFilter(void *data, SDL_Event *event)
{
	switch (event->type) {
		case SDL_CONTROLLERDEVICEADDED:
			for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
				if (!pads[i]) {
					pads[i] = SDL_GameControllerOpen(event->cdevice.which);
					if (pads[i]) {
						inputInitController(i);
					}
					break;
				}
			}
			break;

		case SDL_CONTROLLERDEVICEREMOVED: {
			SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
			if (ctrl) {
				for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
					if (pads[i] == ctrl) {
						inputCloseController(i);
						break;
					}
				}
			}
			break;
		}

		case SDL_MOUSEWHEEL:
			mouseWheel = event->wheel.y;
			break;

		default:
			break;
	}

	return 0;
}

static inline void inputGetScancodeName(const SDL_Scancode sc, char *out, size_t len)
{
		const char *scname = SDL_GetScancodeName(sc);
		if (scname) {
			strncpy(out, scname, len - 1);
			for (u32 i = 0; i < len && out[i]; ++i) {
				if (out[i] == ' ') {
					out[i] = '_';
				} else {
					out[i] = toupper(out[i]);
				}
			}
		} else {
			snprintf(out, len, "KEY%d", (s32)sc);
		}
}

static inline void inputInitKeyNames(void)
{
	for (SDL_Scancode key = SDL_SCANCODE_A; key <= SDL_SCANCODE_SPACE; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	// special characters
	for (SDL_Scancode key = SDL_SCANCODE_MINUS; key < SDL_SCANCODE_CAPSLOCK; ++key) {
		strcpy(vkNames[key], vkPunctNames[key - SDL_SCANCODE_MINUS]);
	}

	for (SDL_Scancode key = SDL_SCANCODE_CAPSLOCK; key <= SDL_SCANCODE_NUMLOCKCLEAR; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	// keypad names
	strcpy(vkNames[SDL_SCANCODE_KP_DIVIDE], "KP_DIVIDE");
	strcpy(vkNames[SDL_SCANCODE_KP_MULTIPLY], "KP_MULTIPLY");
	strcpy(vkNames[SDL_SCANCODE_KP_MINUS], "KP_MINUS");
	strcpy(vkNames[SDL_SCANCODE_KP_PLUS], "KP_PLUS");
	strcpy(vkNames[SDL_SCANCODE_KP_ENTER], "KP_ENTER");
	strcpy(vkNames[SDL_SCANCODE_KP_PERIOD], "KP_PERIOD");
	strcpy(vkNames[SDL_SCANCODE_KP_EQUALS], "KP_EQUALS");
	for (SDL_Scancode key = SDL_SCANCODE_KP_1; key < SDL_SCANCODE_KP_0; ++key) {
		char tmp[8] = "KP_1";
		tmp[3] = '1' + (key - SDL_SCANCODE_KP_1);
		strcpy(vkNames[key], tmp);
	}

	for (SDL_Scancode key = SDL_SCANCODE_LCTRL; key <= SDL_SCANCODE_RGUI; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	// mouse names
	for (u32 vk = VK_MOUSE_BEGIN; vk < VK_JOY1_BEGIN; ++vk) {
		strcpy(vkNames[vk], vkMouseNames[vk - VK_MOUSE_BEGIN]);
	}

	// joystick names
	for (u32 vk = VK_JOY1_BEGIN; vk < VK_TOTAL_COUNT; ++vk) {
		const u32 jidx = (vk - VK_JOY1_BEGIN) / INPUT_MAX_CONTROLLER_BUTTONS;
		const u32 jbtn = (vk - VK_JOY1_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS;
		strcpy(vkNames[vk], vkJoyNames[jbtn]);
		vkNames[vk][3] = '1' + jidx;
	}
}

static inline void inputSaveBinds(void)
{
	char bindstr[256];
	char keyname[256];
	char secname[] = "Input.Player1.Binds";

	bindstr[sizeof(bindstr) - 1] = '\0';

	for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
		secname[12] = '1' + i;
		for (u32 ck = 0; ck < CK_TOTAL_COUNT; ++ck) {
			snprintf(keyname, sizeof(keyname), "%s.%s", secname, inputGetContKeyName(ck));
			bindstr[0] = '\0';
			for (s32 b = 0; b < MAX_BINDS; ++b) {
				if (binds[i][ck][b]) {
					if (b) {
						strncat(bindstr, ", ", sizeof(bindstr) - 1);
					}
					strncat(bindstr, inputGetKeyName(binds[i][ck][b]), sizeof(bindstr) - 1);
				}
			}
			configSetString(keyname, bindstr[0] ? bindstr : "NONE");
		}
	}
}

static inline void inputParseBindString(const s32 ctrl, const u32 ck, char *bindstr)
{
	// unbind all first
	memset(binds[ctrl][ck], 0, sizeof(binds[ctrl][ck]));

	if (!strcasecmp(bindstr, "NONE")) {
		// explicitly nothing bound
		return;
	}

	const char *tok = strtok(bindstr, ", ");
	while (tok) {
		if (tok[0]) {
			const s32 vk = inputGetKeyByName(tok);
			if (vk > 0) {
				inputKeyBind(ctrl, ck, -1, vk);
			}
		}
		tok = strtok(NULL, ", ");
	}
}

static inline void inputLoadBinds(void)
{
	char secname[] = "Input.Player1.Binds";
	char keyname[256];
	char bindstr[256];

	for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
		secname[12] = '1' + i;
		for (u32 ck = 0; ck < CK_TOTAL_COUNT; ++ck) {
			snprintf(keyname, sizeof(keyname), "%s.%s", secname, inputGetContKeyName(ck));
			const char *cfgstr = configGetString(keyname, "");
			if (cfgstr[0]) {
				strncpy(bindstr, cfgstr, sizeof(bindstr) - 1);
				bindstr[sizeof(bindstr) - 1] = '\0';
				inputParseBindString(i, ck, bindstr);
			}
		}
	}
}

void inputSaveConfig(void)
{
	inputSaveBinds();
}

s32 inputInit(void)
{
	if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC)) {
		SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
	}

	SDL_GameControllerUpdate();

	const s32 numJoys = SDL_NumJoysticks();

	connectedMask = 1; // always report first controller as connected

	// if this is set to 1, keyboard will count as a separate controller on its own,
	// so the first connected gamepad will go to player 2 instead of player 1
	const s32 cstart = configGetInt("Input.FirstGamepadNum", 0);

	for (s32 jidx = 0, cidx = cstart; jidx < numJoys && cidx < INPUT_MAX_CONTROLLERS; ++jidx) {
		if (SDL_IsGameController(jidx)) {
			pads[cidx] = SDL_GameControllerOpen(jidx);
			if (pads[cidx]) {
				inputInitController(cidx);
				++cidx;
			}
		}
	}

	// since the main event loop is elsewhere, we can receive some events we need using a watcher
	SDL_AddEventWatch(inputEventFilter, NULL);

	inputInitKeyNames();

	inputSetDefaultKeyBinds();

	mouseEnabled = configGetInt("Input.MouseEnabled", 1);
	mouseSensX = configGetFloat("Input.MouseSpeedX", 1.5f);
	mouseSensY = configGetFloat("Input.MouseSpeedY", 1.5f);

	rumbleScale = configGetFloat("Input.RumbleScale", 0.333f);

	deadzone[0] = configGetInt("Input.LStickDeadzoneX", DEFAULT_DEADZONE);
	deadzone[1] = configGetInt("Input.LStickDeadzoneY", DEFAULT_DEADZONE);
	deadzone[2] = configGetInt("Input.RStickDeadzoneX", DEFAULT_DEADZONE);
	deadzone[3] = configGetInt("Input.RStickDeadzoneY", DEFAULT_DEADZONE_RY);

	stickSens[0] = configGetFloat("Input.LStickScaleX", 1.f);
	stickSens[1] = configGetFloat("Input.LStickScaleY", 1.f);
	stickSens[2] = configGetFloat("Input.RStickScaleX", 1.f);
	stickSens[3] = configGetFloat("Input.RStickScaleY", 1.f);

	stickCButtons = configGetInt("Input.StickCButtons", 1);

	if (configGetInt("Input.SwapSticks", 1)) {
		// invert axis map
		axisMap[0][0] = SDL_CONTROLLER_AXIS_RIGHTX;
		axisMap[0][1] = SDL_CONTROLLER_AXIS_RIGHTY;
		axisMap[1][0] = SDL_CONTROLLER_AXIS_LEFTX;
		axisMap[1][1] = SDL_CONTROLLER_AXIS_LEFTY;
	}

	const s32 overrideMask = (1 << configGetInt("Input.FakeGamepads", 0)) - 1;
	if (overrideMask) {
		connectedMask = overrideMask;
	}

	inputLoadBinds();

	return connectedMask;
}

static inline s32 inputBindPressed(const s32 idx, const u32 ck)
{
	for (s32 i = 0; i < MAX_BINDS; ++i) {
		if (binds[idx][ck][i]) {
			if (inputKeyPressed(binds[idx][ck][i])) {
				return 1;
			}
		}
	}
	return 0;
}

static inline s32 inputAxisScale(s32 x, const s32 deadzone, const f32 scale)
{
	if (abs(x) < deadzone) {
		return 0;
	} else {
		// rescale to fit the non-deadzone range
		if (x < 0) {
			x += deadzone;
		} else {
			x -= deadzone;
		}
		x = x * 32768 / (32768 - deadzone);
		// scale with sensitivity
		x *= scale;
		return (x > 32767) ? 32767 : ((x < -32768) ? -32768 : x);
	}
}

s32 inputReadController(s32 idx, OSContPad *npad)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS  || !npad) {
		return -1;
	}

	npad->button = 0;

	for (u32 i = 0; i < CONT_NUM_BUTTONS; ++i) {
		if (inputBindPressed(idx, i)) {
			npad->button |= 1U << i;
		}
	}

	const s32 xdiff = (inputBindPressed(idx, CK_STICK_XPOS) - inputBindPressed(idx, CK_STICK_XNEG));
	const s32 ydiff = (inputBindPressed(idx, CK_STICK_YPOS) - inputBindPressed(idx, CK_STICK_YNEG));
	npad->stick_x = xdiff < 0 ? -0x80 : (xdiff > 0 ? 0x7F : 0);
	npad->stick_y = ydiff < 0 ? -0x80 : (ydiff > 0 ? 0x7F : 0);

	if (!pads[idx]) {
		return 0;
	}

	s32 leftX = SDL_GameControllerGetAxis(pads[idx], axisMap[0][0]);
	s32 leftY = SDL_GameControllerGetAxis(pads[idx], axisMap[0][1]);
	s32 rightX = SDL_GameControllerGetAxis(pads[idx], axisMap[1][0]);
	s32 rightY = SDL_GameControllerGetAxis(pads[idx], axisMap[1][1]);

	leftX = inputAxisScale(leftX, deadzone[axisMap[0][0]], stickSens[axisMap[0][0]]);
	leftY = inputAxisScale(leftY, deadzone[axisMap[0][1]], stickSens[axisMap[0][1]]);
	rightX = inputAxisScale(rightX, deadzone[axisMap[1][0]], stickSens[axisMap[1][0]]);
	rightY = inputAxisScale(rightY, deadzone[axisMap[1][1]], stickSens[axisMap[1][1]]);

	if (stickCButtons) {
		if (rightX < -0x4000) npad->button |= L_CBUTTONS;
		if (rightX > +0x4000) npad->button |= R_CBUTTONS;
		if (rightY < -0x4000) npad->button |= U_CBUTTONS;
		if (rightY > +0x4000) npad->button |= D_CBUTTONS;
	}

	if (!npad->stick_x && leftX) {
		npad->stick_x = leftX / 0x100;
	}

	s32 stickY = -leftY / 0x100;
	if (!npad->stick_y && stickY) {
		npad->stick_y = (stickY == 128) ? 127 : stickY;
	}

	stickY = -rightY / 0x100;
	npad->rstick_y = (stickY == 128) ? 127 : stickY;
	npad->rstick_x = rightX / 0x100;

	return 0;
}

static inline void inputUpdateMouse(void)
{
	s32 mx, my;
	mouseButtons = SDL_GetMouseState(&mx, &my);

	if (mouseWheel > 0) {
		mouseButtons |= WHEEL_UP_MASK;
	} else if (mouseWheel < 0) {
		mouseButtons |= WHEEL_DN_MASK;
	}

	mouseWheel = 0;

	if (mouseLocked) {
		SDL_GetRelativeMouseState(&mouseDX, &mouseDY);
	} else {
		mouseDX = mx - mouseX;
		mouseDY = my - mouseY;
	}

	mouseX = mx;
	mouseY = my;

	if (!mouseLocked && (mouseButtons & SDL_BUTTON_LMASK)) {
		inputLockMouse(1);
	} else if (mouseLocked && inputKeyPressed(VK_ESCAPE)) {
		inputLockMouse(0);
	}
}

void inputUpdate(void)
{
	SDL_GameControllerUpdate();

	if (mouseEnabled) {
		inputUpdateMouse();
	}
}

s32 inputControllerConnected(s32 idx)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}
	return pads[idx] || (connectedMask & (1 << idx));
}

s32 inputRumbleSupported(s32 idx)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}
	return rumbleSupported[idx];
}

void inputRumble(s32 idx, f32 strength, f32 time)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || !pads[idx]) {
		return;
	}

	if (rumbleSupported[idx]) {
		if (strength <= 0.f) {
			strength = 0.f;
			time = 0.f;
		} else {
			strength *= 65535.f;
			time *= 1000.f;
		}
		SDL_GameControllerRumble(pads[idx], (u16)strength, (u16)strength, (u32)time);
	}
}

s32 inputControllerMask(void)
{
	return connectedMask;
}

void inputKeyBind(s32 idx, u32 ck, s32 bind, u32 vk)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || bind >= MAX_BINDS || ck >= CK_TOTAL_COUNT) {
		return;
	}

	if (bind < 0) {
		for (s32 i = 0; i < MAX_BINDS; ++i) {
			if (binds[idx][ck][i] == 0) {
				bind = i;
				break;
			}
		}
		if (bind < 0) {
			bind = MAX_BINDS - 1; // just overwrite last
		}
	}

	binds[idx][ck][bind] = vk;
}

s32 inputKeyPressed(u32 vk)
{
	if (vk >= VK_KEYBOARD_BEGIN && vk < VK_MOUSE_BEGIN) {
		const u8 *state = SDL_GetKeyboardState(NULL);
		return state[vk - VK_KEYBOARD_BEGIN];
	} else if (vk >= VK_MOUSE_BEGIN && vk < VK_JOY_BEGIN) {
		return mouseButtons & SDL_BUTTON(vk - VK_MOUSE_BEGIN + 1);
	} else if (vk >= VK_JOY_BEGIN && vk < VK_TOTAL_COUNT) {
		vk -= VK_JOY_BEGIN;
		const s32 idx = vk / INPUT_MAX_CONTROLLER_BUTTONS;
		if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || !pads[idx]) {
			return 0;
		}
		vk = vk % INPUT_MAX_CONTROLLER_BUTTONS;
		// triggers
		if (vk == 30 || vk == 31) {
			const s32 trig = SDL_CONTROLLER_AXIS_TRIGGERLEFT + vk - 30;
			return SDL_GameControllerGetAxis(pads[idx], trig) > TRIG_THRESHOLD;
		}
		return SDL_GameControllerGetButton(pads[idx], vk);
	}
	return 0;
}

static inline u16 inputContToContKey(const u16 cont)
{
	if (cont == 0) {
		return 0;
	}
	// just a log2 to convert CONT_* to their indices
	return 32 - __builtin_clz(cont - 1);
}

s32 inputButtonPressed(s32 idx, u16 contbtn)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}

	return inputBindPressed(idx, inputContToContKey(contbtn));
}

void inputLockMouse(s32 lock)
{
	mouseLocked = !!lock;
	SDL_SetRelativeMouseMode(mouseLocked);
}

s32 inputMouseIsLocked(void)
{
	return mouseLocked;
}

void inputMouseGetPosition(s32 *x, s32 *y)
{
	if (x) *x = mouseX * videoGetNativeWidth() / videoGetWidth();
	if (y) *y = mouseY * videoGetNativeHeight() / videoGetHeight();
}

void inputMouseGetRawDelta(s32 *dx, s32 *dy)
{
	if (dx) *dx = mouseDX;
	if (dy) *dy = mouseDY;
}

void inputMouseGetScaledDelta(f32 *dx, f32 *dy)
{
	f32 mdx, mdy;
	if (mouseLocked) {
		mdx = mouseSensX * (f32)mouseDX / 100.0f;
		mdy = mouseSensY * (f32)mouseDY / 100.0f;
	} else {
		mdx = 0.f;
		mdy = 0.f;
	}
	if (dx) *dx = mdx;
	if (dy) *dy = mdy;
}

const char *inputGetContKeyName(u32 ck)
{
	if (ck >= CK_TOTAL_COUNT) {
		return "";
	}
	return ckNames[ck];
}

s32 inputGetContKeyByName(const char *name)
{
	for (u32 i = 0; i < CK_TOTAL_COUNT; ++i) {
		if (!strcmp(name, ckNames[i])) {
			return i;
		}
	}
	fprintf(stderr, "unknown bind name: `%s`\n", name);
	return -1;
}

const char *inputGetKeyName(s32 vk)
{
	if (vk < 0 || vk >= VK_TOTAL_COUNT) {
		vk = 0;
	}
	if (!vkNames[vk][0]) {
		snprintf(vkNames[vk], sizeof(vkNames[vk]), "UNKNOWN%d", vk);
	}
	return vkNames[vk];
}

s32 inputGetKeyByName(const char *name)
{
	s32 start = 0;
	s32 end = 0;

	if (!strncmp(name, "JOY", 3) && isdigit(name[3])) {
		const s32 idx = name[3] - '1';
		if (idx >= 0 && idx < INPUT_MAX_CONTROLLERS) {
			start = VK_JOY1_BEGIN + idx * INPUT_MAX_CONTROLLER_BUTTONS;
			end = start + INPUT_MAX_CONTROLLER_BUTTONS;
		}
	} else if (!strncmp(name, "MOUSE", 5)) {
		start = VK_MOUSE_BEGIN;
		end = VK_JOY1_BEGIN;
	} else if (!strncmp(name, "UNKNOWN", 7) && isdigit(name[7])) {
		const s32 key = atoi(name + 7);
		if (key >= 0 && key < VK_TOTAL_COUNT) {
			return key;
		}
	} else {
		end = VK_MOUSE_BEGIN;
	}

	for (s32 i = start; i < end; ++i) {
		if (!strcmp(vkNames[i], name)) {
			return i;
		}
	}

	fprintf(stderr, "unknown key name: `%s`\n", name);

	return -1;
}
