# cps1recomp -- CPS1 Static Recompilation Toolkit

**Drop-in CPS1 arcade hardware for your static recompilation project. No emulator. Just Capcom's finest, running natively.**

Ever wanted to take a CPS1 classic and run it natively on modern hardware? Street Fighter II, Final Fight, Ghouls 'n Ghosts, Strider -- the entire CPS1 library is built on the same Motorola 68000 CPU and custom ASIC architecture. This toolkit gives you everything you need to bring any of them back to life as native executables.

## What Is This?

cps1recomp is a **reusable runtime library** for building CPS1 static recompilation projects. It provides native replacements for every piece of CPS1 hardware:

| CPS1 Hardware | What It Does | Our Replacement |
|---------------|-------------|-----------------|
| **Motorola 68000** @ 10 MHz | Main CPU | `m68k.h` -- Complete register file + instruction macros (ADD, SUB, CMP, MUL, DIV, shifts, rotates, all 16 condition codes) |
| **CPS-A / CPS-B ASICs** | Video controller | `video.c` -- Software renderer: scroll1 (8x8), scroll2 (16x16), scroll3 (32x32) layers + 256 sprites (single & multi-tile), layer-enable, tile flipping |
| **Palette RAM** | 192 palettes x 16 colors | `palette.c` -- 12-bit RGB to ARGB8888 conversion |
| **Zilog Z80** @ 3.579 MHz | Sound CPU | `z80.c` -- Interpreted Z80 core |
| **Yamaha YM2151** (OPM) | FM synthesis | `ym2151.c` -- [ymfm](https://github.com/aaronsgiles/ymfm) wrapper |
| **OKI MSM6295** | ADPCM samples | `oki6295.c` -- 4-channel ADPCM decoder |
| **Memory Bus** | Address decoding | `bus.c` -- Full CPS1 memory map ($000000-$FFFFFF) |
| **Input Hardware** | Joystick + buttons | `io.c` -- 6-button fighter layout via SDL2 |

Think of it like [genrecomp](https://github.com/sp00nznet/genrecomp) for Genesis or [neogeorecomp](https://github.com/sp00nznet/neogeorecomp) for Neo Geo, but for Capcom's legendary arcade platform.

## How It Works

```
 Your CPS1 ROM (.zip)
        |
        v
 tools/extract_roms.py        # Assemble 68K program, decode GFX ROMs
        |
        v
 tools/analyze_rom.py         # Recursive descent M68K disassembly
        |                      # Function discovery, jump table scanning
        v
 tools/generate_recomp.py     # M68K instructions -> C macros
        |                      # (M68K_ADD16, M68K_CMP32, bus_read16...)
        v
 ┌──────────────┐
 │ recomp_*.c   │──── links against ──── cps1recomp.lib
 │ (your game)  │                         (this project)
 └──────┬───────┘
        |
        v
   game.exe   <-- Native x86-64 executable. No emulator.
```

## The CPS1 Library

CPS1 powered some of the most iconic arcade games ever made:

| Year | Game | Status |
|------|------|--------|
| 1991 | **Street Fighter II: The World Warrior** | [In Progress](https://github.com/sp00nznet/sf2) -- boots through attract -> coin -> START -> **character select** |
| 1989 | Final Fight | Planned |
| 1989 | Ghouls 'n Ghosts | Planned |
| 1989 | Strider | Planned |
| 1990 | Carrier Air Wing | - |
| 1991 | Captain Commando | - |
| 1991 | Knights of the Round | - |
| 1992 | Street Fighter II': Champion Edition | - |
| 1993 | Street Fighter II': Hyper Fighting | - |
| 1992 | Saturday Night Slam Masters | - |

All of these run on the same hardware and can be recompiled using this toolkit. SF2 is the proving ground -- once it's running, the rest follow.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Your CPS1 Game ROM                     │
│              Motorola 68000 machine code                │
└───────────────────────┬─────────────────────────────────┘
                        │ static recompilation
                        v
┌─────────────────────────────────────────────────────────┐
│                  Recompiled C Source                     │
│       bus_write16(0xFF8000, (uint16_t)g_m68k.d[0]);     │
│       M68K_ADD16(g_m68k.d[0], 1);                       │
│       if (M68K_CC_EQ) goto idle;                        │
└───────────────────────┬─────────────────────────────────┘
                        │ links against
                        v
┌─────────────────────────────────────────────────────────┐
│                    cps1recomp runtime                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐  │
│  │  m68k.h  │ │  bus.c   │ │ video.c  │ │ ym2151.c  │  │
│  │ CPU ctx  │ │ CPS1 mem │ │ scroll   │ │  OPM FM   │  │
│  │ + macros │ │ map+I/O  │ │ + sprite │ │  (ymfm)   │  │
│  └──────────┘ └──────────┘ └──────────┘ └───────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐  │
│  │palette.c │ │  z80.c   │ │oki6295.c│ │platform.c │  │
│  │ 12-bit   │ │ sound CPU│ │  ADPCM   │ │   SDL2    │  │
│  │ RGB      │ │ interp   │ │ 4-chan   │ │  window   │  │
│  └──────────┘ └──────────┘ └──────────┘ └───────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- **Windows 11** (Linux/macOS should work too)
- **CMake** 3.16+
- **SDL2** (`vcpkg install sdl2:x64-windows`)
- **Visual Studio 2022** or compatible C17 compiler
- **Python 3.8+** with `capstone` (`pip install capstone`)

### Using in Your Project

```cmake
# In your game project's CMakeLists.txt:
add_subdirectory(path/to/cps1recomp)
target_link_libraries(my_game PRIVATE cps1recomp SDL2::SDL2main)
```

```c
#include <cps1recomp/cps1recomp.h>

int main(int argc, char *argv[]) {
    cps1_init(&(cps1_config_t){
        .rom_path = "build/",      // Pre-extracted ROM files
        .window_scale = 3,
        .vsync = true,
    });

    // Register recompiled functions
    func_table_register(0x040004, my_entry_point);
    func_table_register(0x040100, my_vblank_handler);
    // ...

    cps1_run();  // Never returns
    return 0;
}
```

## Project Structure

```
cps1recomp/
├── include/cps1recomp/
│   ├── cps1recomp.h      # Top-level API (init/run/shutdown)
│   ├── m68k.h             # 68000 CPU context + instruction macros
│   ├── bus.h              # CPS1 memory map + address decoding
│   ├── func_table.h       # Recompiled function dispatch table
│   ├── video.h            # Scroll layers + sprites (384x224)
│   ├── palette.h          # 12-bit RGB palette system
│   ├── ym2151.h           # YM2151 FM synthesis
│   ├── oki6295.h          # OKI MSM6295 ADPCM
│   ├── z80.h              # Z80 audio CPU
│   ├── io.h               # 6-button fighter input
│   ├── timer.h            # Interrupt timing
│   ├── rom.h              # Multi-ROM loader
│   ├── platform.h         # SDL2 window/audio/input
│   └── debug.h            # Tracing + breakpoints
└── src/
    └── *.c                # Implementations
```

## Part of the sp00nznet Ecosystem

cps1recomp is one piece of a growing static recompilation family:

| Platform | Toolkit | Games |
|----------|---------|-------|
| **CPS1** | **cps1recomp** (this) | Street Fighter II |
| Genesis | [genrecomp](https://github.com/sp00nznet/genrecomp) | Pigskin Footbrawl |
| Neo Geo | [neogeorecomp](https://github.com/sp00nznet/neogeorecomp) | Metal Slug, Neo Drift Out |
| SNES | [snesrecomp](https://github.com/sp00nznet/snesrecomp) | Super Mario Kart |
| GBA | [gbarecomp](https://github.com/sp00nznet/gbarecomp) | Advance Wars |
| N64 | [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Zelda, Banjo-Kazooie, Star Fox 64 |
| GameCube | [gcrecomp](https://github.com/sp00nznet/gcrecomp) | - |
| PS3 | [ps3recomp](https://github.com/sp00nznet/ps3recomp) | flOw, Tokyo Jungle |
| Xbox 360 | [360tools](https://github.com/sp00nznet/360tools) | Simpsons Arcade, Crazy Taxi |
| PC | [pcrecomp](https://github.com/sp00nznet/pcrecomp) | X-Wing Alliance, Civilization |

Same philosophy everywhere: **no emulation, no interpretation, no JIT. Just straight native code.**

## Legal

This project does not include any copyrighted game code, graphics, audio, or ROM files. You must supply your own legally obtained CPS1 ROM set. cps1recomp is a tool -- what you build with it is your responsibility.

## License

MIT License. Use it for anything -- commercial projects, homebrew, research, education.

---

*"You must defeat Sheng Long to stand a chance."*

**Built with love for the arcade games that ate our quarters.** Part of [sp00nznet](https://github.com/sp00nznet).
