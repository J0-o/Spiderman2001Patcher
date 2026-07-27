# Spider-Man (Neversoft 2001) Patcher

When asked about the possibilities of a remake:
"While I would initially be reticent because of the nightmarish licensing and approval processes that are around these days... I absolutely would do it...” - Chad Findely

## Goals
The Neversoft 2000/2001 Spider-man game seems to have become abandonware. The goal of this repository is to provide tools and fixes that resolve bugs in the PC version and improve its compatibility with modern hardware and operating systems.

This is a port of KotOR Patch Manager's runtime patch framework.
Every launch discovers and applies every top-level `.kpatch` file in the patch
directory, then starts Spider-Man with the 32-bit runtime DLL injected.

Supported executable builds:

| Build | SHA-256 |
| --- | --- |
| Retail (`SpideyPC.exe` ) | `0A11A49F3F63D4BB15650F322D654553FC47234B01885B852A058FF074689DEA` |
| No-CD (`SpideyPC.exe` ) | `D55A0BB0E920C497CE1CA76F08ED2E62FEEFCB6FF3C2901C0D59890F099BA93C` |

## Build

Requirements:

- .NET 8 SDK (build only; the published patcher is self-contained)
- Visual Studio 2022 with the Desktop development with C++ workload
- Git (used to download the pinned KotOR Patch Manager sparse checkout)

Run:

```powershell
.\build.ps1
```
## Source layout

Every patch is self-contained in one directory under `Patches`. A patch
directory contains its `manifest.toml`, hook TOML files, and—when native code
is required—its Visual C++ project and `src`/`include` directories. Build
outputs remain inside that patch directory and are ignored by Git.

The remaining top-level source is shared infrastructure:

- `SpiderManPatcher`: headless launcher
- `overrides/KPatchCore`: replacements applied to upstream KPatchCore
- `overrides/KotorPatcher`: the three required runtime replacements
- `source-layout.txt`: generated-source, replacement, removal, and ignore list
- `Patches`: Spider-Man patch packages maintained by this repository

## Use

Copy `SpideyLauncer.exe`, and the `Patches` folder to the game directory. Then use the launcher to play the game.

```
.\SpideyLauncher.exe
```

Paths can also be supplied explicitly:

```
.\SpideyLauncher.exe "C:\Games\Spider-Man\SpideyPC.exe" "C:\Mods\Spider-Man\Patches"
```

The patch directory is authoritative. On each run the launcher restores its
previous managed backup, validates and applies the directory's current patch
set, and launches the game. Invalid archives, duplicate IDs, missing
dependencies, conflicts, unsupported executable hashes, overlapping hooks, or
byte mismatches stop the launch with a nonzero exit code.

## Patch format

The format is compatible with the reference manager:

- `manifest.toml`
- one or more files ending in `hooks.toml`
- optional `binaries/windows_x86.dll` for DETOUR or DLL-only patches

Each manifest must include the exact target executable hash:

```toml
id = "example"
name = "Example"
version = "1.0.0"
author = "Author"
description = "Example Spider-Man patch"
requires = []
conflicts = []

[supported_versions]
spider_man_no_cd = "D55A0BB0E920C497CE1CA76F08ED2E62FEEFCB6FF3C2901C0D59890F099BA93C"
```

Hook addresses are virtual addresses in the loaded 32-bit executable. SIMPLE,
REPLACE, DETOUR, DLL_ONLY, and install-time STATIC behavior are inherited from
the reference implementation.
