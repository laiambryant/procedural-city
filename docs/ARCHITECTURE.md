# Architecture

The extension is organized into seven modules under `src/`, plus the
GDExtension entry point at the root. Dependencies point downward only:

```
register_types.cpp          entry point: class registration, singleton lifecycle
│
├── core/                   ProcCityGenerator node: inspector surface, pipeline
│     │                     orchestration, worker threading, scene-tree surgery
│     ├── meshing/          image → geometry (no scene-tree access)
│     ├── native/           in-process displacement backends (cppdisplacementx)
│     ├── cli/              everything about the external CLI processes
│     └── material/         generated maps → BaseMaterial3D
│
├── editor/                 EditorPlugin: the generation-mode radio group
└── hive/                   standalone floor planner (HiveGenCore)
```

Includes are rooted at `src/` (`#include "meshing/packed_surface.h"`), so a
file's module is visible at every include site.

## core/ — orchestration

`ProcCityGenerator` (Node3D) owns the user-facing parameter surface and the
generation pipeline. The pipeline is stage-based (`STAGE_HEIGHT`,
`STAGE_GEOMETRY`, `STAGE_MATERIAL`, `STAGE_APPLY_MATERIAL`); public entry
points compose stages:

- `generate_displacement()` → height only
- `build_geometry()` → geometry from the cached height image (synchronous)
- `generate_material()` → material maps + apply
- `generate_all()` → everything

`proc_city_pipeline.cpp` starts and finishes jobs; `proc_city_worker.cpp` holds
the worker-thread body. The threading contract:

1. `_snapshot_job()` copies **every** input into a `Dictionary` payload on the
   main thread — the worker never reads member fields.
2. The worker renders the maps, decodes them concurrently, and builds
   ArrayMesh-backed geometry off-thread (safe: only worker-owned
   `Image`/`ArrayMesh` resources; surface creation goes through the
   RenderingServer's thread-safe queue).
3. Results return via `call_deferred("_apply_results", …)`; all scene-tree
   mutation happens on the main thread.

`proc_city_generator_bindings.cpp` isolates the ClassDB boilerplate from the
logic, and `proc_city_inspector.cpp` declares the editor-facing surface:
property groups, tool buttons, signals, and the `_validate_property` rules that
grey out properties the current configuration cannot use (the GPU server
outside GPU Legacy, the Hive group outside HexHive, and so on).

## native/ — in-process displacement

`native_renderer.cpp` runs the cppdisplacementx core inside the extension,
dispatching its compute shader through a local `RenderingDevice`
(`native_gpu.cpp`) or compositing on worker threads. `native_params.cpp`
bridges the `GoplacementxParams` resource and the sprite atlas into the core's
own structs. No process, no config file, no temporary files: the maps come back
as `Image`s.

## meshing/ — image → geometry

Pure functions and a `RefCounted` façade (`HeightmapMesher`) with four
backends: ArrayMesh blocks (`block_mesher.cpp`), hex-prism honeycomb
(`hex_hive_mesher.cpp`), MultiMesh and CSG (`heightmap_mesher.cpp`).

`gridmap_plan.cpp` is the fifth backend and the odd one out: it emits no
geometry at all, only a `GridMapPlan` (cell size, origin, occupied cell
coordinates) quantized from the same sampled heights. `core/` turns that plan
into the `GridMap` node and its `MeshLibrary`, keeping this module free of
scene-tree access like the rest.

Each backend's internals live in a header beside it — `block_layout.h` (cell
footprints, visible walls, per-row prefix sums), `block_style.h` (the baked
vertex colours) and `hex_lattice.h` (warped centres, column heights, corners).
Header-only on purpose: this is per-cell code, and an out-of-line call would
cost more than the maths it wraps.

Shared infrastructure:

- `height_sampling` — `HeightImageView` (zero-copy red-channel view of a
  height `Image`) plus cell/UV sampling and the `v^power` remap.
- `packed_surface` — the indexed-triangle sink: exact-size packed arrays
  written through raw pointers, analytic per-face tangents. `Writer`s own
  disjoint vertex/index ranges so threads fill bands concurrently with
  deterministic output.
- `parallel_rows` — row-banded fork/join helper; output is byte-identical for
  any thread count.
- `deterministic_noise` — engine-independent seeded hash/value noise.
- `baked_style.h` — the named tuning constants (AO spans, tint span) and hash
  salts for the baked vertex-colour look. The salts are frozen: changing one
  changes every city generated from an existing seed.

## cli/ — external process handling

- `goplacementx_params` — `Resource` mirroring the CLI's Params JSON;
  `_json.cpp` holds the (de)serialization.
- `goplacementx_runner` — writes the config, resolves seeds, execs the CLI
  (`generate` / `bundle`). No scene-tree access; worker-thread safe.
- `binary_provider` — CLI binary resolution: override → bundled → download
  cache → GitHub latest release (blocking HTTPS with manual redirects, zip/tar
  extraction).
- `gpu_server` — persistent `gpudisplacementx serve` process behind one
  request/response pipe (`io_mutex`-serialized), so GPU bring-up is paid once
  per session. Any failure falls back to the one-shot CLI.
- `gdxraw_loader` — the `.gdxraw` interchange format (16-byte header + raw
  rows) that lets intermediates skip the PNG encode/decode round-trip.

## material/ — maps → material

`build_city_material(CityMaterialSpec)` assembles a Standard/ORM material from
the generated maps; `compose_rgb_albedo` packs three grayscale maps into one
RGB image for the channels texture mode.

## editor/ — inspector plugin

`generation_mode_radio.cpp` replaces the `generation_mode` enum dropdown with a
radio group, so all four backends are visible at once. It is the only code in
the extension that links against editor classes.

## hive/ — floor planner

`HiveGenCore` is a self-contained port of a GDScript dungeon-floor planner and
must stay call-for-call identical to it (same seed → same RNG stream → same
plan). It shares no code with the city pipeline.

## Failure model

Every failure path funnels through `_emit_failed(stage, message)`: it pushes
an editor error and emits `generation_failed`. GPU-specific failures degrade
(server → one-shot CLI → CPU CLI) before failing.

## Determinism

One resolved seed drives the height, albedo and normal maps (they must stay
pixel-aligned) and all baked styling. Derived streams use `mix_seed`
(splitmix64 golden-gamma) or the salts in `baked_style.h`; both are frozen
constants.
