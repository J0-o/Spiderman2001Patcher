# Third-party source

## KotOR Patch Manager

The build downloads `src/KPatchCore` and `src/KotorPatcher` from
[LaneDibello/Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
at the exact revision recorded in `dependencies.lock.json`. The downloaded
checkout and generated source trees are intentionally excluded from this
repository. The upstream `Patches` directory and the other folders marked
`ignore` in `source-layout.txt` are excluded from the sparse checkout.

Spider-Man-specific changes are kept under `overrides`. During a build, only
the replacement and removal paths explicitly recorded in `source-layout.txt`
are applied to the generated source trees.

The upstream repository does not contain a license file at the pinned revision.
Before publicly redistributing derived source or binaries, obtain or confirm
the necessary permission from the upstream author. No license for this
repository is asserted by this notice.

## toml++

The generated runtime's `external/toml.hpp` is the v3.4.0 amalgamated header
from [marzer/tomlplusplus](https://github.com/marzer/tomlplusplus), inherited
from the pinned KotOR Patch Manager source. It is distributed under the MIT
License; the copyright and complete license text are retained at the top of
the header.
