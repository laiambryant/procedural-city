# Contributing

## Setup

```bash
git clone --recurse-submodules <repo-url>
pip install scons
scons platform=linux target=template_debug   # or windows / macos
```

The first build compiles godot-cpp and takes several minutes; later builds are
incremental. Output lands in `demo/addons/procedural_city/bin/`, which the
demo project's `.gdextension` references. Open `demo/` in Godot 4.4+ to test.

## Layout

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the module map and the
threading/determinism contracts. The short version:

| Path | Contents |
|---|---|
| `src/core/` | `ProcCityGenerator` node, pipeline orchestration, bindings |
| `src/meshing/` | image → geometry backends and shared mesh infrastructure |
| `src/native/` | in-process displacement backends (compute shader / worker threads) |
| `src/cli/` | params resource, legacy CLI runner, binary download, gRPC client |
| `src/material/` | material assembly from the generated maps |
| `src/editor/` | inspector plugin |
| `src/hive/` | standalone `HiveGenCore` floor planner |

## Code style

- Godot C++ conventions: tabs, `p_` parameter / `r_` out-parameter prefixes,
  `_private_method` naming, `snake_case`.
- Formatting is enforced by `.clang-format` (CI runs
  `clang-format --dry-run --Werror`). Before pushing:

  ```bash
  clang-format -i src/**/*.cpp src/**/*.h
  ```

- **No magic values, no comments.** Tuning factors, protocol offsets,
  thresholds and hash salts get a named `constexpr` whose name says what the
  value means — file-local when used once, in the module header when shared
  (see `src/meshing/baked_style.h`). The same goes for logic: a block that
  needs explaining is a block that needs a better-named helper function, not a
  comment next to it.
- Includes are rooted at `src/`: `#include "meshing/packed_surface.h"`.
- Keep worker-thread code free of scene-tree access; anything touching nodes
  runs on the main thread (see the threading contract in ARCHITECTURE.md).

## Determinism rules

Same seed ⇒ same output, for any thread count, on every platform:

- Randomness in mesh code goes through `deterministic_noise.h`, never the
  engine RNG.
- The hash salts in `baked_style.h` and the seed-mixing constants are frozen —
  changing them silently regenerates every existing seed's city.
- `HiveGenCore` must stay call-for-call identical to its GDScript counterpart
  (same RNG stream).

## Testing

Contract scripts live in `demo/tests/` and run headless:

```bash
godot --headless --path demo --script tests/mesher_contract.gd
godot --headless --path demo --script tests/inspector_rules.gd
godot --headless --path demo --script tests/chunked_geometry.gd
godot --headless --path demo --script tests/multimesh_parity.gd
godot --headless --path demo --script tests/hive_parity.gd
godot --headless --path demo --script tests/scene_size.gd
```

`bench_*.gd` in the same folder time the pipeline and the mesher backends. If
you touch a mesher, verify the parity scripts still pass and note before/after
timings in the PR.

## Pull requests

- One logical change per PR; keep formatting-only churn out of functional
  commits.
- CI must be green (format check + Linux debug/release builds).
- If your change alters generated geometry for an existing seed, say so
  explicitly in the PR description — that is a breaking change for users with
  saved seeds.
