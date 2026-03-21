/*
 * debug.c — Debug facilities implementation.
 *
 * Only compiled when CPS1RECOMP_DEBUG is defined.
 */

#ifdef CPS1RECOMP_DEBUG

#include <cps1recomp/debug.h>
#include <cps1recomp/m68k.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static bool s_trace_enabled = false;
static FILE *s_trace_file = NULL;

#define MAX_BREAKPOINTS 64
static uint32_t s_breakpoints[MAX_BREAKPOINTS];
static int s_num_breakpoints = 0;

int debug_init(void) {
    s_trace_enabled = false;
    s_num_breakpoints = 0;
    return 0;
}

void debug_shutdown(void) {
    if (s_trace_file) {
        fclose(s_trace_file);
        s_trace_file = NULL;
    }
}

void debug_trace_enable(bool enabled) {
    s_trace_enabled = enabled;
    if (enabled && !s_trace_file) {
        s_trace_file = fopen("cps1_trace.log", "w");
    }
}

void debug_trace_call(uint32_t addr, const char *name) {
    if (!s_trace_enabled) return;
    if (s_trace_file) {
        if (name)
            fprintf(s_trace_file, "CALL $%06X (%s)\n", addr, name);
        else
            fprintf(s_trace_file, "CALL $%06X\n", addr);
    }
}

void debug_trace_mem_read(uint32_t addr, uint32_t val, int size) {
    if (!s_trace_enabled) return;
    if (s_trace_file) {
        fprintf(s_trace_file, "READ%d $%06X = $%0*X\n",
                size * 8, addr, size * 2, val);
    }
}

void debug_trace_mem_write(uint32_t addr, uint32_t val, int size) {
    if (!s_trace_enabled) return;
    if (s_trace_file) {
        fprintf(s_trace_file, "WRITE%d $%06X = $%0*X\n",
                size * 8, addr, size * 2, val);
    }
}

void debug_add_breakpoint(uint32_t addr) {
    if (s_num_breakpoints < MAX_BREAKPOINTS) {
        s_breakpoints[s_num_breakpoints++] = addr;
    }
}

void debug_remove_breakpoint(uint32_t addr) {
    for (int i = 0; i < s_num_breakpoints; i++) {
        if (s_breakpoints[i] == addr) {
            s_breakpoints[i] = s_breakpoints[--s_num_breakpoints];
            return;
        }
    }
}

bool debug_check_breakpoint(uint32_t addr) {
    for (int i = 0; i < s_num_breakpoints; i++) {
        if (s_breakpoints[i] == addr) return true;
    }
    return false;
}

void debug_dump_cpu_state(void) {
    printf("=== 68K CPU State ===\n");
    for (int i = 0; i < 8; i++)
        printf("  D%d=%08X  A%d=%08X\n", i, g_m68k.d[i], i, g_m68k.a[i]);
    printf("  PC=%08X  SR=%04X\n", g_m68k.pc, m68k_get_sr());
    printf("  Flags: %c%c%c%c%c\n",
           g_m68k.flag_x ? 'X' : '-', g_m68k.flag_n ? 'N' : '-',
           g_m68k.flag_z ? 'Z' : '-', g_m68k.flag_v ? 'V' : '-',
           g_m68k.flag_c ? 'C' : '-');
}

void debug_dump_gfxram(uint32_t start, uint32_t count) {
    printf("=== GFX RAM $%06X +%u ===\n", start, count);
    /* TODO: hex dump */
}

void debug_dump_palette(int index) {
    printf("=== Palette %d ===\n", index);
    /* TODO: color dump */
}

void debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

#endif /* CPS1RECOMP_DEBUG */
