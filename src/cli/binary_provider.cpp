#include "cli/binary_provider.h"

#include "cli/release_download.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

// CliSpec is everything that differs between the two CLIs the extension can
// drive: where to download it from, what the executable is called (including
// the pre-rename stem still found in older checkouts), which addon directories
// a bundled copy may sit in, and where downloads are cached.
struct CliSpec {
	const char *repo;
	const char *display_name;
	const char *cli_stem;
	const char *legacy_stem;
	const char *bundled_roots[2];
	const char *cache_dir;
};

static const CliSpec &spec_for(CliKind p_kind) {
	static const CliSpec godisplacementx_spec = {
		"laiambryant/godisplacementx",
		"godisplacementx",
		"godisplacementx-cli",
		"goplacementx-cli",
		{ "godisplacementx", "goplacementx" },
		"user://godisplacementx/bin",
	};
	static const CliSpec gpudisplacementx_spec = {
		"laiambryant/gpudisplacementx",
		"gpudisplacementx",
		"gpudisplacementx-cli",
		nullptr,
		{ "gpudisplacementx", nullptr },
		"user://gpudisplacementx/bin",
	};
	return p_kind == CliKind::GPUDISPLACEMENTX ? gpudisplacementx_spec : godisplacementx_spec;
}

// existing_global_path resolves a res:// or user:// path and returns it only if
// something is actually there, so each resolution step is a single check.
static String existing_global_path(const String &p_path) {
	const String global = ProjectSettings::get_singleton()->globalize_path(p_path);
	return FileAccess::file_exists(global) ? global : String();
}

// bundled_binary_names lists the executable names a shipped copy might use:
// the current stem, plus the pre-rename one where the CLI still has a legacy
// name to honour.
static PackedStringArray bundled_binary_names(const CliSpec &p_spec, const PlatformInfo &p_platform) {
	PackedStringArray names;
	names.push_back(String(p_spec.cli_stem) + p_platform.exe_suffix);
	if (p_spec.legacy_stem) {
		names.push_back(String(p_spec.legacy_stem) + p_platform.exe_suffix);
	}
	return names;
}

// find_bundled_binary searches every addon directory the CLI may ship under,
// for every name it may ship as.
static String find_bundled_binary(const CliSpec &p_spec, const PlatformInfo &p_platform) {
	const PackedStringArray names = bundled_binary_names(p_spec, p_platform);
	for (const char *root : p_spec.bundled_roots) {
		if (!root) {
			continue;
		}
		for (int i = 0; i < names.size(); i++) {
			const String base = String("res://addons/procedural_city/") + root + "/" + p_platform.bundle_dir + "/" + names[i];
			const String found = existing_global_path(base);
			if (!found.is_empty()) {
				return found;
			}
		}
	}
	return String();
}

String godot::resolve_cli_binary(CliKind p_kind, const String &p_override, bool p_allow_download) {
	const CliSpec &spec = spec_for(p_kind);
	const PlatformInfo platform = detect_platform();
	const String binary_name = String(spec.cli_stem) + platform.exe_suffix;

	if (!p_override.is_empty()) {
		const String overridden = existing_global_path(p_override);
		if (!overridden.is_empty()) {
			return overridden;
		}
	}

	const String bundled = find_bundled_binary(spec, platform);
	if (!bundled.is_empty()) {
		return bundled;
	}

	const String cache_dir = ProjectSettings::get_singleton()->globalize_path(spec.cache_dir);
	const String cached = cache_dir.path_join(binary_name);
	if (FileAccess::file_exists(cached)) {
		return cached;
	}

	if (p_allow_download) {
		return download_latest_release(cache_dir, platform, binary_name, spec.repo, spec.display_name);
	}
	return String();
}

String godot::resolve_cli_binary(const String &p_override, bool p_allow_download) {
	return resolve_cli_binary(CliKind::GODISPLACEMENTX, p_override, p_allow_download);
}
