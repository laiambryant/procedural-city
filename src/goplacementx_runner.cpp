#include "goplacementx_runner.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void GoplacementxRunner::_bind_methods() {
	ClassDB::bind_method(D_METHOD("find_binary", "override_path"), &GoplacementxRunner::find_binary);
	ClassDB::bind_method(D_METHOD("write_config", "dir", "params"), &GoplacementxRunner::write_config);
	ClassDB::bind_method(D_METHOD("resolve_seed", "params"), &GoplacementxRunner::resolve_seed);
	ClassDB::bind_method(D_METHOD("run_generate", "binary", "config", "mode", "seed", "out_png", "params"),
			&GoplacementxRunner::run_generate);
	ClassDB::bind_method(D_METHOD("run_bundle", "binary", "config", "emits", "params"),
			&GoplacementxRunner::run_bundle);
}

// append_size_flags appends either explicit --width/--height (when both are set)
// or --resolution, matching the CLI's sizing precedence.
static void append_size_flags(PackedStringArray &p_args, const Ref<GoplacementxParams> &p_params) {
	const int width = p_params.is_valid() ? p_params->get_out_width() : 0;
	const int height = p_params.is_valid() ? p_params->get_out_height() : 0;
	if (width > 0 && height > 0) {
		p_args.push_back("--width");
		p_args.push_back(String::num_int64(width));
		p_args.push_back("--height");
		p_args.push_back(String::num_int64(height));
	} else {
		p_args.push_back("--resolution");
		p_args.push_back(String::num_int64(p_params.is_valid() ? p_params->get_resolution() : 2048));
	}
}

String GoplacementxRunner::find_binary(const String &p_override) const {
	ProjectSettings *ps = ProjectSettings::get_singleton();

	if (!p_override.is_empty()) {
		const String g = ps->globalize_path(p_override);
		if (FileAccess::file_exists(g)) {
			return g;
		}
	}

	const String os_name = OS::get_singleton()->get_name();
	String sub;
	String exe;
	if (os_name == "Windows") {
		sub = "windows";
		exe = "goplacementx-cli.exe";
	} else if (os_name == "macOS") {
		sub = "macos";
		exe = "goplacementx-cli";
	} else {
		sub = "linux";
		exe = "goplacementx-cli";
	}

	const String base = String("res://addons/procedural_city/goplacementx/") + sub + String("/") + exe;
	const String g = ps->globalize_path(base);
	if (FileAccess::file_exists(g)) {
		return g;
	}
	return String();
}

String GoplacementxRunner::write_config(const String &p_dir, const Ref<GoplacementxParams> &p_params) const {
	if (p_params.is_null()) {
		return String();
	}
	const uint64_t stamp = Time::get_singleton()->get_ticks_usec();
	const String path = p_dir.path_join(String("proc_city_") + String::num_uint64(stamp) + String("_config.json"));
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return String();
	}
	f->store_string(p_params->to_json());
	f->close();
	return path;
}

int64_t GoplacementxRunner::resolve_seed(const Ref<GoplacementxParams> &p_params) const {
	if (p_params.is_valid() && !p_params->get_randomize_seed()) {
		return p_params->get_seed();
	}
	const uint64_t t = Time::get_singleton()->get_ticks_usec();
	const uint64_t r = (uint64_t)(uint32_t)UtilityFunctions::randi();
	return (int64_t)((r << 21) ^ t ^ (t << 32));
}

Dictionary GoplacementxRunner::run_generate(const String &p_binary, const String &p_config, const String &p_mode,
		int64_t p_seed, const String &p_out_png, const Ref<GoplacementxParams> &p_params) const {
	PackedStringArray args;
	args.push_back("generate");
	args.push_back("--config");
	args.push_back(p_config);
	args.push_back("--mode");
	args.push_back(p_mode);
	args.push_back("--seed");
	args.push_back(String::num_uint64((uint64_t)p_seed));
	args.push_back("-o");
	args.push_back(p_out_png);

	append_size_flags(args, p_params);

	if (p_params.is_valid() && p_params->get_invert()) {
		args.push_back("--invert");
	}
	if (p_mode == "color" && p_params.is_valid()) {
		const String grad = p_params->gradient_to_string();
		if (!grad.is_empty()) {
			args.push_back("--gradient");
			args.push_back(grad);
		}
	}

	Array output;
	const int64_t code = OS::get_singleton()->execute(p_binary, args, output, true, false);

	String text;
	if (!output.is_empty()) {
		text = String(output[0]);
	}

	Dictionary result;
	result["code"] = code;
	result["output"] = text;
	result["path"] = p_out_png;
	return result;
}

Dictionary GoplacementxRunner::run_bundle(const String &p_binary, const String &p_config,
		const Array &p_emits, const Ref<GoplacementxParams> &p_params) const {
	PackedStringArray args;
	args.push_back("bundle");
	args.push_back("--config");
	args.push_back(p_config);

	append_size_flags(args, p_params);

	if (p_params.is_valid() && p_params->get_invert()) {
		args.push_back("--invert");
	}

	// The gradient only affects color emits, but passing it unconditionally is
	// harmless (grayscale/normal emits ignore it).
	if (p_params.is_valid()) {
		const String grad = p_params->gradient_to_string();
		if (!grad.is_empty()) {
			args.push_back("--gradient");
			args.push_back(grad);
		}
	}

	for (int i = 0; i < p_emits.size(); i++) {
		const Dictionary e = p_emits[i];
		const String mode = e.get("mode", "grayscale");
		const uint64_t seed = (uint64_t)(int64_t)e.get("seed", 0);
		const String path = e.get("path", "");
		if (path.is_empty()) {
			continue;
		}
		args.push_back("--emit");
		args.push_back(mode + String(":") + String::num_uint64(seed) + String(":") + path);
	}

	Array output;
	const int64_t code = OS::get_singleton()->execute(p_binary, args, output, true, false);

	String text;
	if (!output.is_empty()) {
		text = String(output[0]);
	}

	Dictionary result;
	result["code"] = code;
	result["output"] = text;
	return result;
}
