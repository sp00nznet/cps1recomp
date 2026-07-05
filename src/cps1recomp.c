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
/* Run the sound hardware for one frame: step the Z80 sound CPU (which drives
 * the YM2151/OKI and advances the YM timer that paces its own IRQ), then mix a
 * frame of audio and queue it to the host. Called once per displayed frame. */
static void cps1_run_sound_frame(void) {
    /* Run Z80 for one frame (3.579545 MHz / 59.63 Hz). The YM2151 timer IRQ is
     * advanced inside z80_execute via ym2151_tick. */
    z80_execute(60192);

    /* ~735 samples per frame at 44100 Hz / 59.63 Hz */
    #define SAMPLES_PER_FRAME 735
    int16_t ym_buf[SAMPLES_PER_FRAME * 2];     /* Stereo */
    int16_t oki_buf[SAMPLES_PER_FRAME];        /* Mono   */
    int16_t mix_buf[SAMPLES_PER_FRAME * 2];    /* Mixed stereo */

    ym2151_generate(ym_buf, SAMPLES_PER_FRAME);
    oki6295_generate(oki_buf, SAMPLES_PER_FRAME);

    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
        int32_t l = (int32_t)ym_buf[i * 2 + 0] + (int32_t)oki_buf[i];
        int32_t r = (int32_t)ym_buf[i * 2 + 1] + (int32_t)oki_buf[i];
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        mix_buf[i * 2 + 0] = (int16_t)l;
        mix_buf[i * 2 + 1] = (int16_t)r;
    }
    /* num_samples is the stereo-frame count (queue multiplies by 2ch * 2 bytes). */
    platform_audio_queue(mix_buf, SAMPLES_PER_FRAME);

    /* Objective bring-up check: report the audio peak so silence is detectable. */
    static int s_snd_frame = 0;
    if (++s_snd_frame % 600 == 0) {
        int peak = 0;
        for (int i = 0; i < SAMPLES_PER_FRAME * 2; i++) {
            int a = mix_buf[i] < 0 ? -mix_buf[i] : mix_buf[i];
            if (a > peak) peak = a;
        }
        printf("[snd] frame %d: audio peak=%d\n", s_snd_frame, peak);
        fflush(stdout);
    }
}

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

    /* Run the sound CPU + mix one frame of audio */
    cps1_run_sound_frame();

    /* Set the VBlank flag so the game's main loop proceeds */
    bus_wram_write8(0x020E, 0xFF);

    /* Capture frames as BMP + diagnostic dump */
    if (s_frame_count == 600 || s_frame_count == 1700) {
        const char *fn = (s_frame_count == 600) ? "shot_title.bmp" : "shot_select.bmp";
        FILE *bmp = fopen(fn, "wb");
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
        FILE *df = fopen((s_frame_count == 600) ? "diag_title.txt" : "diag_select.txt", "w");
        if (df) {
            fprintf(df, "=== Frame %d diagnostic ===\n", s_frame_count);
            /* Game state from Work RAM (A5=$FF8000) */
            uint8_t *wram = bus_get_wram_ptr();
            uint16_t attract_state = ((uint16_t)wram[0x8000] << 8) | wram[0x8001];
            uint8_t f5d59 = wram[0x8000 + 0x5d59];
            uint8_t f5d56 = wram[0x8000 + 0x5d56];
            uint8_t f2d7 = wram[0x8000 + 0x2d7];
            uint8_t f2e1 = wram[0x8000 + 0x2e1];
            uint8_t f8c = wram[0x8000 + 0x8c];
            uint8_t f2e0 = wram[0x8000 + 0x2e0];
            fprintf(df, "Game: attract_state=%u 5D59=%u 5D56=%u 2D7=%u 2E0=%u 2E1=%u 8C=$%02X\n",
                    attract_state, f5d59, f5d56, f2d7, f2e0, f2e1, f8c);
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
            /* Control regs + WRAM shadow registers (A5-relative $2A-$5C) */
            fprintf(df, "Ctrl: $800122(layerEn,CPSA)=$%04X CPSB$140=$%04X $14A=$%04X $154=$%04X\n",
                    video_read_cps_a(0x122), video_read_cps_b(0x000),
                    video_read_cps_b(0x00A), video_read_cps_b(0x014));
            fprintf(df, "WRAM shadows (A5+off): ");
            for (uint32_t off = 0x2A; off <= 0x5C; off += 2) {
                uint16_t v = ((uint16_t)wram[0x8000+off] << 8) | wram[0x8000+off+1];
                fprintf(df, "$%02X=%04X ", off, v);
            }
            fprintf(df, "\n");
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
            fprintf(df, "\nScroll2 tilemap (non-$4020 entries at $%05X):\n", s2_off);
            int s2_nonstd = 0;
            for (int i = 0; i < 4096; i++) {
                uint16_t w0 = video_gfxram_read(s2_off + i*4);
                if (w0 != 0x4020 && w0 != 0x0000) {
                    if (s2_nonstd < 16) {
                        uint16_t w1 = video_gfxram_read(s2_off + i*4 + 2);
                        fprintf(df, "  [%d] tile=$%04X attr=$%04X (off=$%04X)\n",
                                i, w0, w1, i*4);
                    }
                    s2_nonstd++;
                }
            }
            fprintf(df, "  Total non-standard entries: %d\n", s2_nonstd);
            /* Scroll2/3 tilemap codes via the SWIZZLED scan the renderer uses. */
            uint32_t s2b = ((uint32_t)video_read_cps_a(0x104) << 8) % 0x30000;
            uint32_t s3b = ((uint32_t)video_read_cps_a(0x106) << 8) % 0x30000;
            fprintf(df, "\nScroll2 codes (base $%05X) row0 cols0-11 [swizzled]:\n ", s2b);
            for (int c = 0; c < 12; c++) {
                uint32_t sc = ((0&0x0f)|((c&0x3f)<<4)|((0&0x30)<<6));
                fprintf(df, " $%04X", video_gfxram_read(s2b + sc*4));
            }
            /* Scan scroll3 region ($8000-$BFFF) for non-zero tile codes. */
            int s3nz = 0; uint32_t s3first = 0; uint16_t s3firstcode = 0;
            for (uint32_t o = s3b; o < s3b + 0x4000; o += 4) {
                uint16_t c = video_gfxram_read(o);
                if (c != 0) { if (s3nz == 0) { s3first = o; s3firstcode = c; } s3nz++; }
            }
            fprintf(df, "\nScroll3 ($%05X): %d non-zero entries; first @%05X code=$%04X\n",
                    s3b, s3nz, s3first, s3firstcode);
            fprintf(df, "Scroll3 sample (linear entries 0,64,128,...,15*64):\n ");
            for (int i = 0; i < 16; i++)
                fprintf(df, " $%04X", video_gfxram_read(s3b + (i*64)*4));
            fprintf(df, "\n");
            /* Sprite/object table (first 16 entries, 8 bytes each) */
            uint32_t oo = ((uint32_t)video_read_cps_a(0x100) << 8) % 0x30000;
            fprintf(df, "\nObject table (first 16 at $%05X) X Y code attr:\n", oo);
            for (int i = 0; i < 16; i++)
                fprintf(df, "  [%d] %04X %04X %04X %04X\n", i,
                        video_gfxram_read(oo+i*8), video_gfxram_read(oo+i*8+2),
                        video_gfxram_read(oo+i*8+4), video_gfxram_read(oo+i*8+6));
            /* Palette data from CPS_A_OTHER_BASE register ($800108) */
            uint16_t pal_reg = video_read_cps_a(0x10A);  /* renderer's actual palette base */
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
    /*
     * Install the VBlank hook BEFORE running init. With the recompiler's
     * LEA+BRA continuation, interior-jump-table, and fall-through fixes, the
     * real entry point at $00040E now executes the entire init chain and flows
     * straight into the game's main loop ($000910) — which spins waiting on the
     * VBlank flag. The hook turns that wait into our frame loop, so it must be
     * live before the chain reaches the main loop.
     */
    bus_set_vblank_hook(cps1_vblank_hook);
    printf("[cps1] VBlank hook installed\n"); fflush(stdout);

    /* Initial CPU state the reset/init code expects (the ROM's reset SSP is 0;
     * the real init sets A5 = system-work base and the stack pointer). */
    g_m68k.a[5] = 0x00FF8000;
    g_m68k.a[7] = 0x00FFFFFC;
    g_m68k.ssp  = 0x00FFFFFC;

    printf("[cps1] Running REAL init chain from $00040E...\n"); fflush(stdout);
    if (func_table_lookup(0x00040E)) {
        func_table_call(0x00040E);   /* real init: runs the setup chain, returns */
    }
    printf("[cps1] Real init returned; entering main loop ($000910)...\n"); fflush(stdout);

    /* Bridge: the real init configures the scroll2/3/sprite CPS-A bases but does
     * not yet set the WRAM scroll-shadow registers (A5+$2A..$5C) that the game's
     * per-frame handler copies into the scroll1/palette CPS regs — so without
     * this they get written as $0000 each frame and the screen is garbage. Until
     * the real shadow-setup path is identified, seed them as the init would. */
    {
        uint8_t *wram = bus_get_wram_ptr();
        #define WR16(off, val) do { wram[0x8000+(off)] = (uint8_t)((val)>>8); wram[0x8000+(off)+1] = (uint8_t)(val); } while(0)
        WR16(0x2A, 0x9100);  /* scroll1 base shadow  */
        WR16(0x2C, 0x90C0);  /* scroll2 base shadow  */
        WR16(0x2E, 0x9040);  /* scroll3 base shadow  */
        WR16(0x30, 0x9080);  /* sprite base shadow   */
        WR16(0x32, 0x9200);  /* palette base shadow  */
        WR16(0x34, 0x9000);  /* other base shadow    */
        WR16(0x4C, 0x003F);
        WR16(0x52, 0x12DA);
        WR16(0x5C, 0x003F);
        #undef WR16
    }
    bus_write16(0x800154, 0x12C8); /* CPS-B config */
    bus_write16(0x80014A, 0x003F); /* CPS-B priority mask */
    bus_write16(0x800122, 0x003E); /* layer enable */

    /* Enter the game's main loop. The VBlank hook drives it frame-by-frame. */
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

    /* Sound CPU + audio for this frame */
    cps1_run_sound_frame();
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
