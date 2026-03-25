/*
 * cps1recomp.c — Main CPS1 runtime orchestrator.
 */

#include <cps1recomp/cps1recomp.h>
#include <cps1recomp/palette.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_framebuffer[CPS1_SCREEN_WIDTH * CPS1_SCREEN_HEIGHT];
static bool s_initialized = false;
static int s_frame_count = 0;

/*
 * VBlank hook: called when the game's main loop reads the VBlank flag.
 * We render the current frame, present it, poll input, and set the
 * VBlank flag so the game loop continues processing.
 */
static void cps1_vblank_hook(void) {
    if (s_frame_count == 0) {
        printf("[hook] VBlank hook fired! A5=$%08X\n", g_m68k.a[5]);
        fflush(stdout);
    }
    /* Render current state */
    video_render_frame(s_framebuffer);
    platform_present(s_framebuffer);

    /* Poll input */
    if (!platform_poll_input()) {
        cps1_shutdown();
        exit(0);
    }

    /* Frame sync */
    platform_frame_sync();

    /* Set the VBlank flag so the game's main loop proceeds */
    bus_wram_write8(0x020E, 0xFF);

    /* Capture frames as BMP + diagnostic dump */
    if (s_frame_count == 600 || s_frame_count == 1200) {
        FILE *bmp = fopen("sf2_frame.bmp", "wb");
        if (bmp) {
            int w = CPS1_SCREEN_WIDTH, h = CPS1_SCREEN_HEIGHT;
            int img_size = w * h * 4;
            int file_size = 54 + img_size;
            uint8_t hdr[54] = {0};
            hdr[0]='B'; hdr[1]='M';
            hdr[2]=file_size; hdr[3]=file_size>>8; hdr[4]=file_size>>16; hdr[5]=file_size>>24;
            hdr[10]=54; hdr[14]=40;
            hdr[18]=w; hdr[19]=w>>8; hdr[22]=h; hdr[23]=h>>8;
            hdr[26]=1; hdr[28]=32;
            hdr[34]=img_size; hdr[35]=img_size>>8; hdr[36]=img_size>>16; hdr[37]=img_size>>24;
            fwrite(hdr, 1, 54, bmp);
            for (int y = h - 1; y >= 0; y--) {
                for (int x = 0; x < w; x++) {
                    uint32_t px = s_framebuffer[y * w + x];
                    uint8_t bgra[4] = { (uint8_t)(px), (uint8_t)(px>>8), (uint8_t)(px>>16), (uint8_t)(px>>24) };
                    fwrite(bgra, 1, 4, bmp);
                }
            }
            fclose(bmp);
        }
        /* Dump CPS-A registers, tilemap entries, palette */
        FILE *df = fopen("sf2_diag.txt", "w");
        if (df) {
            fprintf(df, "=== Frame %d diagnostic ===\n", s_frame_count);
            /* Game state from Work RAM (A5=$FF8000) */
            uint8_t *wram = bus_get_wram_ptr();
            uint16_t attract_state = ((uint16_t)wram[0x8000] << 8) | wram[0x8001];
            uint8_t f5d59 = wram[0x8000 + 0x5d59];
            uint8_t f5d56 = wram[0x8000 + 0x5d56];
            fprintf(df, "Game: attract_state=%u 5D59=%u 5D56=%u\n", attract_state, f5d59, f5d56);
            /* Active task slots */
            int active = 0;
            for (int i = 0; i < 16; i++) {
                uint8_t st = wram[i * 0x20];
                if (st) { active++; fprintf(df, "  slot%d: status=$%02X\n", i, st); }
            }
            fprintf(df, "CPS-A registers:\n");
            for (int i = 0; i < 16; i++) {
                uint16_t val = video_read_cps_a(0x100 + i*2);
                fprintf(df, "  $8001%02X = $%04X\n", i*2, val);
            }
            /* Scroll base addresses */
            uint16_t s1_base = video_read_cps_a(0x100);
            uint16_t s2_base = video_read_cps_a(0x102);
            uint16_t s3_base = video_read_cps_a(0x104);
            uint16_t obj_base = video_read_cps_a(0x106);
            uint16_t pal_base = video_read_cps_a(0x108);
            uint16_t other_base = video_read_cps_a(0x10A);
            fprintf(df, "\nDerived GFX RAM offsets:\n");
            fprintf(df, "  Scroll1 base: $%04X -> $%05X\n", s1_base, ((uint32_t)s1_base << 8) % 0x30000);
            fprintf(df, "  Scroll2 base: $%04X -> $%05X\n", s2_base, ((uint32_t)s2_base << 8) % 0x30000);
            fprintf(df, "  Scroll3 base: $%04X -> $%05X\n", s3_base, ((uint32_t)s3_base << 8) % 0x30000);
            fprintf(df, "  Object base:  $%04X -> $%05X\n", obj_base, ((uint32_t)obj_base << 8) % 0x30000);
            fprintf(df, "  Palette base: $%04X -> $%05X\n", pal_base, ((uint32_t)pal_base << 8) % 0x30000);
            fprintf(df, "  Other base:   $%04X -> $%05X\n", other_base, ((uint32_t)other_base << 8) % 0x30000);
            /* Scroll offsets */
            fprintf(df, "\nScroll offsets:\n");
            fprintf(df, "  Scroll1 X=$%04X Y=$%04X\n", video_read_cps_a(0x10C), video_read_cps_a(0x10E));
            fprintf(df, "  Scroll2 X=$%04X Y=$%04X\n", video_read_cps_a(0x110), video_read_cps_a(0x112));
            fprintf(df, "  Scroll3 X=$%04X Y=$%04X\n", video_read_cps_a(0x114), video_read_cps_a(0x116));
            /* Sample tilemap entries */
            uint32_t s1_off = ((uint32_t)s1_base << 8) % 0x30000;
            fprintf(df, "\nScroll1 tilemap (first 8 entries at $%05X):\n", s1_off);
            for (int i = 0; i < 8; i++) {
                uint16_t w0 = video_gfxram_read(s1_off + i*4);
                uint16_t w1 = video_gfxram_read(s1_off + i*4 + 2);
                fprintf(df, "  [%d] tile=$%04X attr=$%04X\n", i, w0, w1);
            }
            uint32_t s2_off = ((uint32_t)s2_base << 8) % 0x30000;
            fprintf(df, "\nScroll2 tilemap (first 8 entries at $%05X):\n", s2_off);
            for (int i = 0; i < 8; i++) {
                uint16_t w0 = video_gfxram_read(s2_off + i*4);
                uint16_t w1 = video_gfxram_read(s2_off + i*4 + 2);
                fprintf(df, "  [%d] tile=$%04X attr=$%04X\n", i, w0, w1);
            }
            /* Palette data from CPS_A_OTHER_BASE register ($800108) */
            uint16_t pal_reg = video_read_cps_a(0x108);
            uint32_t p_off = ((uint32_t)pal_reg << 8) % 0x30000;
            fprintf(df, "\nPalette (first 32 colors at $%05X):\n", p_off);
            for (int i = 0; i < 32; i++) {
                uint16_t c = video_gfxram_read(p_off + i*2);
                fprintf(df, "  [%2d] $%04X", i, c);
                if ((i & 7) == 7) fprintf(df, "\n");
            }
            /* Scan GFX RAM for non-zero data in palette area */
            int nz_count = 0;
            uint32_t first_nz = 0;
            for (uint32_t i = 0x20000; i < 0x30000; i += 2) {
                uint16_t v = video_gfxram_read(i);
                if (v != 0) {
                    if (nz_count == 0) first_nz = i;
                    nz_count++;
                }
            }
            fprintf(df, "\nPalette area $20000-$2FFFF: %d non-zero words", nz_count);
            if (nz_count > 0) fprintf(df, " (first at $%05X)", first_nz);
            fprintf(df, "\n");
            /* Also check where palette data might actually be */
            for (uint32_t base = 0; base < 0x30000; base += 0x4000) {
                int nz = 0;
                for (uint32_t i = base; i < base + 0x4000 && i < 0x30000; i += 2) {
                    if (video_gfxram_read(i) != 0) nz++;
                }
                fprintf(df, "  GFX RAM $%05X-$%05X: %d non-zero words\n", base, base + 0x3FFF, nz);
            }
            fprintf(df, "\n");
            fclose(df);
            printf("[frame %d] Saved sf2_frame.bmp + sf2_diag.txt\n", s_frame_count);
        }
    }
    if (s_frame_count < 3 || s_frame_count % 600 == 0) {
        printf("[frame %d]\n", s_frame_count); fflush(stdout);
    }
    s_frame_count++;
}

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
    }

    /* Run the full init chain from entry point.
     * The LEA+BRA fix lets the init subroutines work correctly.
     * After init, we call sub_000910 which is the main loop setup
     * that initializes task slots and enters the game loop. */
    /* Run init chain — call entry point and fall-through functions */
    printf("[cps1] Running init...\n"); fflush(stdout);

    /* Set up A5 and A7 as the init code would */
    g_m68k.a[5] = 0xFF8000;
    g_m68k.a[7] = 0xFF0000;

    /* Run entry_point and fall-through init functions */
    uint32_t init_addrs[] = {
        0x00040E, 0x00041C, 0x000426, 0x000448, 0x000476,
        0x000500, 0x000512,
    };
    for (int i = 0; i < (int)(sizeof(init_addrs)/sizeof(init_addrs[0])); i++) {
        cps1_func_t fn = func_table_lookup(init_addrs[i]);
        if (fn) fn();
    }
    printf("[cps1] Init functions done\n"); fflush(stdout);

    /*
     * Install VBlank hook: when the game's main loop reads the VBlank
     * flag (at Work RAM offset $020E), we render a frame, present it,
     * poll input, and set the flag so the game continues.
     *
     * This turns the game's infinite VBlank-wait loop into our frame loop.
     */
    bus_set_vblank_hook(cps1_vblank_hook);

    printf("[cps1] VBlank hook installed\n"); fflush(stdout);

    /* Call $0006A6's remaining init (scroll register setup) without
     * the JMP $910 at the end — we handle that ourselves */
    {
        /* $0006A6 sets up scroll shadow registers in Work RAM.
         * We replicate the essential writes. */
        uint8_t *wram = bus_get_wram_ptr();
        /* A5-relative offsets (A5=$FF8000, so wram offset = A5_offset + $8000) */
        #define WR16(off, val) do { wram[0x8000+(off)] = (uint8_t)((val)>>8); wram[0x8000+(off)+1] = (uint8_t)(val); } while(0)
        WR16(0x2A, 0x9100);  /* scroll1 base shadow */
        WR16(0x2C, 0x90C0);  /* scroll2 base shadow */
        WR16(0x2E, 0x9040);  /* scroll3 base shadow */
        WR16(0x30, 0x9080);  /* sprite base shadow */
        WR16(0x32, 0x9200);  /* palette base shadow */
        WR16(0x34, 0x9000);  /* other base shadow */
        WR16(0x4C, 0x003F);
        WR16(0x52, 0x12DA);
        WR16(0x5C, 0x003F);
        #undef WR16
    }

    printf("[cps1] Calling main loop ($000910)...\n"); fflush(stdout);

    /* This call enters the game's infinite main loop.
     * The VBlank hook fires each time the game reads the VBlank flag,
     * rendering and presenting a frame. This call never returns. */
    if (func_table_lookup(0x000910)) {
        func_table_call(0x000910);
    }

    printf("[cps1] Main loop returned (unexpected)\n"); fflush(stdout);

    /* Fallback frame loop */
    int frame = 0;
    while (true) {
        video_render_frame(s_framebuffer);
        platform_present(s_framebuffer);
        if (!platform_poll_input()) exit(0);
        platform_frame_sync();

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
