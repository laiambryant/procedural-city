#ifndef PROC_CITY_JOB_H
#define PROC_CITY_JOB_H

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// The worker-thread half of the pipeline: everything that runs off the main
// thread between "job payload in" and "result Dictionary out". None of it may
// touch the scene tree.

// plan_bundle_emits assembles the emit list for a single bundled CLI invocation
// and records each output path on the result, keyed so the main thread can load
// and later delete the files. p_ext is the caller's choice of interchange
// format (see map_extension_for), which depends on the CLI that will run, so
// this has to be re-planned if the pipeline switches binaries mid-run.
Array plan_bundle_emits(const String &p_dir, uint64_t p_base_seed, bool p_want_height, bool p_want_material,
		int p_texture_mode, const String &p_ext, Dictionary &r_result);

// load_result_images fills each result image slot from the maps produced by
// plan_bundle_emits. Maps returned in-memory by the GPU server (keyed by their
// echoed path in p_images) are used directly; the rest are decoded from disk
// one at a time and material maps are prepared before retention. On failure it
// reports which pipeline stage broke.
bool load_result_images(Dictionary &r_result, const Dictionary &p_images, int p_material_max_size,
		bool p_mipmaps, String &r_fail_stage, String &r_fail_message);

// Resize material-only maps and optionally generate mipmaps on the worker.
// The height map is deliberately excluded so geometry sampling stays exact.
void prepare_material_images(Dictionary &r_result, int p_max_size, bool p_mipmaps);

// build_worker_mesh builds ArrayMesh-backed geometry off the main thread. This
// is safe because the mesher only touches worker-owned Image/ArrayMesh
// resources (surface creation goes through the RenderingServer's thread-safe
// command queue) and the Ref crosses back inside the result Dictionary. CSG,
// MultiMesh and GridMap stay on the main thread (node-tree surgery).
bool build_worker_mesh(const Dictionary &p_job, Dictionary &r_result);

} // namespace godot

#endif // PROC_CITY_JOB_H
