# Procedural City — Godot 4.4 GDExtension

A C++ GDExtension that drives the external **godisplacementx** CLI to generate
a procedural *displacement* map, builds extruded block geometry from it (the
Blender "Grid → Extrude Mesh per face" workflow), and applies a CLI-generated
albedo + normal material to the result. The CLI is fetched automatically: if no
binary is installed, the extension downloads the latest
[godisplacementx release](https://github.com/laiambryant/godisplacementx/releases)
from GitHub and caches it under `user://`.

It exposes the full godisplacementx parameter set inside the Godot inspector,
plus a selectable geometry backend (**ArrayMesh** / **MultiMesh** / **CSG
boxes** / **HexHive** / **GridMap**) and baked styling (vertex-colour ambient
occlusion and per-cell tint variation). Block tops are single flat faces.

---

## Contents

```
src/
  register_types.cpp      GDExtension entry point
  core/                   ProcCityGenerator node, pipeline, worker threading
  meshing/                image → geometry backends (blocks, hex, multimesh, CSG, gridmap)
  cli/                    goplacementx params/runner, binary download, GPU server
  material/               material assembly from the CLI-generated maps
  hive/                   standalone HiveGenCore floor planner
godot-cpp/                git submodule (branch 4.4) — the binding library
demo/                     runnable Godot 4.4 project
  addons/procedural_city/
    bin/                  compiled extension libraries land here (build output)
    godisplacementx/<os>/ optional manually-installed CLI binary (untracked)
    procedural_city.gdextension
  scenes/main.tscn        a wired-up ProcCityGenerator
docs/ARCHITECTURE.md      module map, threading & determinism contracts
CONTRIBUTING.md           setup, code style, PR checklist
SConstruct                top-level build script
```

Design deep-dive: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · Contributor
guide: [CONTRIBUTING.md](CONTRIBUTING.md)

## Registered classes

| Class | Base | Role |
|---|---|---|
| `ProcCityGenerator` | `Node3D` | Main node: parameters, inspector action buttons, threading, signals |
| `GoplacementxParams` | `Resource` | The full goplacementx parameter set; serializes to the CLI config JSON |
| `GoplacementxRunner` | `RefCounted` | Locates the binary, writes the config, runs the CLI (thread-safe) |
| `HeightmapMesher` | `RefCounted` | Image → ArrayMesh / MultiMesh / CSG boxes |

The GridMap backend is not part of `HeightmapMesher`: it plans voxel cells
(`meshing/gridmap_plan.cpp`) that `ProcCityGenerator` turns into a `GridMap`
node plus its `MeshLibrary`.

---

## Prerequisites

- **Godot 4.4** or newer.
- **Python 3** + **SCons** (`pip install scons`).
- A C++17 toolchain:
  - Windows: Visual Studio 2022 (Desktop C++ workload) — SCons auto-detects it.
  - Linux: `gcc`/`clang`. macOS: Xcode command-line tools.
- The **godisplacementx** repo is only needed if you want to hack on the CLI
  itself — the extension downloads prebuilt release binaries on demand.

## 1. Get godot-cpp

The binding library is a submodule pinned to the `4.4` branch:

```bash
git submodule update --init --recursive
```

## 2. Build the extension

```bash
# from the repo root
scons platform=windows target=template_debug arch=x86_64        # editor / debug
scons platform=windows target=template_release arch=x86_64      # exported / release
```

Replace `platform=windows` with `linux` or `macos` as needed. The first build
compiles all of godot-cpp and takes several minutes; subsequent builds are
incremental. Output libraries are written to
`demo/addons/procedural_city/bin/`, which the `.gdextension` file references.

## 3. Get the godisplacementx CLI

Normally you do nothing: on the first generation the extension queries GitHub
for the latest [godisplacementx release](https://github.com/laiambryant/godisplacementx/releases),
downloads the binary for your platform and caches it under
`user://godisplacementx/bin/`. Delete that folder to force a re-download of a
newer release. The behaviour can be disabled per-node with the
**Auto Download Binary** property (Tool group).

The CLI is resolved in this order:

1. **Binary Path Override** (per-node property);
2. a binary you dropped into the addon:
   `demo/addons/procedural_city/godisplacementx/<windows|linux|macos>/godisplacementx-cli[.exe]`
   (the legacy `goplacementx` folder/binary names still work);
3. the `user://godisplacementx/bin/` download cache;
4. a fresh download of the latest GitHub release (when auto-download is on).

To build the CLI yourself instead (offline work, local changes), in the
godisplacementx repo:

```powershell
.\build.ps1 cli          # produces build\bin\godisplacementx-cli.exe
```

then drop the binary into location 2 above (on Linux/macOS `chmod +x` it).
Those folders are intentionally untracked.

---

## Usage

### In the editor (recommended)

1. Open the `demo` project in Godot 4.4. Confirm the extension loads (the
   `ProcCityGenerator` node appears in *Create Node*).
2. Select the `ProcCityGenerator` node. In the inspector:
   - Set **Mesh Size** (world meters), **Grid Vertices** (the Blender Grid
     resolution — one box per cell), **Height Scale**.
   - Shape the skyline with **Height Power** (`v^power` remap: > 1 thins the
     city into a few tall towers, < 1 raises the low blocks) and carve streets
     with **Block Inset** (> 0 shrinks every block footprint, producing
     freestanding buildings over an automatic ground plane).
   - Enable **Generate Collision** for a `StaticBody3D` + trimesh shape under
     the generated geometry (CSG mode uses its built-in `use_collision`,
     GridMap mode collides through its own MeshLibrary shapes).
   - Pick **Build Mode**: `ArrayMesh` (default; continuous material, fast),
     `MultiMesh` (GPU-instanced), `CSG` (literal `CSGBox3D`s, capped),
     `HexHive`, or `GridMap` (a real `GridMap` node you can keep editing by
     hand after generation).
   - In GridMap mode, set **GridMap Level Height** (0 = auto: cells as close to
     cubic as the footprint allows), **GridMap Fill Columns** (off places only
     each column's top cell) and optionally a **GridMap Mesh Library** +
     **GridMap Item Id** to stamp your own props instead of plain blocks.
   - Expand **Params** to tune every goplacementx layer (rect/grid/cols/rows/
     lines/sprites), composition modes, gradient, resolution, seed, etc.
3. Click the action buttons:
   - **Generate Displacement** — runs the CLI, caches the height map.
   - **Build Geometry** — builds blocks from the cached height map.
   - **Generate Material** — runs the CLI in `color` + `normal` modes and applies
     the material.
   - **Generate All** — the full pipeline in one click.
   - **Clear Generated** — removes the generated child.

The CLI runs on a worker thread so the editor never freezes; results are applied
on the main thread.

### From GDScript

```gdscript
var gen := ProcCityGenerator.new()
add_child(gen)
gen.mesh_size = Vector2(20, 20)
gen.grid_vertices = Vector2i(48, 48)
gen.height_scale = 4.0
gen.build_mode = ProcCityGenerator.BUILD_ARRAY_MESH

gen.params = GoplacementxParams.new()
gen.params.resolution = 1024
gen.params.iterations = 200
gen.params.randomize_seed = true

gen.generation_finished.connect(func(stage, path): print("done: ", stage))
gen.generation_failed.connect(func(stage, msg): push_error(msg))
gen.generate_all()
```

You can also skip the CLI entirely and drive the geometry from any grayscale
`Image` of your own:

```gdscript
gen.set_height_image(my_heightmap)   # any Image; the first channel is sampled
gen.height_power = 2.0               # optional skyline remap (v^power)
gen.block_inset = 0.12               # optional streets between blocks
gen.generate_collision = true        # optional StaticBody3D + trimesh shape
gen.build_geometry()
```

### Signals

| Signal | Args |
|---|---|
| `generation_started` | `stage: String` |
| `generation_progress` | `stage: String, ratio: float` |
| `generation_finished` | `stage: String, path: String` |
| `generation_failed` | `stage: String, message: String` |
| `all_finished` | — |

---

## How it maps to godisplacementx

- `GoplacementxParams.to_json()` produces the exact `Params` shape the CLI
  expects (JSON tags such as `rectBrightness`, `compositionModes`, …). Each Go
  `Dual [min,max]` is a `Vector2i (x=min, y=max)`; the gradient is a
  `PackedColorArray`; composition modes / sprite packs are flag enums.
- The config is written once per run; only `--mode`, `--seed`, `--out`,
  `--resolution`/`--width`/`--height`, `--invert`, and `--gradient` are passed on
  the command line. The **height, albedo and normal maps share one config and one
  resolved seed**, so they align pixel-for-pixel. Changing any parameter between
  the displacement and material steps breaks that alignment.

## Texture & material configuration

All texture-generation options live in the **Texture** group of the
`GoplacementxParams` resource:

| Property | Meaning |
|---|---|
| `resolution` | Square output size of the generated maps (1024, 2048, 4096, …). |
| `out_width` / `out_height` | Non-square output; override `resolution` when both > 0. |
| `palette_preset` | Named palette for the color/albedo map. `Custom` uses `gradient_colors`; the rest are built-in (`Grayscale`, `DisplacementX`, `Fire`, `Ocean`, `Sunset`, `Neon`, `Terrain`, `Viridis`). |
| `gradient_colors` | The custom palette (a `PackedColorArray`), used only when `palette_preset = Custom`. Serialized to the CLI as `--gradient "#rrggbb,…"`. |
| `invert` | Invert the generated map. |
| `seamless` | Generate a tileable / seamless texture. |

The **Seed** group (`seed`, `randomize_seed`) controls reproducibility — the
displacement, albedo and normal maps all share one resolved seed so they stay
aligned.

### Selecting vs generating a palette

- **Select** a built-in look by setting `palette_preset`.
- **Generate** a fresh palette with the **Randomize Palette** inspector button
  (or `generate_random_palette(stops, seed)` from script). It fills
  `gradient_colors` with an HSV-derived dark→light ramp (so it reads well as a
  height-mapped albedo) and switches `palette_preset` to `Custom`. In the editor
  it re-runs the material automatically if a displacement already exists.

```gdscript
gen.params.palette_preset = 0            # Custom
gen.params.generate_random_palette(5, 0) # 5 random stops, seed 0 = random
# or pick a preset:
gen.params.palette_preset = 3            # Fire
gen.generate_material()
```

### Surface options (on the node, applied to the built material)

`material_mode` (Standard/ORM), `normal_strength`, `roughness`, `metallic`,
`uv_scale` (tiles the maps over the mesh — ArrayMesh only), `texture_filter`
(`Nearest` for a crisp pixel-art look, `Linear` for smooth, or
`Linear Mipmap Anisotropic` for runtime camera movement), `texture_repeat`, and
`material_max_size` (`0` keeps the generated resolution; a positive value
downscales material maps before their GPU upload without changing the height
map used for geometry).

### Style options (baked into the mesh)

The **Style** group bakes lighting/variation cues into vertex colours at build
time (no extra draw cost; multiplied into the albedo):

| Property | Meaning |
|---|---|
| `ao_strength` | Ambient-occlusion strength: walls darken toward canyon floors, roofs darken beside taller neighbours, hex alley floors darken. `0` disables. |
| `color_variation` | Seeded per-cell luminance variation that breaks texture repetition. `0` disables. |

Walls also sample the texture with *draped* UVs (the roof mapping continued
down the face) instead of the old planar projection, so vertical faces no
longer smear a single texture row. The HexHive backend additionally gets a
`hive_floor` ground plane so the gaps between cells read as alleys instead of
holes.

## Performance

The generation pipeline is built for large grids and large maps:

- **Meshers write exact-size packed arrays** (indexed triangles, analytic
  per-face tangents) instead of per-vertex `SurfaceTool` calls and a mikktspace
  pass — the block mesher builds a 65k-cell city in tens of milliseconds.
- **Row-banded multithreading** everywhere the work is per-cell: height
  sampling, block emission (two-pass: per-row quad counts → prefix sums →
  disjoint writer ranges) and hex emission. Output is byte-identical for any
  thread count.
- **MultiMesh uploads one packed buffer** (`set_buffer`, 12 floats per
  instance) instead of one `set_instance_transform` call per box.
- **Intermediates skip the PNG round-trip**: the CLI emits the `.gdxraw`
  interchange format (raw L8/RGB8/RGBA8 rows, 16-byte header) and the extension
  maps it straight into an `Image` — no encode, no decode, height maps load as
  single-channel L8. Set **Keep Intermediate PNG** to get inspectable PNGs
  instead (slower).
- **Raw maps stream into their final allocation** from disk, the CLI pipe, and
  gRPC. Bundle responses are decoded map-by-map, so the client does not retain
  a second full response alongside all decoded images.
- **Height images are sampled in place** with their native pixel stride, with
  no full-image format conversion or extracted red-channel copy. The
  box-average downsample — the heaviest loop in the pipeline, since it reads
  every byte of the source map — accumulates in 32-bit lanes so the compiler can
  fold a row with packed byte adds, and splits one row per thread rather than
  the 16-row granularity the cheap emission passes want.
- **Geometry can be chunked** (`geometry_chunks`, ArrayMesh backend): the city
  is emitted as an N×N grid of `MeshInstance3D` tiles over disjoint cell rects,
  so the renderer frustum- and occlusion-culls per tile instead of submitting
  one grid-wide mesh every frame, and each tile's collision BVH is built over
  its own slice. Neighbour culling still reads the whole height grid, so the
  chunks are triangle-for-triangle what the single mesh was (the ground plane
  aside, which is laid per tile on purpose). On a 72×72 grid over an 8192 map,
  building the collision shapes costs 30.4 ms as one mesh, 23.5 ms at 2×2 and
  19.2 ms at 4×4; mesh build time is unchanged.
- **Hidden geometry can be clipped before allocation**: set
  `clip_below_height` to the world-space height of an opaque ground or street
  plane. ArrayMesh roofs below it are omitted and crossing walls are clamped to
  it; the same compact surface feeds render and collision generation.
- **Cell heights are cached during ArrayMesh construction**. Subsequent
  `get_cell_heights()` calls return the exact samples already used by the mesh
  instead of scanning the source map again.
- After consuming the cached heights, call `release_source_images()` to drop
  the CPU-side height/albedo/normal sources. Material textures and generated
  geometry remain valid.

`demo/tests/bench_meshers.gd` times the mesher backends headless;
`demo/tests/bench_hotpath.gd` times the same path at the resolution a shipping
game uses (8192 source map onto a 72×72 grid) and splits the collision cost into
readback vs BVH build; `demo/tests/multimesh_parity.gd` pins the multimesh
buffer layout; `demo/tests/hive_parity.gd` pins `HiveGenCore`'s RNG stream by
digest; `demo/tests/chunked_geometry.gd` pins chunked output against the single
mesh and covers the query API.

## Culling and gameplay queries

| Property | Meaning |
| --- | --- |
| `geometry_chunks` | `1` (default) keeps one grid-wide `MeshInstance3D` named `GeneratedCity`. Above that, `GeneratedCity` becomes a container of `Chunk_<n>` tiles, each with its own mesh, AABB and collision body. ArrayMesh backend only. |
| `generate_occluders` | Adds an `OccluderInstance3D` per tile, built from that tile's own triangles (conservative by construction). Needs occlusion culling enabled in project settings to do anything. |

Three queries read the cached cell heights, so they cost a grid lookup rather
than an image sample and keep working after `release_source_images()`. All take
points in the generator's local space:

```gdscript
var top := city.sample_city_height(point)          # height field value, 0 outside
var solid := city.is_point_inside_block(point)     # honours clip_below_height + block_inset
var spawn := city.find_clear_point(point, 40.0)    # nearest cell no taller than clip_below_height
```

`is_point_inside_block` reports false for cells clipped away by
`clip_below_height` (they contribute no geometry) and for the streets that
`block_inset` opens between freestanding towers. `find_clear_point` searches
outward in rings and returns the input unchanged when nothing within
`max_radius` is clear.

## Geometry notes

- One extruded box per grid cell; `grid_vertices` downsamples the height image
  (`Nearest` or `BoxAverage` filter). Cells beyond **Max Cells** abort with a
  clear error.
- `height_power` remaps every sampled height (`v^power`) before scaling;
  `block_inset` (fraction of a cell per side, 0–0.45) switches the ArrayMesh
  backend from merged blocks to freestanding buildings with streets and a
  ground plane, and shrinks the box footprints in the other backends.
- `generate_collision` adds `GeneratedCity/CollisionBody` (a `StaticBody3D`
  with a `ConcavePolygonShape3D` built from the block geometry). CSG mode
  enables the combiner's own `use_collision` instead; GridMap mode bakes a
  `BoxShape3D` into the generated MeshLibrary item and sets the node's
  collision layer, since a GridMap is its own collider.
- `clip_below_height` is an ArrayMesh optimization for cities sunk under an
  opaque plane. `0` disables it; otherwise cells entirely below the cutoff are
  discarded and exposed walls begin at the cutoff. Match it to the plane's
  local Y height so visible geometry is unchanged.
- **GridMap** quantizes each column's height into stacked cells instead of
  extruding a mesh: `gridmap_level_height` sets the vertical cell size (0
  derives one from the footprint so cells come out roughly cubic), rounded to
  the nearest whole number of levels with a floor of one so the ground layer
  never develops holes. `gridmap_fill_columns` off places only the top cell of
  each column (a hollow surface, one cell per column). `block_inset` shrinks
  the block inside its cell, so streets still work. The node is a plain
  `GridMap` on its default `cell_center_*` flags, positioned at the grid's
  north-west corner with level 0 on `y = 0` — hand-edit it afterwards like any
  other GridMap. A GridMap takes no material override, so the material is
  applied to the block mesh in the generated MeshLibrary; supply your own
  `gridmap_mesh_library` (plus `gridmap_item_id`) and it is used untouched,
  materials and shapes included. Plans over 65 536 cells warn, and over
  1 048 576 fail with the knobs to turn.
- **ArrayMesh** is the only backend that drapes the albedo/normal as a single
  continuous image over the whole surface (side walls between cells of differing
  height are generated; shared interior walls are skipped). MultiMesh and CSG
  tile the material per box.
- Meshing is allocation-exact and index-based: the builders write straight
  into preallocated packed arrays with analytic tangents (no SurfaceTool, no
  mikktspace pass), sampling the height image in place for L8/RGB8/RGBA8
  data. A 256×256-cell ArrayMesh city builds in ~40 ms (was ~590 ms).
- **CSG** is expensive — Godot rebuilds the whole boolean union on every change.
  A warning is emitted past ~2048 boxes; prefer it only when you need a
  boolean-welded solid.

## Limitations / platform

- Editor / authoring focused: the CLI is resolved from `res://` at dev time. For
  exported games, executables inside a PCK can't run; ship the CLI as a loose
  file and point **Binary Path Override** at it.
- Release binaries exist for windows/amd64, linux/amd64+arm64 and
  darwin/amd64+arm64; the auto-download picks the right one. Anything else needs
  a locally built CLI.
