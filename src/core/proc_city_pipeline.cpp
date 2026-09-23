#include "core/proc_city_generator.h"

#include "cli/goplacementx_runner.h"
#include "core/proc_city_log.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static constexpr double PROGRESS_HEIGHT_DONE = 0.5;

static Ref<Image> image_or_null(const Dictionary &p_result, const char *p_key) {
	if (p_result.has(p_key)) {
		return p_result[p_key];
	}
	return Ref<Image>();
}

static Ref<GoplacementxParams> snapshot_params_for_worker(const Ref<GoplacementxParams> &p_params) {
	Ref<Resource> duplicate = p_params->duplicate(true);
	return Ref<GoplacementxParams>(duplicate);
}

static bool job_needs_existing_height_image(int p_stages, bool p_height_image_valid) {
	return (p_stages & ProcCityGenerator::STAGE_MATERIAL) && !(p_stages & ProcCityGenerator::STAGE_HEIGHT) && p_height_image_valid;
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

String ProcCityGenerator::_pipeline_label(int p_stages) {
	if ((p_stages & STAGE_HEIGHT) && !(p_stages & STAGE_MATERIAL)) {
		return "displacement";
	}
	if (!(p_stages & STAGE_HEIGHT) && (p_stages & STAGE_MATERIAL)) {
		return "material";
	}
	return "all";
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

	emit_signal("generation_started", _pipeline_label(p_stages));
	_pipeline_started_usec = Time::get_singleton()->get_ticks_usec();
	log_pipeline_event("generation started: " + _pipeline_label(p_stages) + " (" +
			generation_mode_name(generation_mode) + ", resolution " + String::num_int64(params->get_resolution()) + ")");

	_busy = true;
	_worker.instantiate();
	_worker->start(Callable(this, "_thread_body").bind(_snapshot_job(p_stages)));
}

Dictionary ProcCityGenerator::_snapshot_job(int p_stages) const {
	Dictionary job;
	job["params"] = snapshot_params_for_worker(params);
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
	job["geometry_chunks"] = geometry_chunks;
	job["mesh_size"] = mesh_size;
	job["grid_vertices"] = grid_vertices;
	job["height_scale"] = height_scale;
	job["base_height"] = base_height;
	job["height_power"] = height_power;
	job["block_inset"] = block_inset;
	job["clip_below_height"] = clip_below_height;
	job["sample_filter"] = sample_filter;
	job["hive_warp"] = hive_warp;
	job["hive_jitter"] = hive_jitter;
	job["hive_gap"] = hive_gap;
	job["hive_flat_rect"] = hive_flat_rect;
	job["carve_rects"] = carve_rects.duplicate();
	job["hive_rim_boost"] = hive_rim_boost;
	job["hive_rim_falloff"] = hive_rim_falloff;
	job["hive_floor"] = hive_floor;
	job["ao_strength"] = ao_strength;
	job["color_variation"] = color_variation;
	job["material_max_size"] = material_max_size;
	job["texture_filter"] = texture_filter;
	if (job_needs_existing_height_image(p_stages, _height_image.is_valid())) {
		job["existing_height_image"] = _height_image;
	}
	job["height_inputs_revision"] = _height_inputs_revision;
	return job;
}

bool ProcCityGenerator::_result_matches_height_inputs(const Dictionary &p_result) const {
	return (int64_t)p_result.get("height_inputs_revision", -1) == _height_inputs_revision;
}

void ProcCityGenerator::_apply_results(Dictionary p_result) {
	const int stages = p_result["stages"];
	const bool height_inputs_current = _result_matches_height_inputs(p_result);

	if (p_result.has("height_image")) {
		_height_image = p_result["height_image"];
		_last_height_path = String(p_result.get("height_path", ""));
		_cell_heights_cache.clear();
	}
	if (height_inputs_current && p_result.has("cell_heights")) {
		_cell_heights_cache = p_result["cell_heights"];
	}
	_resolved_seed = (int64_t)p_result["seed"];
	_last_generation_used = (int)p_result.get("generation_mode_used", GEN_GPU_NATIVE);

	if (stages & STAGE_HEIGHT) {
		emit_signal("generation_finished", "displacement", _last_height_path);
		emit_signal("generation_progress", "all", PROGRESS_HEIGHT_DONE);
	}

	bool geometry_ok = true;
	if (stages & STAGE_GEOMETRY) {
		StageTimer install_timer("geometry install");
		geometry_ok = _apply_result_geometry(p_result);
		if (geometry_ok) {
			install_timer.report();
			emit_signal("generation_finished", "geometry", String());
		}
	}

	if (stages & STAGE_MATERIAL) {
		_store_result_images(p_result);
	}

	if ((stages & STAGE_APPLY_MATERIAL) && geometry_ok) {
		StageTimer material_timer("material apply");
		_apply_material_main();
		material_timer.report();
		emit_signal("generation_finished", "material", _last_albedo_path);
	}

	_cleanup_temp(p_result);
	_finish_worker();

	log_pipeline_event("generation complete in " +
			String::num((double)(Time::get_singleton()->get_ticks_usec() - _pipeline_started_usec) / 1000000.0, 2) + " s");

	emit_signal("generation_progress", "all", 1.0);
	emit_signal("all_finished");
}

void ProcCityGenerator::_store_result_images(const Dictionary &p_result) {
	_material_texture_mode = (int)p_result.get("texture_mode", texture_mode);
	_material_texture_filter = (int)p_result.get("texture_filter", texture_filter);
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
	_join_worker();
	_worker.unref();
	_busy = false;
}

static const char *TEMP_PATH_KEYS[] = {
	"height_path", "albedo_path", "normal_path",
	"r_path", "g_path", "b_path", "rough_path", "config_path"
};

void ProcCityGenerator::_cleanup_temp(const Dictionary &p_result) {
	if (keep_intermediate_png) {
		return;
	}
	for (const char *key : TEMP_PATH_KEYS) {
		remove_file(String(p_result.get(key, "")));
	}
}

void ProcCityGenerator::_emit_failed(String p_stage, String p_message) {
	_finish_worker();
	UtilityFunctions::push_error(String("[ProcCity] ") + p_stage + String(": ") + p_message);
	emit_signal("generation_failed", p_stage, p_message);
}
