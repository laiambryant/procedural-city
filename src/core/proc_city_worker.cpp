#include "core/proc_city_generator.h"

#include "cli/gdxraw_loader.h"
#include "cli/goplacementx_runner.h"
#include "cli/gpu_server.h"
#include "cli/rpc_client.h"
#include "core/proc_city_job.h"
#include "core/proc_city_log.h"
#include "native/native_renderer.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Exit code gpudisplacementx uses for "no usable GPU adapter", the one failure
// worth retrying on the CPU binary rather than reporting.
static constexpr int GPU_NO_ADAPTER_EXIT = 2;

// What one generation produced, or why it could not: the two backends fill the
// same shape so the tail of _thread_body never learns which one ran.
struct MapProduction {
	Dictionary run;
	int used_mode = ProcCityGenerator::GEN_GPU_NATIVE;
	String fail_stage;
	String fail_message;

	bool ok() const { return fail_stage.is_empty(); }

	static MapProduction failure(const String &p_stage, const String &p_message) {
		MapProduction production;
		production.fail_stage = p_stage;
		production.fail_message = p_message;
		return production;
	}
};

static bool is_native_generation_mode(int p_mode) {
	return p_mode == ProcCityGenerator::GEN_GPU_NATIVE || p_mode == ProcCityGenerator::GEN_CPU_NATIVE;
}

static bool is_legacy_gpu_mode(int p_mode) {
	return p_mode == ProcCityGenerator::GEN_GPU_LEGACY;
}

static String resolve_legacy_binary(const Ref<GoplacementxRunner> &p_runner, const Dictionary &p_job, int &r_used_mode) {
	const String override_path = p_job["binary_override"];
	const bool allow_download = p_job["auto_download"];
	if (is_legacy_gpu_mode((int)p_job.get("generation_mode", ProcCityGenerator::GEN_CPU_LEGACY))) {
		p_runner->set_cli_kind(GoplacementxRunner::CLI_GPUDISPLACEMENTX);
		const String gpu_binary = p_runner->ensure_binary(override_path, allow_download);
		if (!gpu_binary.is_empty()) {
			r_used_mode = ProcCityGenerator::GEN_GPU_LEGACY;
			return gpu_binary;
		}
		UtilityFunctions::push_warning("[ProcCity] gpudisplacementx CLI not found and the GitHub download did not succeed - falling back to the legacy CPU pipeline (binary_path_override is ignored for the fallback).");
		p_runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
		r_used_mode = ProcCityGenerator::GEN_CPU_LEGACY;
		return p_runner->ensure_binary(String(), allow_download);
	}
	p_runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
	r_used_mode = ProcCityGenerator::GEN_CPU_LEGACY;
	return p_runner->ensure_binary(override_path, allow_download);
}

static const char *cli_display_name(int p_generation_mode) {
	return is_legacy_gpu_mode(p_generation_mode) ? "gpudisplacementx" : "godisplacementx";
}

// run_generation_bundle prefers the persistent GPU server when the node opted
// in and the generation resolved to GPU; any server failure (process not up,
// pipe closed, per-request error) transparently falls back to the one-shot CLI,
// which itself still handles the exit-2 CPU fallback downstream.
static Dictionary run_generation_bundle(const Ref<GoplacementxRunner> &p_runner, const String &p_binary,
		const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params,
		int p_used_mode, bool p_use_server, int p_material_max_size, bool p_mipmaps) {
	if (is_legacy_gpu_mode(p_used_mode) && p_use_server) {
		ProcCityGpuServer *server = ProcCityGpuServer::get_singleton();
		if (server && server->ensure_started(p_binary)) {
			const Dictionary served = server->run_bundle(p_config, p_emits, p_params, p_material_max_size, p_mipmaps);
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

// plan_emits_for_cli names the output maps in an interchange format the
// currently resolved CLI can actually write. It has to be re-run after a
// GPU->CPU fallback: reusing the GPU's .gdxraw paths would leave the CPU binary
// writing PNG bytes into files that later fail the GDXR header check.
static Array plan_emits_for_cli(const Ref<GoplacementxRunner> &p_runner, const Dictionary &p_job, const String &p_dir,
		bool p_want_height, bool p_want_material, Dictionary &r_result) {
	const String ext = map_extension_for((bool)p_job.get("keep_intermediate_png", false), p_runner->supports_gdxraw());
	return plan_bundle_emits(p_dir, (uint64_t)(int64_t)p_job["seed"], p_want_height, p_want_material,
			p_job["texture_mode"], ext, r_result);
}

static CliKind to_binary_provider_kind(int p_generation_mode) {
	return is_legacy_gpu_mode(p_generation_mode) ? CliKind::GPUDISPLACEMENTX : CliKind::GODISPLACEMENTX;
}

// try_grpc_bundle is the first thing every legacy generation attempts: params
// travel in the request message and maps come back over one persistent channel,
// so nothing is ever written to or read from disk and the CLI's startup cost
// (process spawn, or the GPU's ~600ms device bring-up) is paid once per
// session instead of once per generation. Returns an empty Dictionary - never
// one with a "code" key - when gRPC isn't available for this binary, so the
// caller knows to fall back to writing the config file.
static Dictionary try_grpc_bundle(const String &p_binary, int p_used_mode, const Array &p_emits,
		const Ref<GoplacementxParams> &p_params, int p_material_max_size, bool p_mipmaps) {
	ProcCityRpcClient *rpc = proc_city_rpc_client_for(to_binary_provider_kind(p_used_mode));
	if (!rpc->ensure_started(p_binary)) {
		return Dictionary();
	}
	const Dictionary served = rpc->run_bundle(p_emits, p_params, p_material_max_size, p_mipmaps);
	if ((int)served.get("code", 1) == 0) {
		return served;
	}
	UtilityFunctions::push_warning("[ProcCity] " + String(cli_display_name(p_used_mode)) + " gRPC request failed (" + String(served.get("output", "")) + ") - retrying without gRPC.");
	return Dictionary();
}

// cli_failure_message prefers whatever the CLI printed, falling back to the
// exit code when it died without saying anything.
static String cli_failure_message(const Dictionary &p_run, int p_used_mode) {
	String message = String(p_run.get("output", ""));
	if (message.strip_edges().is_empty()) {
		message = String(cli_display_name(p_used_mode)) + " exited with code " + String::num_int64((int)p_run.get("code", -1));
	}
	return message;
}

// The in-process backend needs no binary, no config file and no temporary
// files: it plans the same emit paths purely as keys and hands the maps back as
// Images, so the loader downstream never touches the disk.
static MapProduction produce_native_maps(const Dictionary &p_job, Dictionary &r_result) {
	const int stages = p_job["stages"];
	const Array emits = plan_bundle_emits(p_job["dir"], (uint64_t)(int64_t)p_job["seed"],
			stages & ProcCityGenerator::STAGE_HEIGHT, stages & ProcCityGenerator::STAGE_MATERIAL,
			p_job["texture_mode"], map_extension_for(false, true), r_result);

	bool used_gpu = false;
	MapProduction production;
	production.run = run_native_bundle(emits, p_job["params"],
			(int)p_job.get("generation_mode", ProcCityGenerator::GEN_GPU_NATIVE) == ProcCityGenerator::GEN_GPU_NATIVE,
			used_gpu);
	production.used_mode = used_gpu ? ProcCityGenerator::GEN_GPU_NATIVE : ProcCityGenerator::GEN_CPU_NATIVE;
	if ((int)production.run.get("code", 1) != 0) {
		return MapProduction::failure((stages & ProcCityGenerator::STAGE_MATERIAL) ? "material" : "displacement",
				String(production.run.get("output", "The native backend produced no maps.")));
	}
	return production;
}

static MapProduction produce_legacy_maps(const Dictionary &p_job, Dictionary &r_result, const Ref<GoplacementxRunner> &p_runner) {
	const int stages = p_job["stages"];
	const String dir = p_job["dir"];
	Ref<GoplacementxParams> params = p_job["params"];
	const int material_size = (int)p_job.get("material_max_size", 0);
	const bool material_mipmaps =
			(int)p_job.get("texture_filter", ProcCityGenerator::TEXTURE_FILTER_LINEAR) == ProcCityGenerator::TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC;

	MapProduction production;
	String binary = resolve_legacy_binary(p_runner, p_job, production.used_mode);
	if (binary.is_empty()) {
		return MapProduction::failure("setup", "godisplacementx CLI not found and the GitHub download did not succeed (offline? no release yet?). Set Binary Path Override or drop the binary under addons/procedural_city/godisplacementx/<platform>/.");
	}

	Array emits = plan_emits_for_cli(p_runner, p_job, dir, stages & ProcCityGenerator::STAGE_HEIGHT,
			stages & ProcCityGenerator::STAGE_MATERIAL, r_result);

	String config;
	production.run = try_grpc_bundle(binary, production.used_mode, emits, params, material_size, material_mipmaps);
	if (production.run.is_empty()) {
		config = p_runner->write_config(dir, params);
		if (config.is_empty()) {
			return MapProduction::failure("setup", "Could not write config JSON into " + dir);
		}
		r_result["config_path"] = config;
		const bool use_server = (bool)p_job.get("use_gpu_server", false) && !(bool)p_job.get("keep_intermediate_png", false);
		production.run = run_generation_bundle(p_runner, binary, config, emits, params, production.used_mode, use_server,
				material_size, material_mipmaps);
	}

	if (is_legacy_gpu_mode(production.used_mode) && (int)production.run["code"] == GPU_NO_ADAPTER_EXIT) {
		UtilityFunctions::push_warning("[ProcCity] gpudisplacementx reported no usable GPU adapter (exit 2) - falling back to the legacy CPU pipeline.");
		p_runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
		binary = p_runner->ensure_binary(String(), p_job["auto_download"]);
		if (binary.is_empty()) {
			return MapProduction::failure("setup", "godisplacementx CLI not found for the CPU fallback (offline? no release yet?). Drop the binary under addons/procedural_city/godisplacementx/<platform>/.");
		}
		production.used_mode = ProcCityGenerator::GEN_CPU_LEGACY;
		emits = plan_emits_for_cli(p_runner, p_job, dir, stages & ProcCityGenerator::STAGE_HEIGHT,
				stages & ProcCityGenerator::STAGE_MATERIAL, r_result);
		production.run = try_grpc_bundle(binary, production.used_mode, emits, params, material_size, material_mipmaps);
		if (production.run.is_empty()) {
			if (config.is_empty()) {
				config = p_runner->write_config(dir, params);
				if (config.is_empty()) {
					return MapProduction::failure("setup", "Could not write config JSON into " + dir);
				}
				r_result["config_path"] = config;
			}
			production.run = p_runner->run_bundle(binary, config, emits, params);
		}
	}

	if ((int)production.run["code"] != 0) {
		return MapProduction::failure((stages & ProcCityGenerator::STAGE_MATERIAL) ? "material" : "displacement",
				cli_failure_message(production.run, production.used_mode));
	}
	return production;
}

// The native backends keep every map in memory, so their planned paths were
// only dictionary keys. Dropping them once the images are loaded stops
// generation_finished from advertising a file nobody wrote, and stops the
// cleanup pass from chasing it.
static void forget_planned_paths(Dictionary &r_result) {
	for (const char *key : { "height_path", "albedo_path", "normal_path", "r_path", "g_path", "b_path", "rough_path" }) {
		r_result.erase(key);
	}
}

static String describe_requested_stages(int p_stages) {
	PackedStringArray names;
	if (p_stages & ProcCityGenerator::STAGE_HEIGHT) {
		names.push_back("height");
	}
	if (p_stages & ProcCityGenerator::STAGE_MATERIAL) {
		names.push_back("material");
	}
	if (p_stages & ProcCityGenerator::STAGE_GEOMETRY) {
		names.push_back("geometry");
	}
	return names.is_empty() ? String("no stage") : String("+").join(names);
}

static String describe_material_images(const Dictionary &p_result) {
	PackedStringArray parts;
	for (const char *key : { "albedo_image", "normal_image", "rough_image" }) {
		if (!p_result.has(key)) {
			continue;
		}
		Ref<Image> image = p_result[key];
		if (image.is_valid() && !image->is_empty()) {
			parts.push_back(String(key).replace("_image", "") + " " + format_pixel_size(image));
		}
	}
	return parts.is_empty() ? String("no map") : String(", ").join(parts);
}

static String describe_worker_meshes(const Dictionary &p_result) {
	if (!p_result.has("geometry_meshes")) {
		return "built on the main thread";
	}
	const TypedArray<ArrayMesh> meshes = p_result["geometry_meshes"];
	int64_t surfaces = 0;
	for (int i = 0; i < meshes.size(); i++) {
		Ref<ArrayMesh> mesh = meshes[i];
		if (mesh.is_valid()) {
			surfaces += mesh->get_surface_count();
		}
	}
	return String::num_int64(meshes.size()) + " mesh(es), " + String::num_int64(surfaces) + " surface(s)";
}

static Dictionary make_result_shell(const Dictionary &p_job) {
	Dictionary result;
	result["stages"] = p_job["stages"];
	result["seed"] = p_job["seed"];
	result["texture_mode"] = p_job["texture_mode"];
	result["texture_filter"] = p_job["texture_filter"];
	result["dir"] = p_job["dir"];
	result["height_inputs_revision"] = p_job.get("height_inputs_revision", -1);
	return result;
}

void ProcCityGenerator::_thread_body(Dictionary p_job) {
	const int stages = p_job["stages"];
	const int mode = (int)p_job.get("generation_mode", GEN_GPU_NATIVE);
	const int material_size = (int)p_job.get("material_max_size", 0);
	const bool material_mipmaps =
			(int)p_job.get("texture_filter", TEXTURE_FILTER_LINEAR) == TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC;

	Dictionary result = make_result_shell(p_job);

	Ref<GoplacementxRunner> runner;
	runner.instantiate();
	StageTimer maps_timer("maps");
	const MapProduction produced = is_native_generation_mode(mode)
			? produce_native_maps(p_job, result)
			: produce_legacy_maps(p_job, result, runner);
	if (!produced.ok()) {
		call_deferred("_emit_failed", produced.fail_stage, produced.fail_message);
		return;
	}
	maps_timer.report(generation_mode_name(produced.used_mode) + ", " + describe_requested_stages(stages));
	result["generation_mode_used"] = produced.used_mode;

	if ((int)p_job.get("texture_mode", TEX_SINGLE) == TEX_SHARED && p_job.has("existing_height_image")) {
		result["shared_height_image"] = p_job["existing_height_image"];
	}

	String fail_stage;
	String fail_message;
	StageTimer images_timer("material images");
	if (!load_result_images(result, produced.run.get("images", Dictionary()), material_size, material_mipmaps,
				fail_stage, fail_message)) {
		call_deferred("_emit_failed", fail_stage, fail_message);
		return;
	}
	prepare_material_images(result, material_size, material_mipmaps);
	if (stages & STAGE_MATERIAL) {
		images_timer.report(describe_material_images(result));
	}
	if (is_native_generation_mode(mode)) {
		forget_planned_paths(result);
	}

	if (stages & STAGE_GEOMETRY) {
		StageTimer mesh_timer("geometry meshing");
		if (!build_worker_mesh(p_job, result)) {
			call_deferred("_emit_failed", "geometry", "Worker mesh build failed.");
			return;
		}
		mesh_timer.report(describe_worker_meshes(result));
	}

	call_deferred("_apply_results", result);
}
