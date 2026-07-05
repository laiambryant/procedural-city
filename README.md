# Procedural City — Godot 4.4 GDExtension

A C++ GDExtension that drives the external **goplacementx** CLI to generate a
procedural *displacement* map, builds extruded block geometry from it (the
Blender "Grid → Extrude Mesh per face" workflow), and applies a CLI-generated
albedo + normal material to the result.

It exposes the full goplacementx parameter set inside the Godot inspector, plus
a selectable geometry backend (**ArrayMesh** / **MultiMesh** / **CSG boxes**).

---

## Contents

```
src/                      C++ sources for the extension
godot-cpp/                git submodule (branch 4.4) — the binding library
thirdparty/goplacementx/  canonical home for the prebuilt CLI binaries
demo/                     runnable Godot 4.4 project
  addons/procedural_city/
    bin/                  compiled extension libraries land here (build output)
    goplacementx/<os>/    the bundled CLI binary, resolved at runtime
    procedural_city.gdextension
  scenes/main.tscn        a wired-up ProcCityGenerator
SConstruct                top-level build script
```

## Registered classes

| Class | Base | Role |
|---|---|---|
| `ProcCityGenerator` | `Node3D` | Main node: parameters, inspector action buttons, threading, signals |
| `GoplacementxParams` | `Resource` | The full goplacementx parameter set; serializes to the CLI config JSON |
| `GoplacementxRunner` | `RefCounted` | Locates the binary, writes the config, runs the CLI (thread-safe) |
| `HeightmapMesher` | `RefCounted` | Image → ArrayMesh / MultiMesh / CSG boxes |

---

## Prerequisites

- **Godot 4.4** or newer.
- **Python 3** + **SCons** (`pip install scons`).
- A C++17 toolchain:
  - Windows: Visual Studio 2022 (Desktop C++ workload) — SCons auto-detects it.
  - Linux: `gcc`/`clang`. macOS: Xcode command-line tools.
- The **goplacementx** repo (for building the CLI binary).

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

## 3. Build & bundle the goplacementx CLI

The plugin shells out to the **CLI** build of goplacementx (not the GUI). In the
goplacementx repo:

```powershell
.\build.ps1 cli          # produces build\bin\goplacementx-cli.exe
```

Copy the binary next to the addon, under a per-platform folder:

```
demo/addons/procedural_city/goplacementx/windows/goplacementx-cli.exe
demo/addons/procedural_city/goplacementx/linux/goplacementx-cli
demo/addons/procedural_city/goplacementx/macos/goplacementx-cli
```

(The Windows binary is already bundled.) On Linux/macOS, mark it executable
(`chmod +x`). You can override the location per-node via the **Binary Path
Override** property.

> **Git note:** the project `.gitignore` ignores `*.exe`/`*.so`. The bundled CLI
> binaries are re-included via `!**/goplacementx/**`. If you add binaries for a
> new platform and git ignores them, force-add: `git add -f path/to/binary`.

---

## Usage

### In the editor (recommended)

1. Open the `demo` project in Godot 4.4. Confirm the extension loads (the
   `ProcCityGenerator` node appears in *Create Node*).
2. Select the `ProcCityGenerator` node. In the inspector:
   - Set **Mesh Size** (world meters), **Grid Vertices** (the Blender Grid
     resolution — one box per cell), **Height Scale**.
   - Pick **Build Mode**: `ArrayMesh` (default; continuous material, fast),
     `MultiMesh` (GPU-instanced), or `CSG` (literal `CSGBox3D`s, capped).
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

### Signals

| Signal | Args |
|---|---|
| `generation_started` | `stage: String` |
| `generation_progress` | `stage: String, ratio: float` |
| `generation_finished` | `stage: String, path: String` |
| `generation_failed` | `stage: String, message: String` |
| `all_finished` | — |

---

## How it maps to goplacementx

- `GoplacementxParams.to_json()` produces the exact `Params` shape goplacementx
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
(`Nearest` for a crisp pixel-art look, `Linear` for smooth), and
`texture_repeat`.

## Geometry notes

- One extruded box per grid cell; `grid_vertices` downsamples the height image
  (`Nearest` or `BoxAverage` filter). Cells beyond **Max Cells** abort with a
  clear error.
- **ArrayMesh** is the only backend that drapes the albedo/normal as a single
  continuous image over the whole surface (side walls between cells of differing
  height are generated; shared interior walls are skipped). MultiMesh and CSG
  tile the material per box.
- **CSG** is expensive — Godot rebuilds the whole boolean union on every change.
  A warning is emitted past ~2048 boxes; prefer it only when you need a
  boolean-welded solid.

## Limitations / platform

- Editor / authoring focused: the CLI is resolved from `res://` at dev time. For
  exported games, executables inside a PCK can't run; ship the CLI as a loose
  file and point **Binary Path Override** at it.
- Cross-platform binaries must be built on each target OS (`goplacementx` ships a
  Windows `build.ps1`; build the `cli` target on Linux/macOS too).
