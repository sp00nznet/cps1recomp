/*
 * cps1recomp.h — Main header for the CPS1 static recompilation runtime.
 *
 * Top-level include for game projects. Pulls in the full runtime API:
 * CPU context, bus, video, audio, input, and platform.
 *
 * Usage:
 *   #include <cps1recomp/cps1recomp.h>
 *
 *   int main(int argc, char *argv[]) {
 *       cps1_init(&(cps1_config_t){
 *           .rom_path = argv[1],
 *           .window_scale = 3,
 *       });
 *
 *       // Register recompiled functions
 *       func_table_register(0x000200, func_000200);
 *       // ...
 *
 *       cps1_run();
 *       return 0;
 *   }
 */

#ifndef CPS1RECOMP_H
#define CPS1RECOMP_H

#include "m68k.h"
#include "bus.h"
#include "func_table.h"
#include "video.h"
#include "palette.h"
#include "io.h"
#include "ym2151.h"
#include "oki6295.h"
#include "z80.h"
#include "timer.h"
#include "rom.h"
#include "platform.h"
#include "debug.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Configuration ----- */

typedef struct {
    const char *rom_path;       /* Path to ROM zip or directory */
    int window_scale;           /* Window size multiplier (1 = 384x224) */
    bool fullscreen;
    bool vsync;
} cps1_config_t;

/* ----- Lifecycle ----- */

int cps1_init(const cps1_config_t *config);
void cps1_run(void);
void cps1_shutdown(void);

/* ----- Frame Hooks ----- */

void cps1_begin_frame(void);
void cps1_trigger_vblank(void);
void cps1_end_frame(void);

/* ----- Version ----- */

#define CPS1RECOMP_VERSION_MAJOR 0
#define CPS1RECOMP_VERSION_MINOR 1
#define CPS1RECOMP_VERSION_PATCH 0

const char *cps1_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_H */
