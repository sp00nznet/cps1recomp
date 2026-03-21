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
    fflush(stdout);

    /* Initialize subsystems in order */
    printf("[init] bus..."); fflush(stdout);
    if (bus_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);
    printf("[init] func_table..."); fflush(stdout);
    if (func_table_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] m68k..."); fflush(stdout);
    m68k_init();
    printf("ok\n"); fflush(stdout);

    printf("[init] video..."); fflush(stdout);
    if (video_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] palette..."); fflush(stdout);
    if (palette_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] io..."); fflush(stdout);
    if (io_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] timer..."); fflush(stdout);
    if (timer_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] z80..."); fflush(stdout);
    if (z80_cpu_init() != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] ym2151..."); fflush(stdout);
    if (ym2151_init(44100) != 0) return -1;
    printf("ok\n"); fflush(stdout);

    printf("[init] oki6295..."); fflush(stdout);
    if (oki6295_init(44100) != 0) return -1;
    printf("ok\n"); fflush(stdout);

    debug_init();

    /* Load ROMs */
    printf("[init] loading ROMs..."); fflush(stdout);
    if (config->rom_path) {
        if (rom_load(config->rom_path, &SF2_ROMSET) != 0) {
            printf("WARNING: ROM loading failed\n"); fflush(stdout);
        } else {
            printf("ok\n"); fflush(stdout);
        }
    }

    /* Initialize platform (SDL2 window, audio, input) */
    int scale = config->window_scale > 0 ? config->window_scale : 3;
    printf("[init] platform (scale=%d)...", scale); fflush(stdout);
    if (platform_init(scale, config->fullscreen, config->vsync) != 0) {
        return -1;
    }
    printf("ok\n"); fflush(stdout);

    platform_audio_init(44100);
    platform_set_title("Street Fighter II Recompiled");

    /* Load vectors from ROM */
    const uint8_t *rom = bus_get_rom_ptr();
    if (rom) {
        m68k_load_vectors(rom);
        printf("[cps1] Entry point: $%06X, SSP: $%08X\n", g_m68k.pc, g_m68k.ssp);
        fflush(stdout);
    } else {
        printf("[cps1] WARNING: No ROM loaded\n");
        fflush(stdout);
    }

    s_initialized = true;
    printf("[init] complete\n"); fflush(stdout);
    return 0;
}

void cps1_run(void) {
    printf("[cps1] Entering main loop (%u functions registered)\n", func_table_count());
    fflush(stdout);

    /*
     * CPS1 game execution model:
     *
     * 1. Run the entry point (hardware init, one-time setup)
     * 2. The game installs a VBlank handler that runs every frame
     * 3. Main loop: render frame, run VBlank handler, present
     *
     * The entry point at $00040E initializes hardware and sets up
     * the game state machine. After init, the game runs from the
     * VBlank IRQ handler at $000A94 each frame.
     */

    /* Run one-time initialization from entry point */
    printf("[cps1] Running entry point at $%06X...\n", g_m68k.pc);
    fflush(stdout);
    if (func_table_lookup(g_m68k.pc)) {
        func_table_call(g_m68k.pc);
    }
    printf("[cps1] Entry point returned\n");
    fflush(stdout);

    /* Find VBlank handler from the vector table */
    const uint8_t *rom = bus_get_rom_ptr();
    uint32_t vblank_addr = 0;
    if (rom) {
        /* IRQ2 vector is at $68 in the 68K vector table */
        vblank_addr = ((uint32_t)rom[0x68] << 24) | ((uint32_t)rom[0x69] << 16) |
                      ((uint32_t)rom[0x6A] << 8)  | rom[0x6B];
    }
    printf("[cps1] VBlank handler: $%06X (%s)\n", vblank_addr,
           func_table_lookup(vblank_addr) ? "registered" : "NOT FOUND");
    fflush(stdout);

    /* Main frame loop */
    int frame = 0;
    while (true) {
        cps1_begin_frame();

        /* Run VBlank handler (the game's per-frame logic) */
        if (vblank_addr && func_table_lookup(vblank_addr)) {
            func_table_call(vblank_addr);
        }

        cps1_trigger_vblank();
        cps1_end_frame();

        if (frame < 3) {
            printf("[frame %d] complete\n", frame); fflush(stdout);
        }
        frame++;
    }
}

void cps1_shutdown(void) {
    platform_shutdown();
    oki6295_shutdown();
    ym2151_shutdown();
    z80_cpu_shutdown();
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

    /* Generate audio: ~735 samples per frame at 44100 Hz / 59.63 Hz */
    #define SAMPLES_PER_FRAME 735
    int16_t ym_buf[SAMPLES_PER_FRAME * 2];    /* Stereo */
    int16_t oki_buf[SAMPLES_PER_FRAME];        /* Mono */
    int16_t mix_buf[SAMPLES_PER_FRAME * 2];    /* Mixed stereo output */

    ym2151_generate(ym_buf, SAMPLES_PER_FRAME);
    oki6295_generate(oki_buf, SAMPLES_PER_FRAME);

    /* Mix: YM2151 stereo + OKI mono (center-panned) */
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
        int32_t l = (int32_t)ym_buf[i * 2 + 0] + (int32_t)oki_buf[i];
        int32_t r = (int32_t)ym_buf[i * 2 + 1] + (int32_t)oki_buf[i];
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        mix_buf[i * 2 + 0] = (int16_t)l;
        mix_buf[i * 2 + 1] = (int16_t)r;
    }
    platform_audio_queue(mix_buf, SAMPLES_PER_FRAME * 2);

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
