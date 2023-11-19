#include <stdio.h>
#include <SDL.h>
#include <unistd.h>
#include <time.h>

#include "platform.h"
#include "system.h"

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"

// define a way to yield the cpu when in a busy wait
#ifdef PLATFORM_WIN32
// winapi provides a yield macro
#include <windows.h>
#define DO_YIELD() YieldProcessor()
#elif defined(PLATFORM_X86) || defined(PLATFORM_X86_64)
// this should work even if the code is not built with SSE enabled, at least on gcc and clang,
// but if it doesn't we'll have to use  __builtin_ia32_pause() or something
#include <immintrin.h>
#define DO_YIELD() _mm_pause()
#elif defined(PLATFORM_ARM)
// same as YieldProcessor() on ARM Windows
#define DO_YIELD() __asm__ volatile("dmb ishst\n\tyield":::"memory")
#else
// fuck it
#define DO_YIELD() do { } while (0)
#endif

static SDL_Window* wnd;
static SDL_GLContext ctx;
static SDL_Renderer* renderer;
static int sdl_to_lus_table[512];
static bool vsync_enabled = true;
// OTRTODO: These are redundant. Info can be queried from SDL.
static int window_width = DESIRED_SCREEN_WIDTH;
static int window_height = DESIRED_SCREEN_HEIGHT;
static bool fullscreen_state;
static bool is_running = true;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);

static int target_fps = 120; // above 60 since vsync is enabled by default
static uint64_t previous_time;
static uint64_t qpc_freq;

#ifdef PLATFORM_WIN32
// on win32 we use waitable timers instead of nanosleep
typedef HANDLE WINAPI (*CREATEWAITABLETIMEREXAFN)(LPSECURITY_ATTRIBUTES, LPCSTR, DWORD, DWORD);
static HANDLE timer;
static CREATEWAITABLETIMEREXAFN pfnCreateWaitableTimerExA;
#endif

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (target_fps)

static inline void do_sleep(const int64_t left) {
#ifdef PLATFORM_WIN32
    static LARGE_INTEGER li;
    li.QuadPart = -left;
    SetWaitableTimer(timer, &li, 0, nullptr, nullptr, false);
    WaitForSingleObject(timer, INFINITE);
#else
    const timespec spec = { 0, left * 100 };
    nanosleep(&spec, nullptr);
#endif
}

static void set_fullscreen(bool on, bool call_callback) {
    if (fullscreen_state == on) {
        return;
    }
    fullscreen_state = on;
    SDL_SetWindowFullscreen(wnd, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}

static void gfx_sdl_init(const char* game_name, const char* gfx_api_name, bool start_in_fullscreen, uint32_t width,
                         uint32_t height, int32_t posX, int32_t posY) {
    window_width = width;
    window_height = height;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysFatalError("Could not init SDL:\n%s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    if (sysArgCheck("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    char title[512];
    snprintf(title, sizeof(title), "%s (%s)", game_name, gfx_api_name);

    int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    if (display_in_use < 0) { // Fallback to default if out of bounds
        posX = 100;
        posY = 100;
    }

    const Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL;
    wnd = SDL_CreateWindow(title, posX, posY, window_width, window_height, flags);
    if (!wnd) {
        sysFatalError("Could not open SDL window:\n%s", SDL_GetError());
    }

    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static u32 glver[][3] = {
        { 0, 0, 0                                    }, // for command line override
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY },
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE },
        { 2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY },
    };

    u32 glcore = false;
    u32 verstart = 1;
    const u32 verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = sysArgGetString("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    u32 vmin = 0, vmaj = 0;
    for (u32 i = verstart; i < verend; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, glver[i][2]);
        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            sysLogPrintf(LOG_WARNING, "GL: could not create GL%d.%d context: %s", vmaj, vmin, SDL_GetError());
        } else {
            glcore = (glver[i][2] == SDL_GL_CONTEXT_PROFILE_CORE);
            break;
        }
    }

    if (!ctx) {
        sysFatalError("Could not create an OpenGL context of any supported version.\nSDL error: %s", SDL_GetError());
    } else {
        sysLogPrintf(LOG_NOTE, "GL: created GL%d.%d%s context", vmaj, vmin, glcore ? "core" : "");
    }

    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_GL_SetSwapInterval(1);

    qpc_freq = SDL_GetPerformanceFrequency();

#ifdef PLATFORM_WIN32
    // this function is only present on Vista+, so try to import it from kernel32 by hand
    pfnCreateWaitableTimerExA = (CREATEWAITABLETIMEREXAFN)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExA");
    if (pfnCreateWaitableTimerExA) {
        // function exists, try to create a hires timer
        timer = pfnCreateWaitableTimerExA(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (!timer) {
        // no function or hires timers not supported, fallback to lower resolution timer
        sysLogPrintf(LOG_WARNING, "SDL: hires waitable timers not available");
        timer = CreateWaitableTimerA(nullptr, false, nullptr);
    }
#endif

    set_fullscreen(start_in_fullscreen, false);
}

static void gfx_sdl_close(void) {
    is_running = false;
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void gfx_sdl_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    SDL_GL_GetDrawableSize(wnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
    SDL_GetWindowPosition(wnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    // alt-enter received, switch fullscreen state
                    set_fullscreen(!fullscreen_state, true);
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(wnd, &window_width, &window_height);
                } else if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                           event.window.windowID == SDL_GetWindowID(wnd)) {
                    // We listen specifically for main window close because closing main window
                    // on macOS does not trigger SDL_Quit.
                    exit(0);
                }
                break;
            case SDL_QUIT:
                exit(0);
                break;
        }
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        do_sleep(left);
    }

    do {
        DO_YIELD();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t)t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}

static void gfx_sdl_swap_buffers_begin(void) {
    if (target_fps) {
        sync_framerate_with_timer();
    }
    SDL_GL_SwapWindow(wnd);
}

static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double)qpc_freq;
}

static void gfx_sdl_set_target_fps(int fps) {
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return false;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *)wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static void gfx_sdl_set_swap_interval(int interval) {
    SDL_GL_SetSwapInterval(interval);
    vsync_enabled = (interval != 0);
}

struct GfxWindowManagerAPI gfx_sdl = { 
    gfx_sdl_init,
    gfx_sdl_close,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_get_active_window_refresh_rate,
    gfx_sdl_set_cursor_visibility,
    gfx_sdl_get_dimensions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_set_target_fps,
    gfx_sdl_can_disable_vsync,
    gfx_sdl_get_window_handle,
    gfx_sdl_set_window_title,
    gfx_sdl_set_swap_interval,
};
