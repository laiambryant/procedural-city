# Performance and simplification notes

## Changes in this revision

- **CPU compositing:** partition the canvas once, then run the existing
  cppdisplacementx compositor serially inside each band. Previously every
  sufficiently large drawing command started and joined its own threads.
  Commands keep their original order; sprite coordinates are translated with
  the clipped band. Small canvases use the serial path. No blend equations,
  seeds, or dependency source files change.
- **Chunk meshing:** fill independent packed surfaces concurrently, in batches
  bounded by hardware concurrency. Commit ArrayMesh resources sequentially.
  The observed headless RID failure and workaround are recorded in
  [HEADLESS_RENDERER.md](HEADLESS_RENDERER.md).
  Batching bounds the additional staging memory. Chunk and triangle order stay
  unchanged, and inner row workers are disabled when chunks run in parallel.
- **Native height maps:** export an uninverted grayscale field directly,
  avoiding a redundant RGBA canvas allocation and copy: 16 MiB at 2048²,
  256 MiB at 8192². Color, normal, and inverted outputs retain the core's
  original derivation routines.
- **Material assembly:** reuse `HeightImageView` for RGB channel composition,
  removing the second implementation of image-format/stride decoding.
- **Showcase:** one mesh per visible city, three cities generated once before
  playback, and a timeline that is a pure function of one clock. It uses a
  2048² source field, no collision and no per-tile tweens: the rise is a scale
  written straight from the pose function, so a captured frame and a played
  frame at the same time are identical. The three cached cities trade retained
  mesh/texture memory for uninterrupted playback. Blocks are meshed with no
  inset — neighbouring cells at the same height share a roof and their shared
  walls are culled — and the generator is sunk by `clip_below_height` so the
  low ground stays under the street plane. These are scene settings; they do
  not change the extension’s default height sampling or existing seeds.

## Measured results

| Workload | Before | After | Improvement |
|---|---:|---:|---:|
| CPU compositor, 2048², 420 iterations | 26.34 ms | 8.97 ms | 2.9× |
| 256×256 cells, 4×4 chunks | 53.96 ms | 44.52 ms | 1.2× |
| 256×256 cells, 8×8 chunks | 221.53 ms | 44.99 ms | 4.9× |

Compositor values are medians of four runs in the same oracle executable;
chunk values are medians of five runs after warmup, using the before/after
extension builds. All eight chunk fingerprints match. Small grids showed
little benefit, so fewer than eight tiles keep parallelism within rows.

The final native end-to-end benchmark (96×96 cells, 2048² maps) measured
**69.28 ms CPU** and **101.69 ms GPU**, each a median of five warmed runs.
Both produced height SHA-256
`846f56c38e2722585953080a2020ef1d9f5837491126d4e38a8e139580d10787`.
This benchmark uses standard material settings, without the showcase's
mipmapped shader or capture overhead. The CPU wins on this integrated GPU;
profile both backends on the intended hardware.

## Reproducing measurements

Measured on an Intel Core i5-1145G7 (4 cores / 8 threads), Iris Xe,
Linux, Godot 4.7.2, GCC with `-O3` for the standalone compositor and
`template_debug` for the Godot benchmarks. These are local measurements,
not a promise for other machines or scenes. Pixel/geometry fingerprints must
match before comparing timings.

```bash
# CPU oracle and timing: compares directly against the unchanged dependency.
g++ -O3 -std=c++17 -pthread -Isrc -Icppdisplacementx/include \
  tests/native_cpu_contract.cpp cppdisplacementx/src/*.cpp \
  -o /tmp/procedural-city-native-contract
/tmp/procedural-city-native-contract

# Warmed medians, plus geometry and height fingerprints.
python3 scripts/benchmark.py

# Nine contracts, including layout rules and real GPU height/albedo/normal parity.
python3 scripts/check_runtime.py --gpu
```

The compositor contract covers odd dimensions, a one-pixel image, nonzero
initial pixels, multiple seeds, transparent sprites, clipping, and all four
sprite rotations. The normal/albedo parity test now reads the root
MeshInstance3D too; previously it could compare two empty arrays and pass.

## Further code that could be slimmer

1. **Legacy transport orchestration** (`src/core/proc_city_worker.cpp`,
   `src/cli/gpu_server.cpp`, `src/cli/rpc_client.cpp`). Introduce a small
   transport result type and one fallback coordinator. Binary resolution,
   config creation, backend reporting, and error handling currently repeat
   between the GPU, CPU, gRPC, and one-shot routes. Keep transport-specific
   lifecycle code separate and preserve the existing fallback order.
2. **Parameter serialization** (`src/cli/goplacementx_params_json.cpp`,
   `src/cli/goplacementx_params_proto.cpp`, `src/native/native_params.cpp`).
   Extend the existing parameter field list with serialization metadata so
   JSON, protobuf, and native assignments come from one definition. Preserve
   public property names, defaults, enum values, and serialized compatibility.
3. **Worker snapshots** (`src/core/proc_city_pipeline.cpp`,
   `src/core/proc_city_job.cpp`). A typed internal job/result structure could
   replace repeated string-key Dictionary lookups and conversions. Convert
   to Godot values only at the deferred-call boundary. This would mainly
   improve auditability and maintenance; do not assume a large speedup.
4. **Material preparation** (`src/core/proc_city_job.cpp`). Loading prepares
   individual maps, then finalization visits them again. Consolidate this
   into a single explicit preparation stage while retaining channel-mode
   composition and the shared-height copy. The existing size/mipmap guards
   already prevent most duplicate pixel work, so this is a cleanup priority.

The next larger performance experiments are a dedicated long-lived GPU worker
(to reuse its local RenderingDevice and pipeline safely) and GPU-resident
color/normal derivation to reduce readback/upload traffic. Both change resource
lifetime and synchronization substantially and need separate profiling and
fallback tests. A process-global RenderingDevice shared between arbitrary job
threads would be an unsafe shortcut.
