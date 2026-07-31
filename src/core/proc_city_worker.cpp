#include "core/proc_city_generator.h"

#include "cli/gdxraw_loader.h"
#include "cli/goplacementx_runner.h"
#include "cli/gpu_server.h"
#include "core/proc_city_job.h"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Exit code gpudisplacementx uses for "no usable GPU adapter", the one failure
// worth retrying on the CPU binary rather than reporting.
static constexpr int GPU_NO_ADAPTER_EXIT = 2;

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

// cli_failure_message prefers whatever the CLI printed, falling back to the
// exit code when it died without saying anything.
static String cli_failure_message(const Dictionary &p_run, int p_used_mode) {
	String message = String(p_run.get("output", ""));
	if (message.strip_edges().is_empty()) {
		message = String(cli_display_name(p_used_mode)) + " exited with code " + String::num_int64((int)p_run.get("code", -1));
	}
	return message;
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

	Array emits = plan_emits_for_cli(runner, p_job, dir, stages & STAGE_HEIGHT, stages & STAGE_MATERIAL, result);

	const bool use_server = (bool)p_job.get("use_gpu_server", false) && !(bool)p_job.get("keep_intermediate_png", false);
	Dictionary run = run_generation_bundle(runner, binary, config, emits, p, used_mode, use_server);
	if (used_mode == GEN_GPU && (int)run["code"] == GPU_NO_ADAPTER_EXIT) {
		UtilityFunctions::push_warning("[ProcCity] gpudisplacementx reported no usable GPU adapter (exit 2) - falling back to the CPU pipeline.");
		runner->set_cli_kind(GoplacementxRunner::CLI_GODISPLACEMENTX);
		binary = runner->ensure_binary(String(), p_job["auto_download"]);
		if (binary.is_empty()) {
			call_deferred("_emit_failed", "setup", "godisplacementx CLI not found for the CPU fallback (offline? no release yet?). Drop the binary under addons/procedural_city/godisplacementx/<platform>/.");
			return;
		}
		used_mode = GEN_CPU;
		emits = plan_emits_for_cli(runner, p_job, dir, stages & STAGE_HEIGHT, stages & STAGE_MATERIAL, result);
		run = runner->run_bundle(binary, config, emits, p);
	}
	if ((int)run["code"] != 0) {
		call_deferred("_emit_failed", (stages & STAGE_MATERIAL) ? "material" : "displacement", cli_failure_message(run, used_mode));
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
