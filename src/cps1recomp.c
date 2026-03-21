/*
 * cps1recomp.c — Main CPS1 runtime orchestrator.
 */

#include <cps1recomp/cps1recomp.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_framebuffer[CPS1_SCREEN_WIDTH * CPS1_SCREEN_HEIGHT];
static bool s_initialized = false;

int cps1_init(const cps1_config_t *config) {
    printf("cps1recomp v%d.%d.%d\n",
           CPS1RECOMP_VERSION_MAJOR, CPS1RECOMP_VERSION_MINOR, CPS1RECOMP_VERSION_PATCH);

    /* Initialize subsystems in order */
    if (bus_init() != 0) return -1;
    if (func_table_init() != 0) return -1;
    m68k_init();
    if (video_init() != 0) return -1;
    if (palette_init() != 0) return -1;
    if (io_init() != 0) return -1;
    if (timer_init() != 0) return -1;
    if (z80_init() != 0) return -1;
    if (ym2151_init(44100) != 0) return -1;
    if (oki6295_init(44100) != 0) return -1;
    debug_init();

    /* Load ROMs */
    if (config->rom_path) {
        if (rom_load(config->rom_path, &SF2_ROMSET) != 0) {
            fprintf(stderr, "[cps1] ROM loading not yet implemented (Phase 1)\n");
            /* Don't fail — allow skeleton to run */
        }
    }

    /* Initialize platform (SDL2 window, audio, input) */
    int scale = config->window_scale > 0 ? config->window_scale : 3;
    if (platform_init(scale, config->fullscreen, config->vsync) != 0) {
        return -1;
    }
    platform_audio_init(44100);
    platform_set_title("Street Fighter II Recompiled");

    /* Load vectors from ROM */
    const uint8_t *rom = bus_get_rom_ptr();
    if (rom) {
        m68k_load_vectors(rom);
        printf("[cps1] Entry point: $%06X, SSP: $%08X\n", g_m68k.pc, g_m68k.ssp);
    }

    s_initialized = true;
    return 0;
}

void cps1_run(void) {
    printf("[cps1] Entering main loop (%u functions registered)\n", func_table_count());

    while (true) {
        cps1_begin_frame();

        /* Execute recompiled 68K code (one frame's worth) */
        if (func_table_lookup(g_m68k.pc)) {
            func_table_call(g_m68k.pc);
        }

        cps1_trigger_vblank();
        cps1_end_frame();
    }
}

void cps1_shutdown(void) {
    platform_shutdown();
    oki6295_shutdown();
    ym2151_shutdown();
    z80_shutdown();
    timer_shutdown();
    io_shutdown();
    palette_shutdown();
    video_shutdown();
    func_table_shutdown();
    bus_shutdown();
    rom_shutdown();
    debug_shutdown();
    s_initialized = false;
}

void cps1_begin_frame(void) {
    io_update();
}

void cps1_trigger_vblank(void) {
    timer_trigger_vblank();

    /* Render the current frame */
    video_render_frame(s_framebuffer);

    /* Generate audio */
    int16_t ym_buf[2048];
    int16_t oki_buf[1024];
    ym2151_generate(ym_buf, 735);   /* ~44100/60 samples per frame */
    oki6295_generate(oki_buf, 735);

    /* TODO: Mix YM2151 stereo + OKI mono -> output */
    platform_audio_queue(ym_buf, 735 * 2);

    /* Run Z80 for one frame */
    z80_execute(60192);  /* 3.579545 MHz / 59.63 Hz */
}

void cps1_end_frame(void) {
    /* Present rendered frame */
    platform_present(s_framebuffer);

    /* Poll input (returns false if quit requested) */
    if (!platform_poll_input()) {
        cps1_shutdown();
        exit(0);
    }

    /* Frame timing */
    platform_frame_sync();
}

const char *cps1_version_string(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             CPS1RECOMP_VERSION_MAJOR, CPS1RECOMP_VERSION_MINOR, CPS1RECOMP_VERSION_PATCH);
    return buf;
}
