/*
 * platform.h — SDL2 platform abstraction layer for CPS1.
 *
 * Handles windowing, input mapping, audio output, and frame timing.
 * This is the only file that touches SDL2 directly.
 */

#ifndef CPS1RECOMP_PLATFORM_H
#define CPS1RECOMP_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the platform layer.
 * Creates SDL2 window (384x224 * scale), renderer, and audio device.
 */
int platform_init(int window_scale, bool fullscreen, bool vsync);
void platform_shutdown(void);

/* Present a rendered frame (384x224 ARGB8888). */
void platform_present(const uint32_t *framebuffer);

void platform_toggle_fullscreen(void);

/* Poll input. Returns false on quit request. */
bool platform_poll_input(void);

/* Audio output. */
int platform_audio_init(int sample_rate);
void platform_audio_queue(const int16_t *samples, int num_samples);

/* Frame sync (~59.63 Hz). */
void platform_frame_sync(void);

uint64_t platform_get_ticks(void);
void platform_set_title(const char *title);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_PLATFORM_H */
