#include "native/native_renderer.h"

#include "native/native_gpu.h"
#include "native/native_params.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <cppdisplacementx/engine.h>

#include <algorithm>
#include <vector>

using namespace godot;

namespace {

struct EmitSpec {
	int64_t seed = 0;
	String path;
	cppdx::OutputMode mode = cppdx::OutputMode::GRAYSCALE;
};

struct BundleInputs {
	cppdx::Params params;
	cppdx::SpriteAtlas atlas;
	std::vector<cppdx::ColorRgb> gradient;
	Vector2i size;
	bool invert = false;
	std::vector<EmitSpec> emits;
};

} // namespace

static std::vector<EmitSpec> parse_emits(const Array &p_emits) {
	std::vector<EmitSpec> specs;
	specs.reserve((size_t)p_emits.size());
	for (int i = 0; i < p_emits.size(); i++) {
		const Dictionary entry = p_emits[i];
		const String path = entry.get("path", "");
		if (path.is_empty()) {
			continue;
		}
		EmitSpec spec;
		spec.seed = (int64_t)entry.get("seed", 0);
		spec.path = path;
		spec.mode = to_native_output_mode(entry.get("mode", "grayscale"));
		specs.push_back(spec);
	}
	return specs;
}

static std::vector<int64_t> distinct_seeds_in_order(const std::vector<EmitSpec> &p_emits) {
	std::vector<int64_t> seeds;
	for (const EmitSpec &emit : p_emits) {
		if (std::find(seeds.begin(), seeds.end(), emit.seed) == seeds.end()) {
			seeds.push_back(emit.seed);
		}
	}
	return seeds;
}

static BundleInputs collect_inputs(const Array &p_emits, const Ref<GoplacementxParams> &p_params) {
	BundleInputs inputs;
	inputs.params = to_native_params(p_params);
	inputs.atlas = build_native_atlas(inputs.params);
	inputs.gradient = to_native_gradient(p_params);
	inputs.size = native_canvas_size(p_params);
	inputs.invert = p_params.is_valid() && p_params->get_invert();
	inputs.emits = parse_emits(p_emits);
	return inputs;
}

// Renders one seed's height field. A GPU session that failed to come up is
// passed in already closed, so the CPU path takes over without another attempt.
static cppdx::Canvas render_field(const BundleInputs &p_inputs, int64_t p_seed, NativeGpuSession *p_gpu, bool &r_used_gpu) {
	const uint32_t width = (uint32_t)p_inputs.size.x;
	const uint32_t height = (uint32_t)p_inputs.size.y;
	const cppdx::CommandList commands = cppdx::build_command_list(p_inputs.params, width, height, (uint64_t)p_seed);

	cppdx::Canvas canvas(width, height);
	if (p_gpu != nullptr) {
		String error;
		if (p_gpu->composite(commands, p_inputs.atlas, width, height, canvas, error)) {
			r_used_gpu = true;
			return canvas;
		}
		UtilityFunctions::push_warning("[ProcCity] Native GPU compositor failed (" + error + ") - generating on the CPU backend.");
		canvas = cppdx::Canvas(width, height);
	}
	r_used_gpu = false;
	cppdx::composite_cpu(canvas, commands, p_inputs.atlas.view(), 0);
	return canvas;
}

static Dictionary failed_bundle(const String &p_message) {
	Dictionary result;
	result["code"] = 1;
	result["output"] = p_message;
	result["images"] = Dictionary();
	return result;
}

Dictionary godot::run_native_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
		bool p_prefer_gpu, bool &r_used_gpu) {
	const BundleInputs inputs = collect_inputs(p_emits, p_params);
	if (inputs.size.x <= 0 || inputs.size.y <= 0) {
		return failed_bundle("Invalid canvas size " + String(inputs.size));
	}
	if (inputs.emits.empty()) {
		return failed_bundle("No output maps were requested.");
	}

	NativeGpuSession gpu;
	String gpu_error;
	const bool gpu_ready = p_prefer_gpu && gpu.begin(gpu_error);
	if (p_prefer_gpu && !gpu_ready) {
		UtilityFunctions::push_warning("[ProcCity] Native GPU backend unavailable (" + gpu_error + ") - generating on the CPU backend.");
	}

	r_used_gpu = false;
	Dictionary images;
	for (int64_t seed : distinct_seeds_in_order(inputs.emits)) {
		bool seed_used_gpu = false;
		const cppdx::Canvas field = render_field(inputs, seed, gpu_ready ? &gpu : nullptr, seed_used_gpu);
		r_used_gpu = seed_used_gpu;
		for (const EmitSpec &emit : inputs.emits) {
			if (emit.seed != seed) {
				continue;
			}
			const cppdx::Canvas map = cppdx::derive_map(field, emit.mode, inputs.invert, inputs.gradient);
			images[emit.path] = canvas_to_image(map);
		}
	}

	Dictionary result;
	result["code"] = 0;
	result["output"] = String();
	result["images"] = images;
	return result;
}
