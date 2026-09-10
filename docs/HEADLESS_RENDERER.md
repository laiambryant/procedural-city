# Headless renderer: concurrent mesh creation

Observed locally on **Godot 4.7.2, Linux**, while running
`demo/tests/bench_chunks.gd` with `--headless` on 9 September 2026.

An experimental chunk optimization created and committed an `ArrayMesh` from
several `std::thread` workers concurrently. The headless dummy renderer reported:

```text
Attempting to initialize the wrong RID
Parameter "mem" is null.
Attempting to use an uninitialized RID
Parameter "m" is null.
```

The stack locations included `core/templates/rid_owner.h` (`initialize_rid`,
`get_or_null`) and `servers/rendering/dummy/storage/mesh_storage.h`
(`mesh_set_blend_shape_mode`, `mesh_set_blend_shape_count`, `mesh_add_surface`,
`mesh_get_surface`). Some surfaces were missing, and the geometry fingerprint
assertion failed. A successful Vulkan run alone would not establish that this
path also works headlessly. The underlying engine implementation was not
patched or independently audited; this records the observed behavior rather
than claiming all Godot rendering backends have the same limitation.

## Repository workaround

`src/meshing/block_mesher.cpp` separates `build_rect_surface()` from
`commit_surface()`:

1. Worker tasks fill their own packed vertex/index arrays. They do not create
   ArrayMesh resources or call RenderingServer.
2. After joining, the calling meshing thread commits the completed surfaces
   sequentially, in original chunk order.
3. Batches are bounded by hardware concurrency to limit staging memory.

The calling thread can itself be the generator's pipeline worker; this is not
an instruction to move scene-tree mutations off the main thread. It prevents
concurrent renderer calls **within this chunk build**, and does not establish
thread safety for multiple independent generators committing concurrently.

Verified with the headless chunk benchmark and chunk/collision/occluder/query
contract, plus a real-device CPU/GPU pixel-parity run. Before/after geometry
fingerprints are listed alongside the timings in [PERFORMANCE.md](PERFORMANCE.md).
Keep the headless tests when changing meshing concurrency; do not move
`PackedSurface::commit()` back into the chunk workers without revalidating this.
