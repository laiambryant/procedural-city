#include "core/proc_city_job.h"

#include "cli/gdxraw_loader.h"
#include "core/proc_city_generator.h"
#include "material/city_material_builder.h"
#include "meshing/height_sampling.h"
#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <iterator>
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

static void add_emit(Array &r_emits, const String &p_mode, uint64_t p_seed, const String &p_path,
		const char *p_image_key, bool p_material, bool p_normal = false, bool p_compose_channel = false) {
	Dictionary e;
	e["mode"] = p_mode;
	e["seed"] = (int64_t)p_seed;
	e["path"] = p_path;
	e["image_key"] = p_image_key;
	e["material"] = p_material;
	e["normal"] = p_normal;
	e["compose_channel"] = p_compose_channel;
	r_emits.push_back(e);
}

// The four grayscale maps TEX_CHANNELS routes into R/G/B/Roughness, paired with
// the result key each one is recorded under.
static const char *CHANNEL_KEYS[4][3] = {
	{ "_r", "r_path", "r_image" },
	{ "_g", "g_path", "g_image" },
	{ "_b", "b_path", "b_image" },
	{ "_rough", "rough_path", "rough_image" },
};

static void add_channel_emits(Array &r_emits, const String &p_base, uint64_t p_base_seed, const String &p_ext, Dictionary &r_result) {
	for (uint64_t i = 0; i < std::size(CHANNEL_KEYS); i++) {
		const String out = p_base + String(CHANNEL_KEYS[i][0]) + p_ext;
		add_emit(r_emits, "grayscale", mix_seed(p_base_seed, i + 1), out,
				CHANNEL_KEYS[i][2], true, false, i < 3);
		r_result[CHANNEL_KEYS[i][1]] = out;
	}
}

static void add_albedo_normal_emits(Array &r_emits, const String &p_base, uint64_t p_base_seed, const String &p_ext, Dictionary &r_result) {
	const String albedo_out = p_base + String("_albedo") + p_ext;
	const String normal_out = p_base + String("_normal") + p_ext;
	add_emit(r_emits, "color", p_base_seed, albedo_out, "albedo_image", true);
	add_emit(r_emits, "normal", p_base_seed, normal_out, "normal_image", true, true);
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
		add_emit(emits, "grayscale", p_base_seed, out, "height_image", false);
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
	bool material;
	bool normal;
	bool compose_channel;
};

static const MapLoad MAP_LOADS[] = {
	{ "height_path", "height_image", "displacement", false, false, false },
	{ "albedo_path", "albedo_image", "material", true, false, false },
	{ "normal_path", "normal_image", "material", true, true, false },
	{ "r_path", "r_image", "material", true, false, true },
	{ "g_path", "g_image", "material", true, false, true },
	{ "b_path", "b_image", "material", true, false, true },
	{ "rough_path", "rough_image", "material", true, false, false },
};

// Disk fallbacks decode and prepare one map at a time. This deliberately gives
// up multi-map decode concurrency: with 8192 maps it avoids retaining six
// full-resolution images until the last decoder joins. In-memory server maps
// have already taken this same preparation path, and this idempotent pass also
// protects callers that supply an older unprepared server result.
bool godot::load_result_images(Dictionary &r_result, const Dictionary &p_images, int p_material_max_size,
		bool p_mipmaps, String &r_fail_stage, String &r_fail_message) {
	for (const MapLoad &load : MAP_LOADS) {
		if (!r_result.has(load.path_key)) {
			continue;
		}
		const String path = r_result[load.path_key];
		Ref<Image> image;
		if (p_images.has(path)) {
			image = p_images[path];
		} else {
			if (!FileAccess::file_exists(path)) {
				r_fail_stage = load.fail_stage;
				r_fail_message = "goplacementx did not produce " + path;
				return false;
			}
			image = load_map_image(path);
		}
		if (image.is_null()) {
			r_fail_stage = load.fail_stage;
			r_fail_message = "Failed to load " + path;
			return false;
		}
		if (load.material) {
			prepare_material_image(image, p_material_max_size,
					p_mipmaps && !load.compose_channel, load.normal);
		}
		r_result[load.image_key] = image;
	}
	return true;
}

static Ref<Image> result_image_or_null(const Dictionary &p_result, const char *p_key) {
	if (!p_result.has(p_key)) {
		return Ref<Image>();
	}
	Ref<Image> image = p_result[p_key];
	return image;
}

// Finalize cross-map material work on the worker. Independent maps were
// already resized as they arrived. Channel mode now composes its albedo here,
// so the main thread never generates it (or mips it) while installing a city.
// Shared mode takes a shading-only copy of height: geometry keeps the original
// full-resolution image while roughness obeys the material upload cap.
void godot::prepare_material_images(Dictionary &r_result, int p_max_size, bool p_mipmaps) {
	const int texture_mode = (int)r_result.get("texture_mode", ProcCityGenerator::TEX_SINGLE);
	prepare_material_image(result_image_or_null(r_result, "albedo_image"), p_max_size, p_mipmaps);
	prepare_material_image(result_image_or_null(r_result, "normal_image"), p_max_size, p_mipmaps, true);
	prepare_material_image(result_image_or_null(r_result, "rough_image"), p_max_size, p_mipmaps);

	if (texture_mode == ProcCityGenerator::TEX_SHARED) {
		Ref<Image> height = result_image_or_null(r_result, "height_image");
		if (height.is_null()) {
			height = result_image_or_null(r_result, "shared_height_image");
		}
		if (height.is_valid() && !height->is_empty()) {
			Ref<Image> roughness = height->duplicate();
			prepare_material_image(roughness, p_max_size, p_mipmaps);
			r_result["rough_image"] = roughness;
		}
		r_result.erase("shared_height_image");
		return;
	}
	if (texture_mode == ProcCityGenerator::TEX_CHANNELS) {
		Ref<Image> red = result_image_or_null(r_result, "r_image");
		Ref<Image> green = result_image_or_null(r_result, "g_image");
		Ref<Image> blue = result_image_or_null(r_result, "b_image");
		prepare_material_image(red, p_max_size, false);
		prepare_material_image(green, p_max_size, false);
		prepare_material_image(blue, p_max_size, false);
		Ref<Image> albedo = compose_rgb_albedo(red, green, blue);
		if (albedo.is_valid()) {
			prepare_material_image(albedo, p_max_size, p_mipmaps);
			r_result["albedo_image"] = albedo;
			r_result.erase("r_image");
			r_result.erase("g_image");
			r_result.erase("b_image");
		}
	}
}

static PackedFloat32Array pack_cell_heights(const std::vector<float> &p_heights) {
	PackedFloat32Array packed;
	packed.resize((int64_t)p_heights.size());
	if (!p_heights.empty()) {
		memcpy(packed.ptrw(), p_heights.data(), p_heights.size() * sizeof(float));
	}
	return packed;
}

static Dictionary cell_height_result(const Dictionary &p_job, const std::vector<float> &p_heights) {
	const CellGrid grid = make_cell_grid(p_job["mesh_size"], p_job["grid_vertices"]);
	Dictionary result;
	result["columns"] = grid.cols;
	result["rows"] = grid.rows;
	result["cell_size"] = Vector2(grid.cw, grid.cd);
	result["origin"] = Vector2(grid.ox, grid.oz);
	result["heights"] = pack_cell_heights(p_heights);
	return result;
}

// build_job_meshes returns every mesh the job's build mode produces. Hex is
// always one mesh; blocks honour geometry_chunks, so a chunked city has its
// tiles built on the worker exactly like the single mesh they replace.
static TypedArray<ArrayMesh> build_job_meshes(const Dictionary &p_job, const Ref<Image> &p_height, int p_mode,
		Dictionary &r_result) {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	TypedArray<ArrayMesh> out;
	if (p_mode == ProcCityGenerator::BUILD_HEX) {
		Ref<ArrayMesh> hex = mesher->build_hex_mesh(p_height, p_job["mesh_size"], p_job["grid_vertices"],
				p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
				p_job["hive_warp"], p_job["hive_jitter"], p_job["hive_gap"], (int64_t)p_job["seed"],
				p_job["hive_flat_rect"], p_job["hive_rim_boost"], p_job["hive_rim_falloff"],
				p_job["height_power"], p_job["ao_strength"], p_job["color_variation"], p_job["hive_floor"]);
		if (hex.is_valid()) {
			out.push_back(hex);
		}
		return out;
	}

	std::vector<float> heights;
	const int chunks = (int)p_job.get("geometry_chunks", 1);
	if (chunks > 1) {
		const std::vector<Ref<ArrayMesh>> meshes = mesher->build_array_mesh_chunks(
				p_height, p_job["mesh_size"], p_job["grid_vertices"],
				p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
				p_job["height_power"], p_job["block_inset"], (int64_t)p_job["seed"],
				p_job["ao_strength"], p_job["color_variation"], p_job["clip_below_height"], chunks, heights);
		for (const Ref<ArrayMesh> &mesh : meshes) {
			out.push_back(mesh);
		}
	} else {
		Ref<ArrayMesh> mesh = mesher->build_array_mesh_with_heights(
				p_height, p_job["mesh_size"], p_job["grid_vertices"],
				p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
				p_job["height_power"], p_job["block_inset"], (int64_t)p_job["seed"],
				p_job["ao_strength"], p_job["color_variation"], p_job["clip_below_height"], heights);
		if (mesh.is_valid()) {
			out.push_back(mesh);
		}
	}
	if (out.size() > 0) {
		r_result["cell_heights"] = cell_height_result(p_job, heights);
	}
	return out;
}

bool godot::build_worker_mesh(const Dictionary &p_job, Dictionary &r_result) {
	if (!r_result.has("height_image")) {
		return true;
	}
	const int mode = p_job["build_mode"];
	if (mode != ProcCityGenerator::BUILD_HEX && mode != ProcCityGenerator::BUILD_ARRAY_MESH) {
		return true;
	}

	const TypedArray<ArrayMesh> meshes = build_job_meshes(p_job, r_result["height_image"], mode, r_result);
	if (meshes.is_empty()) {
		return false;
	}
	r_result["geometry_meshes"] = meshes;
	return true;
}
