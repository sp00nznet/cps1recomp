/*
 * platform.c — SDL2 platform layer for CPS1.
 *
 * Adapted from genrecomp/platform_sdl.c for CPS1's 384x224 resolution.
 * Provides windowing, framebuffer presentation, audio output, and input mapping
 * for a 6-button fighting game layout.
 */

#include <cps1recomp/platform.h>
#include <cps1recomp/video.h>
#include <cps1recomp/io.h>

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_Window       *s_window    = NULL;
static SDL_Renderer     *s_renderer  = NULL;
static SDL_Texture      *s_texture   = NULL;
static SDL_AudioDeviceID s_audio_dev = 0;
static uint64_t          s_frame_start = 0;
static bool              s_fullscreen = false;
static bool              s_initialized = false;

/* CPS1 refresh rate: ~59.63 Hz (16.77 ms/frame) */
#define CPS1_FRAME_NS 16771000ULL

int platform_init(int window_scale, bool fullscreen, bool vsync) {
    if (s_initialized) return 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[platform] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    int w = CPS1_SCREEN_WIDTH * window_scale;
    int h = CPS1_SCREEN_HEIGHT * window_scale;

    uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    s_window = SDL_CreateWindow(
        "Street Fighter II Recompiled",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, flags
    );
    if (!s_window) {
        fprintf(stderr, "[platform] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    uint32_t render_flags = SDL_RENDERER_ACCELERATED;
    if (vsync) render_flags |= SDL_RENDERER_PRESENTVSYNC;

    s_renderer = SDL_CreateRenderer(s_window, -1, render_flags);
    if (!s_renderer) {
        fprintf(stderr, "[platform] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return -1;
    }

    /* Aspect-ratio-correct scaling */
    SDL_RenderSetLogicalSize(s_renderer, CPS1_SCREEN_WIDTH, CPS1_SCREEN_HEIGHT);

    s_texture = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        CPS1_SCREEN_WIDTH, CPS1_SCREEN_HEIGHT);
    if (!s_texture) {
        fprintf(stderr, "[platform] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return -1;
    }

    s_fullscreen = fullscreen;
    s_frame_start = SDL_GetPerformanceCounter();
    s_initialized = true;

    printf("[platform] Window: %dx%d (scale %d, %s, %s)\n",
           w, h, window_scale,
           fullscreen ? "fullscreen" : "windowed",
           vsync ? "vsync" : "no-vsync");

    return 0;
}

void platform_shutdown(void) {
    if (!s_initialized) return;

    if (s_audio_dev > 0) {
        SDL_CloseAudioDevice(s_audio_dev);
        s_audio_dev = 0;
    }
    if (s_texture)  { SDL_DestroyTexture(s_texture);   s_texture  = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);     s_window   = NULL; }

    SDL_Quit();
    s_initialized = false;
}

void platform_present(const uint32_t *framebuffer) {
    if (!s_texture || !s_renderer) return;

    SDL_UpdateTexture(s_texture, NULL, framebuffer, CPS1_SCREEN_WIDTH * 4);
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

void platform_toggle_fullscreen(void) {
    if (!s_window) return;
    s_fullscreen = !s_fullscreen;
    SDL_SetWindowFullscreen(s_window,
        s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool platform_poll_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
            if (ev.key.keysym.sym == SDLK_F11) platform_toggle_fullscreen();
        }
    }

    /* Read keyboard state and map to CPS1 inputs */
    const uint8_t *keys = SDL_GetKeyboardState(NULL);

    /* Player 1: Direction + Punches (player index 0) */
    io_set_button(0, IO_BTN_UP,    keys[SDL_SCANCODE_UP]);
    io_set_button(0, IO_BTN_DOWN,  keys[SDL_SCANCODE_DOWN]);
    io_set_button(0, IO_BTN_LEFT,  keys[SDL_SCANCODE_LEFT]);
    io_set_button(0, IO_BTN_RIGHT, keys[SDL_SCANCODE_RIGHT]);
    io_set_button(0, IO_BTN_LP,    keys[SDL_SCANCODE_A]);     /* Light Punch */
    io_set_button(0, IO_BTN_MP,    keys[SDL_SCANCODE_S]);     /* Medium Punch */
    io_set_button(0, IO_BTN_HP,    keys[SDL_SCANCODE_D]);     /* Heavy Punch */

    /* Player 1: Kicks (player index 2 = P1 kick port) */
    io_set_button(2, IO_BTN_LK, keys[SDL_SCANCODE_Z]);        /* Light Kick */
    io_set_button(2, IO_BTN_MK, keys[SDL_SCANCODE_X]);        /* Medium Kick */
    io_set_button(2, IO_BTN_HK, keys[SDL_SCANCODE_C]);        /* Heavy Kick */

    /* System buttons (player index 4) */
    io_set_button(4, IO_BTN_COIN1,  keys[SDL_SCANCODE_5]);
    io_set_button(4, IO_BTN_START1, keys[SDL_SCANCODE_RETURN]);
    io_set_button(4, IO_BTN_COIN2,  keys[SDL_SCANCODE_6]);
    io_set_button(4, IO_BTN_START2, keys[SDL_SCANCODE_BACKSPACE]);

    return true;
}

int platform_audio_init(int sample_rate) {
    SDL_AudioSpec want = {0};
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;          /* Stereo (YM2151 is stereo) */
    want.samples = 1024;
    want.callback = NULL;       /* Queue-based audio */

    SDL_AudioSpec have;
    s_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio_dev > 0) {
        SDL_PauseAudioDevice(s_audio_dev, 0);
        printf("[platform] Audio: %d Hz, %d ch, %d samples\n",
               have.freq, have.channels, have.samples);
        return 0;
    }

    fprintf(stderr, "[platform] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return -1;
}

void platform_audio_queue(const int16_t *samples, int num_samples) {
    if (s_audio_dev > 0 && samples) {
        SDL_QueueAudio(s_audio_dev, samples,
                       (uint32_t)(num_samples * 2 * sizeof(int16_t)));
    }
}

void platform_frame_sync(void) {
    /* VSync handles timing when renderer has PRESENTVSYNC.
     * This provides a fallback for non-vsync (~59.63 Hz). */
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    /* Target: 1/59.63 seconds per frame */
    uint64_t target = s_frame_start + (freq * 10000ULL) / 596300ULL;

    while (SDL_GetPerformanceCounter() < target) {
        SDL_Delay(0);
    }

    s_frame_start = SDL_GetPerformanceCounter();
}

uint64_t platform_get_ticks(void) {
    return SDL_GetTicks64();
}

void platform_set_title(const char *title) {
    if (s_window) {
        SDL_SetWindowTitle(s_window, title);
    }
}
