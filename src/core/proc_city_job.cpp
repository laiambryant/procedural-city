#include "core/proc_city_job.h"

#include "cli/gdxraw_loader.h"
#include "core/proc_city_generator.h"
#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>

#include <iterator>
#include <thread>
#include <vector>

using namespace godot;

// Weyl-sequence increment from splitmix64 (2^64 / phi): multiplying a small
// index by it spreads consecutive indices uniformly over the 64-bit range.
static constexpr uint64_t GOLDEN_GAMMA_64 = 0x9E3779B97F4A7C15ULL;

// mix_seed derives an independent-but-deterministic seed from a base seed and a
// small index, so the per-channel maps differ while staying reproducible.
static uint64_t mix_seed(uint64_t p_base, uint64_t p_index) {
	return p_base ^ (p_index * GOLDEN_GAMMA_64);
}

static void add_emit(Array &r_emits, const String &p_mode, uint64_t p_seed, const String &p_path) {
	Dictionary e;
	e["mode"] = p_mode;
	e["seed"] = (int64_t)p_seed;
	e["path"] = p_path;
	r_emits.push_back(e);
}

// The four grayscale maps TEX_CHANNELS routes into R/G/B/Roughness, paired with
// the result key each one is recorded under.
static const char *CHANNEL_KEYS[4][2] = {
	{ "_r", "r_path" },
	{ "_g", "g_path" },
	{ "_b", "b_path" },
	{ "_rough", "rough_path" },
};

static void add_channel_emits(Array &r_emits, const String &p_base, uint64_t p_base_seed, const String &p_ext, Dictionary &r_result) {
	for (uint64_t i = 0; i < std::size(CHANNEL_KEYS); i++) {
		const String out = p_base + String(CHANNEL_KEYS[i][0]) + p_ext;
		add_emit(r_emits, "grayscale", mix_seed(p_base_seed, i + 1), out);
		r_result[CHANNEL_KEYS[i][1]] = out;
	}
}

static void add_albedo_normal_emits(Array &r_emits, const String &p_base, uint64_t p_base_seed, const String &p_ext, Dictionary &r_result) {
	const String albedo_out = p_base + String("_albedo") + p_ext;
	const String normal_out = p_base + String("_normal") + p_ext;
	add_emit(r_emits, "color", p_base_seed, albedo_out);
	add_emit(r_emits, "normal", p_base_seed, normal_out);
	r_result["albedo_path"] = albedo_out;
	r_result["normal_path"] = normal_out;
}

Array godot::plan_bundle_emits(const String &p_dir, uint64_t p_base_seed, bool p_want_height, bool p_want_material,
							   int p_texture_mode, const String &p_ext, Dictionary &r_result) {
	const uint64_t stamp = Time::get_singleton()->get_ticks_usec();
	const String base = p_dir.path_join("proc_city_" + String::num_uint64(stamp));

	Array emits;
	if (p_want_height) {
		const String out = base + String("_height") + p_ext;
		add_emit(emits, "grayscale", p_base_seed, out);
		r_result["height_path"] = out;
	}
	if (!p_want_material) {
		return emits;
	}
	if (p_texture_mode == ProcCityGenerator::TEX_CHANNELS) {
		add_channel_emits(emits, base, p_base_seed, p_ext, r_result);
	} else {
		add_albedo_normal_emits(emits, base, p_base_seed, p_ext, r_result);
	}
	return emits;
}

// MapLoad names one CLI output: where its path was recorded, where the decoded
// image belongs, and which pipeline stage owns the failure if it is missing.
struct MapLoad {
	const char *path_key;
	const char *image_key;
	const char *fail_stage;
};

static const MapLoad MAP_LOADS[] = {
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

// collect_pending_loads takes the maps the GPU server already handed back
// in-memory and leaves only the ones still to be read off disk.
static bool collect_pending_loads(Dictionary &r_result, const Dictionary &p_images, std::vector<PendingLoad> &r_pending,
								  String &r_fail_stage, String &r_fail_message) {
	for (const MapLoad &ml : MAP_LOADS) {
		if (!r_result.has(ml.path_key)) {
			continue;
		}
		const String path = r_result[ml.path_key];
		if (p_images.has(path)) {
			r_result[ml.image_key] = p_images[path];
			continue;
		}
		if (!FileAccess::file_exists(path)) {
			r_fail_stage = ml.fail_stage;
			r_fail_message = "goplacementx did not produce " + path;
			return false;
		}
		PendingLoad load;
		load.spec = &ml;
		load.path = path;
		r_pending.push_back(load);
	}
	return true;
}

// decode_concurrently gives each map its own thread. Safe because every thread
// writes only its own PendingLoad slot, and the vector is not resized here.
static void decode_concurrently(std::vector<PendingLoad> &r_pending) {
	std::vector<std::thread> decoders;
	decoders.reserve(r_pending.size());
	for (PendingLoad &load : r_pending) {
		decoders.emplace_back([&load]() { load.image = load_map_image(load.path); });
	}
	for (std::thread &decoder : decoders) {
		decoder.join();
	}
}

bool godot::load_result_images(Dictionary &r_result, const Dictionary &p_images, String &r_fail_stage, String &r_fail_message) {
	std::vector<PendingLoad> pending;
	if (!collect_pending_loads(r_result, p_images, pending, r_fail_stage, r_fail_message)) {
		return false;
	}
	decode_concurrently(pending);

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

static Ref<ArrayMesh> build_job_mesh(const Dictionary &p_job, const Ref<Image> &p_height, int p_mode) {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	if (p_mode == ProcCityGenerator::BUILD_HEX) {
		return mesher->build_hex_mesh(p_height, p_job["mesh_size"], p_job["grid_vertices"],
									  p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
									  p_job["hive_warp"], p_job["hive_jitter"], p_job["hive_gap"], (int64_t)p_job["seed"],
									  p_job["hive_flat_rect"], p_job["hive_rim_boost"], p_job["hive_rim_falloff"],
									  p_job["height_power"], p_job["ao_strength"], p_job["color_variation"], p_job["hive_floor"]);
	}
	return mesher->build_array_mesh(p_height, p_job["mesh_size"], p_job["grid_vertices"],
									p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
									p_job["height_power"], p_job["block_inset"],
									(int64_t)p_job["seed"], p_job["ao_strength"], p_job["color_variation"]);
}

bool godot::build_worker_mesh(const Dictionary &p_job, Dictionary &r_result) {
	if (!r_result.has("height_image")) {
		return true;
	}
	const int mode = p_job["build_mode"];
	if (mode != ProcCityGenerator::BUILD_HEX && mode != ProcCityGenerator::BUILD_ARRAY_MESH) {
		return true;
	}

	Ref<ArrayMesh> mesh = build_job_mesh(p_job, r_result["height_image"], mode);
	if (mesh.is_null()) {
		return false;
	}
	r_result["geometry_mesh"] = mesh;
	return true;
}
