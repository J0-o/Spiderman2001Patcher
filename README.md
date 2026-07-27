# Spider-Man Patch Manager

This is a UI-free adaptation of KotOR Patch Manager's runtime patch framework.
Every launch discovers and applies every top-level `.kpatch` file in the patch
directory, then starts Spider-Man with the 32-bit runtime DLL injected.

Only Spider-Man source and explicit upstream replacements are kept in this
repository. The build downloads the pinned KotOR Patch Manager source recorded
in `dependencies.lock.json`, following the sources, replacements, removals, and
ignored folders in `source-layout.txt`. Downloaded and generated source is
excluded by `.gitignore`. See `THIRD_PARTY.md` for provenance and licensing
notes.

Supported executable builds:

| Build | SHA-256 |
| --- | --- |
| Retail (`SpideyPC_original.exe` in this workspace) | `0A11A49F3F63D4BB15650F322D654553FC47234B01885B852A058FF074689DEA` |
| No-CD (`SpideyPC.exe` in this workspace) | `D55A0BB0E920C497CE1CA76F08ED2E62FEEFCB6FF3C2901C0D59890F099BA93C` |

## Build

Requirements:

- .NET 8 SDK (build only; the published patcher is self-contained)
- Visual Studio 2022 with the Desktop development with C++ workload
- Git (used to download the pinned KotOR Patch Manager sparse checkout)

Run:

```powershell
.\build.ps1
```

On the first run, the script creates a sparse checkout under `.deps`, verifies
the pinned revision, and downloads only `src/KPatchCore` and
`src/KotorPatcher`. The upstream `Patches` directory and the other ignored
folders in `source-layout.txt` are not materialized. The script reconstructs
`KPatchCore` and `SpiderManPatcher.Runtime`, applies only the listed replacement
files, removes the listed unused paths, and builds the complete patcher. Later
builds reuse the verified checkout.

The compact deployable build is written to `dist`: one self-contained launcher
with the injection runtime embedded, plus this repository's packaged
Spider-Man patches.

Do not edit the generated `KPatchCore` or `SpiderManPatcher.Runtime`
directories. Put replacement or added files under `overrides/<source-name>`
and list every replacement/addition, removal, or ignored upstream folder in
`source-layout.txt`. The build rejects unlisted override files.

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

Copy the contents of `dist` beside `SpideyPC.exe`, put all `.kpatch` files in
the adjacent `patches` directory, and run:

```powershell
.\SpideyLauncher.exe
```

Paths can also be supplied explicitly:

```powershell
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
