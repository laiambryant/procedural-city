#include "cli/release_download.h"

#include "cli/archive_extract.h"
#include "cli/http_get.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// The API endpoint redirects at most once in practice; the asset download goes
// api.github.com -> release CDN and may hop a couple more times.
static constexpr int MAX_API_REDIRECTS = 4;
static constexpr int MAX_ASSET_REDIRECTS = 6;

// File the installed tag is recorded in, beside the binary, so a later session
// can tell which release the cache holds.
static const char *VERSION_STAMP_NAME = "VERSION";

PlatformInfo godot::detect_platform() {
	PlatformInfo p;
	const String os_name = OS::get_singleton()->get_name();
	if (os_name == "Windows") {
		p.os = "windows";
		p.exe_suffix = ".exe";
		p.bundle_dir = "windows";
	} else if (os_name == "macOS") {
		p.os = "darwin";
		p.bundle_dir = "macos";
	} else {
		p.os = "linux";
		p.bundle_dir = "linux";
	}
	const String arch = Engine::get_singleton()->get_architecture_name();
	if (arch == "x86_64") {
		p.arch = "amd64";
	} else if (arch == "arm64") {
		p.arch = "arm64";
	}
	return p;
}

static String pick_release_asset(const Dictionary &p_release, const String &p_suffix, String &r_tag) {
	r_tag = p_release.get("tag_name", "");
	const Array assets = p_release.get("assets", Array());
	for (int i = 0; i < assets.size(); i++) {
		const Dictionary asset = assets[i];
		const String name = asset.get("name", "");
		if (name.ends_with(p_suffix)) {
			return asset.get("browser_download_url", "");
		}
	}
	return String();
}

// mark_executable sets the unix permission bit FileAccess cannot: a downloaded
// binary that is not executable is of no use to anyone.
static void mark_executable(const String &p_path) {
	PackedStringArray args;
	args.push_back("+x");
	args.push_back(p_path);
	OS::get_singleton()->execute("chmod", args);
}

static bool write_executable(const String &p_path, const PackedByteArray &p_bytes, const PlatformInfo &p_platform) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_buffer(p_bytes);
	f->close();
	if (p_platform.exe_suffix.is_empty()) {
		mark_executable(p_path);
	}
	return true;
}

static void stamp_version(const String &p_cache_dir, const String &p_tag) {
	Ref<FileAccess> version = FileAccess::open(p_cache_dir.path_join(VERSION_STAMP_NAME), FileAccess::WRITE);
	if (version.is_valid()) {
		version->store_string(p_tag);
		version->close();
	}
}

// http_failure prefers the transport error, falling back to the status code
// when the request completed but the server said no.
static String http_failure(const HttpResponse &p_response) {
	return p_response.error.is_empty() ? "HTTP " + String::num_int64(p_response.code) : p_response.error;
}

// fetch_latest_release resolves the newest release of p_repo to a downloadable
// asset URL for this platform, reporting r_tag and whether it is a zip.
static String fetch_latest_release_asset(const String &p_repo, const String &p_display, const PlatformInfo &p_platform,
										 String &r_tag, bool &r_zip) {
	const String api_url = String("https://api.github.com/repos/") + p_repo + "/releases/latest";
	UtilityFunctions::print("[ProcCity] Looking up the latest " + p_display + " release...");
	const HttpResponse api = http_get(api_url, "application/vnd.github+json", MAX_API_REDIRECTS);
	if (!api.ok()) {
		UtilityFunctions::push_warning("[ProcCity] Release lookup failed: " + http_failure(api));
		return String();
	}

	const Variant parsed = JSON::parse_string(api.body.get_string_from_utf8());
	if (parsed.get_type() != Variant::DICTIONARY) {
		UtilityFunctions::push_warning("[ProcCity] Release lookup returned unexpected JSON.");
		return String();
	}
	r_zip = p_platform.os == "windows";
	const String suffix = "_" + p_platform.os + "_" + p_platform.arch + (r_zip ? ".zip" : ".tar.gz");
	const String asset_url = pick_release_asset(parsed, suffix, r_tag);
	if (asset_url.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] Release " + r_tag + " has no asset matching *" + suffix + ".");
	}
	return asset_url;
}

static PackedByteArray unpack_binary(const PackedByteArray &p_archive, bool p_zip, const String &p_binary_name, const String &p_cache_dir) {
	if (p_zip) {
		return extract_zip_member(p_archive, p_binary_name, p_cache_dir);
	}
	const PackedByteArray tar = p_archive.decompress_dynamic(MAX_DOWNLOAD_BYTES, FileAccess::COMPRESSION_GZIP);
	return extract_tar_member(tar, p_binary_name);
}

String godot::download_latest_release(const String &p_cache_dir, const PlatformInfo &p_platform,
									  const String &p_binary_name, const String &p_repo, const String &p_display_name) {
	if (p_platform.arch.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] Unsupported CPU architecture for " + p_display_name + " auto-download.");
		return String();
	}

	String tag;
	bool zip = false;
	const String asset_url = fetch_latest_release_asset(p_repo, p_display_name, p_platform, tag, zip);
	if (asset_url.is_empty()) {
		return String();
	}

	UtilityFunctions::print("[ProcCity] Downloading " + p_display_name + " " + tag + " (" + asset_url.get_file() + ")...");
	const HttpResponse archive = http_get(asset_url, "application/octet-stream", MAX_ASSET_REDIRECTS);
	if (!archive.ok()) {
		UtilityFunctions::push_warning("[ProcCity] Asset download failed: " + http_failure(archive));
		return String();
	}

	if (DirAccess::make_dir_recursive_absolute(p_cache_dir) != OK) {
		UtilityFunctions::push_warning("[ProcCity] Could not create cache dir " + p_cache_dir + ".");
		return String();
	}

	const PackedByteArray binary = unpack_binary(archive.body, zip, p_binary_name, p_cache_dir);
	if (binary.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] " + asset_url.get_file() + " did not contain " + p_binary_name + ".");
		return String();
	}

	const String dest = p_cache_dir.path_join(p_binary_name);
	if (!write_executable(dest, binary, p_platform)) {
		UtilityFunctions::push_warning("[ProcCity] Could not write " + dest + ".");
		return String();
	}
	stamp_version(p_cache_dir, tag);
	UtilityFunctions::print("[ProcCity] Installed " + p_display_name + " " + tag + " at " + dest);
	return dest;
}
