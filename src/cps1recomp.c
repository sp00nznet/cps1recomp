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
        /* CPS1 SF2: SSP in ROM is 0 — the init code sets it up.
         * We need a valid stack for recompiled code to work, so
         * set SSP to top of Work RAM if it's 0. */
        printf("[cps1] ROM SSP: $%08X, A7: $%08X\n", g_m68k.ssp, g_m68k.a[7]);
        fflush(stdout);
        /* CPS1: SSP is 0 in ROM, game init sets it up. Force valid stack. */
        g_m68k.ssp = 0x00FFFFFC;
        g_m68k.a[7] = 0x00FFFFFC;
        m68k_set_sr(0x2700);  /* Supervisor mode, interrupts masked */
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

    /*
     * Hardware initialization.
     *
     * SF2's init code uses a custom calling convention (LEA+BRA with A4
     * as return register) that the recompiler can't follow. Instead of
     * running the init chain, we apply the known register values that
     * SF2's init code would write to the CPS-A registers.
     *
     * Values extracted from disassembly of $00040E-$0004A6:
     */
    printf("[cps1] Applying SF2 hardware init...\n"); fflush(stdout);
    {
        /* CPS-A register writes from the init code */
        bus_write8(0x800030, 0x80);    /* Reset pulse */
        bus_write8(0x800030, 0x00);    /* Release reset */
        bus_write8(0x800181, 0xF0);    /* Sound latch init */

        /* GFX RAM layout */
        bus_write16(0x80010C, 0xFFC0); /* Scroll 1 X offset */
        bus_write16(0x80010E, 0x0000); /* Scroll 1 Y offset */
        bus_write16(0x800100, 0x9100); /* Scroll 1 base */
        bus_write16(0x800102, 0x90C0); /* Scroll 2 base */
        bus_write16(0x800104, 0x9040); /* Scroll 3 base */
        bus_write16(0x800106, 0x9080); /* Sprite base */
        bus_write16(0x800108, 0x9200); /* Other/palette base */
        bus_write16(0x80010A, 0x9000); /* Palette control */

        /* CPS-B registers from init */
        bus_write16(0x800154, 0x12C8); /* CPS-B ID / config */
        bus_write16(0x800122, 0x003E); /* Layer enable */
        bus_write16(0x80014A, 0x003F); /* Priority mask */

        /* Set up scroll offsets for layers 2 and 3 */
        bus_write16(0x800110, 0x0000); /* Scroll 2 X */
        bus_write16(0x800112, 0x0000); /* Scroll 2 Y */
        bus_write16(0x800114, 0x0000); /* Scroll 3 X */
        bus_write16(0x800116, 0x0000); /* Scroll 3 Y */

        /* Initialize Work RAM stack area */
        g_m68k.a[7] = 0x00FFFFFC;

        printf("[cps1] CPS-A/B registers configured\n"); fflush(stdout);

        /*
         * Write test pattern to GFX RAM + palette to verify renderer.
         *
         * Palette: write some visible colors at palette base ($920000).
         * Scroll 2 tilemap: write tile entries at scroll2 base ($90C000).
         * The tiles reference GFX ROM data, so we need tile numbers that
         * correspond to actual non-empty tiles in the decoded GFX data.
         */
        printf("[cps1] Writing test pattern...\n"); fflush(stdout);

        /* Set up palette 0 with visible colors */
        /* Palette is at GFX RAM offset $20000 (bus addr $920000) */
        /* Color format: ----RRRR GGGGBBBB (12-bit RGB) */
        bus_write16(0x920000, 0x0000);  /* Color 0: transparent/black */
        bus_write16(0x920002, 0x0F00);  /* Color 1: red */
        bus_write16(0x920004, 0x00F0);  /* Color 2: green */
        bus_write16(0x920006, 0x000F);  /* Color 3: blue */
        bus_write16(0x920008, 0x0FF0);  /* Color 4: yellow */
        bus_write16(0x92000A, 0x0F0F);  /* Color 5: magenta */
        bus_write16(0x92000C, 0x00FF);  /* Color 6: cyan */
        bus_write16(0x92000E, 0x0FFF);  /* Color 7: white */
        bus_write16(0x920010, 0x0888);  /* Color 8: gray */
        bus_write16(0x920012, 0x0F80);  /* Color 9: orange */
        /* Set palette 1 as well */
        for (int i = 0; i < 16; i++) {
            uint16_t c = bus_read16(0x920000 + i * 2);
            bus_write16(0x920020 + i * 2, c);
        }

        /*
         * Write scroll 2 tilemap entries.
         * Scroll 2 base = $90C0 -> GFX RAM offset $0C000 (bus $90C000).
         * Each entry is 2 words (4 bytes):
         *   Word 0: tile number (16-bit)
         *   Word 1: palette[15:11] | flip_y[5] | flip_x[4] | priority[3:0]
         *
         * Use tile numbers 1-100 which should hit actual GFX ROM data.
         * Layout: 64 columns * 64 rows, column-major.
         */
        for (int col = 0; col < 25; col++) {
            for (int row = 0; row < 15; row++) {
                uint32_t entry_addr = 0x90C000 + (uint32_t)(col * 64 + row) * 4;
                uint16_t tile_num = (uint16_t)((col * 15 + row + 1) & 0xFFFF);
                /* Palette 0, no flip */
                bus_write16(entry_addr, tile_num);
                bus_write16(entry_addr + 2, 0x0000);
            }
        }

        printf("[cps1] Test pattern written (scroll2 tiles + palette)\n");
        fflush(stdout);
    }

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

        /* Run VBlank handler */
        if (vblank_addr && func_table_lookup(vblank_addr)) {
            func_table_call(vblank_addr);
        }

        /* Render + present */
        video_render_frame(s_framebuffer);
        platform_present(s_framebuffer);
        if (!platform_poll_input()) exit(0);
        platform_frame_sync();

        /* Dump framebuffer to BMP on frame 1 for debugging */
        if (frame == 1) {
            FILE *bmp = fopen("sf2_frame.bmp", "wb");
            if (bmp) {
                /* Write minimal BMP header (384x224, 32bpp) */
                int w = CPS1_SCREEN_WIDTH, h = CPS1_SCREEN_HEIGHT;
                int row_bytes = w * 4;
                int img_size = row_bytes * h;
                int file_size = 54 + img_size;
                uint8_t hdr[54] = {0};
                hdr[0]='B'; hdr[1]='M';
                hdr[2]=file_size; hdr[3]=file_size>>8; hdr[4]=file_size>>16; hdr[5]=file_size>>24;
                hdr[10]=54;
                hdr[14]=40; /* DIB header size */
                hdr[18]=w; hdr[19]=w>>8;
                hdr[22]=h; hdr[23]=h>>8; /* positive = bottom-up */
                hdr[26]=1; /* planes */
                hdr[28]=32; /* bpp */
                hdr[34]=img_size; hdr[35]=img_size>>8; hdr[36]=img_size>>16; hdr[37]=img_size>>24;
                fwrite(hdr, 1, 54, bmp);
                /* BMP is bottom-up, ARGB -> BGRA */
                for (int y = h - 1; y >= 0; y--) {
                    for (int x = 0; x < w; x++) {
                        uint32_t px = s_framebuffer[y * w + x];
                        uint8_t bgra[4] = {
                            (uint8_t)(px),         /* B */
                            (uint8_t)(px >> 8),    /* G */
                            (uint8_t)(px >> 16),   /* R */
                            (uint8_t)(px >> 24)    /* A */
                        };
                        fwrite(bgra, 1, 4, bmp);
                    }
                }
                fclose(bmp);
                printf("[frame %d] Saved framebuffer to sf2_frame.bmp\n", frame);
                fflush(stdout);
            }
        }

        if (frame < 3 || frame % 600 == 0) {
            printf("[frame %d]\n", frame); fflush(stdout);
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
