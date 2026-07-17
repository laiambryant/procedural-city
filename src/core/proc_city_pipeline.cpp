#include "core/proc_city_generator.h"

#include "cli/gdxraw_loader.h"
#include "cli/goplacementx_runner.h"
#include "cli/gpu_server.h"
#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <iterator>
#include <thread>
#include <vector>

using namespace godot;

// Weyl-sequence increment from splitmix64 (2^64 / phi): multiplying a small
// index by it spreads consecutive indices uniformly over the 64-bit range.
static constexpr uint64_t GOLDEN_GAMMA_64 = 0x9E3779B97F4A7C15ULL;

// Ratio reported on generation_progress once the height stage of a combined
// run has finished.
static constexpr double PROGRESS_HEIGHT_DONE = 0.5;

// mix_seed derives an independent-but-deterministic seed from a base seed and a
// small index, so the per-channel maps differ while staying reproducible.
static uint64_t mix_seed(uint64_t p_base, uint64_t p_index) {
	return p_base ^ (p_index * GOLDEN_GAMMA_64);
}

static Ref<Image> image_or_null(const Dictionary &p_result, const char *p_key) {
	if (p_result.has(p_key)) {
		return p_result[p_key];
	}
	return Ref<Image>();
}

static void remove_file(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}
	Ref<DirAccess> da = DirAccess::open(p_path.get_base_dir());
	if (da.is_valid()) {
		da->remove(p_path.get_file());
	}
}

static void add_emit(Array &r_emits, const String &p_mode, uint64_t p_seed, const String &p_path) {
	Dictionary e;
	e["mode"] = p_mode;
	e["seed"] = (int64_t)p_seed;
	e["path"] = p_path;
	r_emits.push_back(e);
}

// plan_bundle_emits assembles the emit list for a single bundled CLI invocation
// and records each output path on the result, keyed so the main thread can load
// and later delete the files. Intermediates default to the .gdxraw interchange
// format (no PNG encode/decode round-trip); kept intermediates stay PNG so
// they remain inspectable.
static Array plan_bundle_emits(const String &p_dir, uint64_t p_base_seed,
							   bool p_want_height, bool p_want_material, int p_texture_mode, const String &p_ext, Dictionary &r_result) {
	const uint64_t stamp = Time::get_singleton()->get_ticks_usec();
	const String base = p_dir.path_join("proc_city_" + String::num_uint64(stamp));

	Array emits;
	if (p_want_height) {
		const String out = base + String("_height") + p_ext;
		add_emit(emits, "grayscale", p_base_seed, out);
		r_result["height_path"] = out;
	}
	if (p_want_material) {
		if (p_texture_mode == ProcCityGenerator::TEX_CHANNELS) {
			static const char *CHANNEL_KEYS[4][2] = {
				{ "_r", "r_path" },
				{ "_g", "g_path" },
				{ "_b", "b_path" },
				{ "_rough", "rough_path" },
			};
			for (uint64_t i = 0; i < std::size(CHANNEL_KEYS); i++) {
				const String out = base + String(CHANNEL_KEYS[i][0]) + p_ext;
				add_emit(emits, "grayscale", mix_seed(p_base_seed, i + 1), out);
				r_result[CHANNEL_KEYS[i][1]] = out;
			}
		} else {
			const String albedo_out = base + String("_albedo") + p_ext;
			const String normal_out = base + String("_normal") + p_ext;
			add_emit(emits, "color", p_base_seed, albedo_out);
			add_emit(emits, "normal", p_base_seed, normal_out);
			r_result["albedo_path"] = albedo_out;
			r_result["normal_path"] = normal_out;
		}
	}
	return emits;
}

// load_result_images fills each result image slot from the maps produced by
// plan_bundle_emits. Maps returned in-memory by the GPU server (keyed by their
// echoed path in p_images) are used directly; the rest are decoded from disk
// concurrently (each thread fills only its own slot). On failure it reports
// which pipeline stage broke.
static bool load_result_images(Dictionary &r_result, const Dictionary &p_images, String &r_fail_stage, String &r_fail_message) {
	struct MapLoad {
		const char *path_key;
		const char *image_key;
		const char *fail_stage;
	};
	static const MapLoad loads[] = {
		{ "height_path", "height_image", "displacement" },
		{ "albedo_path", "albedo_image", "material" },
		{ "normal_path", "normal_image", "material" },
		{ "r_path", "r_image", "material" },
		{ "g_path", "g_image", "material" },
		{ "b_path", "b_image", "material" },
		{ "rough_path", "rough_image", "material" },
	};

	struct PendingLoad {
		const MapLoad *spec = nullptr;
		String path;
		Ref<Image> image;
	};
	std::vector<PendingLoad> pending;
	for (const MapLoad &ml : loads) {
		if (!r_result.has(ml.path_key)) {
			continue;
		}
		const String path = r_result[ml.path_key];
		if (p_images.has(path)) {
			r_result[ml.image_key] = p_images[path];
			continue;
		}
		PendingLoad load;
		load.spec = &ml;
		load.path = path;
		if (!FileAccess::file_exists(load.path)) {
			r_fail_stage = ml.fail_stage;
			r_fail_message = "goplacementx did not produce " + load.path;
			return false;
		}
		pending.push_back(load);
	}

	std::vector<std::thread> decoders;
	decoders.reserve(pending.size());
	for (PendingLoad &load : pending) {
		decoders.emplace_back([&load]() { load.image = load_map_image(load.path); });
	}
	for (std::thread &decoder : decoders) {
		decoder.join();
	}

	for (PendingLoad &load : pending) {
		if (load.image.is_null()) {
			r_fail_stage = load.spec->fail_stage;
			r_fail_message = "Failed to load " + load.path;
			return false;
		}
		r_result[load.spec->image_key] = load.image;
	}
	return true;
}

// build_worker_mesh builds ArrayMesh-backed geometry off the main thread. This
// is safe because the mesher only touches worker-owned Image/ArrayMesh
// resources (surface creation goes through the RenderingServer's thread-safe
// command queue) and the Ref crosses back inside the result Dictionary. CSG and
// MultiMesh stay on the main thread (node-tree surgery / conservative).
static bool build_worker_mesh(const Dictionary &p_job, Dictionary &r_result) {
	if (!r_result.has("height_image")) {
		return true;
	}
	const int mode = p_job["build_mode"];
	if (mode != ProcCityGenerator::BUILD_HEX && mode != ProcCityGenerator::BUILD_ARRAY_MESH) {
		return true;
	}

	Ref<Image> height_img = r_result["height_image"];
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> mesh;
	if (mode == ProcCityGenerator::BUILD_HEX) {
		mesh = mesher->build_hex_mesh(height_img, p_job["mesh_size"], p_job["grid_vertices"],
									  p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
									  p_job["hive_warp"], p_job["hive_jitter"], p_job["hive_gap"], (int64_t)p_job["seed"],
									  p_job["hive_flat_rect"], p_job["hive_rim_boost"], p_job["hive_rim_falloff"],
									  p_job["height_power"], p_job["ao_strength"], p_job["color_variation"], p_job["hive_floor"]);
	} else {
		mesh = mesher->build_array_mesh(height_img, p_job["mesh_size"], p_job["grid_vertices"],
										p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
										p_job["height_power"], p_job["block_inset"],
										(int64_t)p_job["seed"], p_job["ao_strength"], p_job["color_variation"]);
	}
	if (mesh.is_null()) {
		return false;
	}
	r_result["geometry_mesh"] = mesh;
	return true;
}

void ProcCityGenerator::_start_pipeline(int p_stages, bool p_fresh_seed) {
	_ensure_params();

	if (_busy) {
		_emit_failed("busy", "A generation is already in progress.");
		return;
	}
	if ((p_stages & STAGE_MATERIAL) && !(p_stages & STAGE_HEIGHT) && _height_image.is_null()) {
		_emit_failed("material", "Run Generate Displacement first.");
		return;
	}
	if (p_stages & STAGE_GEOMETRY) {
		const String budget_error = _cell_budget_error();
		if (!budget_error.is_empty()) {
			_emit_failed("geometry", budget_error);
			return;
		}
	}

	if (p_fresh_seed) {
		Ref<GoplacementxRunner> runner;
		runner.instantiate();
		_resolved_seed = runner->resolve_seed(params);
	}

	String label = "all";
	if ((p_stages & STAGE_HEIGHT) && !(p_stages & STAGE_MATERIAL)) {
		label = "displacement";
	} else if (!(p_stages & STAGE_HEIGHT) && (p_stages & STAGE_MATERIAL)) {
		label = "material";
	}
	emit_signal("generation_started", label);

	_busy = true;
	_worker.instantiate();
	_worker->start(Callable(this, "_thread_body").bind(_snapshot_job(p_stages)));
}

// _snapshot_job copies every input the worker needs into the job payload: the
// worker must never read member fields the main thread could mutate mid-run.
Dictionary ProcCityGenerator::_snapshot_job(int p_stages) const {
	Dictionary job;
	job["params"] = params;
	job["stages"] = p_stages;
	job["generation_mode"] = generation_mode;
	job["seed"] = _resolved_seed;
	job["texture_mode"] = texture_mode;
	job["dir"] = _resolve_output_dir();
	job["binary_override"] = binary_path_override;
	job["auto_download"] = auto_download_binary;
	job["use_gpu_server"] = use_gpu_server;
	job["keep_intermediate_png"] = keep_intermediate_png;
	job["build_mode"] = build_mode;
	job["mesh_size"] = mesh_size;
	job["grid_vertices"] = grid_vertices;
	job["height_scale"] = height_scale;
	job["base_height"] = base_height;
	job["height_power"] = height_power;
	job["block_inset"] = block_inset;
	job["sample_filter"] = sample_filter;
	job["hive_warp"] = hive_warp;
	job["hive_jitter"] = hive_jitter;
	job["hive_gap"] = hive_gap;
	job["hive_flat_rect"] = hive_flat_rect;
	job["hive_rim_boost"] = hive_rim_boost;
	job["hive_rim_falloff"] = hive_rim_falloff;
	job["hive_floor"] = hive_floor;
	job["ao_strength"] = ao_strength;
	job["color_variation"] = color_variation;
	job["map_extension"] = map_extension_for(keep_intermediate_png);
	return job;
}

static String resolve_generation_binary(const Ref<GoplacementxRunner> &p_runner, const Dictionary &p_job, int &r_used_mode) {
	const String override_path = p_job["binary_override"];
	const bool allow_download = p_job["auto_download"];
	if ((int)p_job.get("generation_mode", ProcCityGenerator::GEN_CPU) == ProcCityGenerator::GEN_GPU) {
		p_runner->set_cli_kind(GoplacementxRunner::CLI_GPUDISPLACEMENTX);
		const String gpu_binary = p_runner->ensure_binary(override_path, allow_download);
		if (!gpu_binary.is_empty()) {
			r_used_mode = ProcCityGenerator::GEN_GPU;
			return gpu_binary;
		}
		UtilityFunctions::push_warning("[ProcCity] gpudisplacementx CLI not found and the GitHub download did not succeed - falling back to the CPU pipeline (binary_path_override is ignored for the fallback).");
		p_runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
		r_used_mode = ProcCityGenerator::GEN_CPU;
		return p_runner->ensure_binary(String(), allow_download);
	}
	p_runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
	r_used_mode = ProcCityGenerator::GEN_CPU;
	return p_runner->ensure_binary(override_path, allow_download);
}

static const char *cli_display_name(int p_generation_mode) {
	return p_generation_mode == ProcCityGenerator::GEN_GPU ? "gpudisplacementx" : "godisplacementx";
}

// run_generation_bundle prefers the persistent GPU server when the node opted
// in and the generation resolved to GPU; any server failure (process not up,
// pipe closed, per-request error) transparently falls back to the one-shot CLI,
// which itself still handles the exit-2 CPU fallback downstream.
static Dictionary run_generation_bundle(const Ref<GoplacementxRunner> &p_runner, const String &p_binary,
										const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params,
										int p_used_mode, bool p_use_server) {
	if (p_used_mode == ProcCityGenerator::GEN_GPU && p_use_server) {
		ProcCityGpuServer *server = ProcCityGpuServer::get_singleton();
		if (server && server->ensure_started(p_binary)) {
			const Dictionary served = server->run_bundle(p_config, p_emits, p_params);
			if ((int)served.get("code", 1) == 0) {
				return served;
			}
			UtilityFunctions::push_warning("[ProcCity] GPU server request failed (" + String(served.get("output", "")) + ") - retrying with the one-shot gpudisplacementx CLI.");
		} else {
			UtilityFunctions::push_warning("[ProcCity] GPU server did not start - using the one-shot gpudisplacementx CLI.");
		}
	}
	return p_runner->run_bundle(p_binary, p_config, p_emits, p_params);
}

void ProcCityGenerator::_thread_body(Dictionary p_job) {
	Ref<GoplacementxParams> p = p_job["params"];
	const int stages = p_job["stages"];
	const String dir = p_job["dir"];

	Ref<GoplacementxRunner> runner;
	runner.instantiate();

	int used_mode = GEN_CPU;
	String binary = resolve_generation_binary(runner, p_job, used_mode);
	if (binary.is_empty()) {
		call_deferred("_emit_failed", "setup", "godisplacementx CLI not found and the GitHub download did not succeed (offline? no release yet?). Set Binary Path Override or drop the binary under addons/procedural_city/godisplacementx/<platform>/.");
		return;
	}

	const String config = runner->write_config(dir, p);
	if (config.is_empty()) {
		call_deferred("_emit_failed", "setup", "Could not write config JSON into " + dir);
		return;
	}

	Dictionary result;
	result["stages"] = stages;
	result["seed"] = p_job["seed"];
	result["texture_mode"] = p_job["texture_mode"];
	result["dir"] = dir;
	result["config_path"] = config;

	const Array emits = plan_bundle_emits(dir, (uint64_t)(int64_t)p_job["seed"],
										  stages & STAGE_HEIGHT, stages & STAGE_MATERIAL, p_job["texture_mode"],
										  p_job["map_extension"], result);

	const bool use_server = (bool)p_job.get("use_gpu_server", false) && !(bool)p_job.get("keep_intermediate_png", false);
	Dictionary run = run_generation_bundle(runner, binary, config, emits, p, used_mode, use_server);
	if (used_mode == GEN_GPU && (int)run["code"] == 2) {
		UtilityFunctions::push_warning("[ProcCity] gpudisplacementx reported no usable GPU adapter (exit 2) - falling back to the CPU pipeline.");
		runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
		binary = runner->ensure_binary(String(), p_job["auto_download"]);
		if (binary.is_empty()) {
			call_deferred("_emit_failed", "setup", "godisplacementx CLI not found for the CPU fallback (offline? no release yet?). Drop the binary under addons/procedural_city/godisplacementx/<platform>/.");
			return;
		}
		used_mode = GEN_CPU;
		run = runner->run_bundle(binary, config, emits, p);
	}
	if ((int)run["code"] != 0) {
		String message = String(run.get("output", ""));
		if (message.strip_edges().is_empty()) {
			message = String(cli_display_name(used_mode)) + " exited with code " + String::num_int64((int)run.get("code", -1));
		}
		call_deferred("_emit_failed", (stages & STAGE_MATERIAL) ? "material" : "displacement", message);
		return;
	}
	result["generation_mode_used"] = used_mode;

	String fail_stage;
	String fail_message;
	if (!load_result_images(result, run.get("images", Dictionary()), fail_stage, fail_message)) {
		call_deferred("_emit_failed", fail_stage, fail_message);
		return;
	}

	if ((stages & STAGE_GEOMETRY) && !build_worker_mesh(p_job, result)) {
		call_deferred("_emit_failed", "geometry", "Worker mesh build failed.");
		return;
	}

	call_deferred("_apply_results", result);
}

void ProcCityGenerator::_apply_results(Dictionary p_result) {
	const int stages = p_result["stages"];

	if (p_result.has("height_image")) {
		_height_image = p_result["height_image"];
		_last_height_path = String(p_result.get("height_path", ""));
	}
	_resolved_seed = (int64_t)p_result["seed"];
	_last_generation_used = (int)p_result.get("generation_mode_used", GEN_CPU);

	if (stages & STAGE_HEIGHT) {
		emit_signal("generation_finished", "displacement", _last_height_path);
		emit_signal("generation_progress", "all", PROGRESS_HEIGHT_DONE);
	}

	bool geometry_ok = true;
	if (stages & STAGE_GEOMETRY) {
		geometry_ok = _apply_result_geometry(p_result);
		if (geometry_ok) {
			emit_signal("generation_finished", "geometry", String());
		}
	}

	if (stages & STAGE_MATERIAL) {
		_store_result_images(p_result);
	}

	if ((stages & STAGE_APPLY_MATERIAL) && geometry_ok) {
		_apply_material_main();
		emit_signal("generation_finished", "material", _last_albedo_path);
	}

	_cleanup_temp(p_result);
	_finish_worker();

	emit_signal("generation_progress", "all", 1.0);
	emit_signal("all_finished");
}

void ProcCityGenerator::_store_result_images(const Dictionary &p_result) {
	_material_texture_mode = (int)p_result.get("texture_mode", texture_mode);
	_albedo_image = image_or_null(p_result, "albedo_image");
	_normal_image = image_or_null(p_result, "normal_image");
	_r_image = image_or_null(p_result, "r_image");
	_g_image = image_or_null(p_result, "g_image");
	_b_image = image_or_null(p_result, "b_image");
	_rough_image = image_or_null(p_result, "rough_image");
	_last_albedo_path = String(p_result.get("albedo_path", p_result.get("r_path", "")));
	_last_normal_path = String(p_result.get("normal_path", ""));
}

void ProcCityGenerator::_finish_worker() {
	if (_worker.is_valid()) {
		if (_worker->is_started()) {
			_worker->wait_to_finish();
		}
		_worker.unref();
	}
	_busy = false;
}

void ProcCityGenerator::_cleanup_temp(const Dictionary &p_result) {
	if (keep_intermediate_png) {
		return;
	}
	static const char *TEMP_PATH_KEYS[] = {
		"height_path", "albedo_path", "normal_path",
		"r_path", "g_path", "b_path", "rough_path", "config_path"
	};
	for (const char *key : TEMP_PATH_KEYS) {
		remove_file(String(p_result.get(key, "")));
	}
}

void ProcCityGenerator::_emit_failed(String p_stage, String p_message) {
	_finish_worker();
	UtilityFunctions::push_error(String("[ProcCity] ") + p_stage + String(": ") + p_message);
	emit_signal("generation_failed", p_stage, p_message);
}
