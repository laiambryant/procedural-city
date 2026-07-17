#include "cli/binary_provider.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace {

struct CliSpec {
	const char *repo;
	const char *display_name;
	const char *cli_stem;
	const char *legacy_stem;
	const char *bundled_roots[2];
	const char *cache_dir;
};

const CliSpec &spec_for(CliKind p_kind) {
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

constexpr int64_t MAX_DOWNLOAD_BYTES = 256ll * 1024 * 1024;
constexpr uint64_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint64_t TRANSFER_TIMEOUT_MS = 180000;
constexpr int CONNECT_POLL_INTERVAL_MS = 10;
constexpr int BODY_POLL_INTERVAL_MS = 5;
// The API endpoint redirects at most once in practice; the asset download goes
// api.github.com -> release CDN and may hop a couple more times.
constexpr int MAX_API_REDIRECTS = 4;
constexpr int MAX_ASSET_REDIRECTS = 6;

struct PlatformInfo {
	String os;		   // release asset OS token: windows / linux / darwin
	String arch;	   // release asset arch token: amd64 / arm64
	String exe_suffix; // ".exe" on Windows
	String bundle_dir; // addon subdirectory the binary ships in
};

PlatformInfo detect_platform() {
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

// --- Blocking HTTPS GET with manual redirect handling -----------------------

struct HttpResponse {
	int code = 0;
	PackedByteArray body;
	String error;

	bool ok() const { return error.is_empty() && code == 200; }
};

bool split_url(const String &p_url, String &r_host, String &r_path) {
	if (!p_url.begins_with("https://")) {
		return false; // release/API traffic is https-only
	}
	const String rest = p_url.trim_prefix("https://");
	const int slash = rest.find("/");
	r_host = slash < 0 ? rest : rest.substr(0, slash);
	r_path = slash < 0 ? String("/") : rest.substr(slash);
	return !r_host.is_empty();
}

bool poll_until(const Ref<HTTPClient> &p_client, HTTPClient::Status p_leave, uint64_t p_timeout_ms) {
	const uint64_t start = Time::get_singleton()->get_ticks_msec();
	while (p_client->get_status() == p_leave) {
		if (Time::get_singleton()->get_ticks_msec() - start > p_timeout_ms) {
			return false;
		}
		p_client->poll();
		OS::get_singleton()->delay_msec(CONNECT_POLL_INTERVAL_MS);
	}
	return true;
}

String header_value(const Dictionary &p_headers, const String &p_key) {
	const Array keys = p_headers.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String k = keys[i];
		if (k.nocasecmp_to(p_key) == 0) {
			return p_headers[keys[i]];
		}
	}
	return String();
}

HttpResponse http_get(const String &p_url, const String &p_accept, int p_redirects_left) {
	HttpResponse res;
	String host;
	String path;
	if (!split_url(p_url, host, path)) {
		res.error = "Unsupported URL: " + p_url;
		return res;
	}

	Ref<HTTPClient> client;
	client.instantiate();
	if (client->connect_to_host("https://" + host, -1, TLSOptions::client()) != OK) {
		res.error = "Could not start connection to " + host;
		return res;
	}
	if (!poll_until(client, HTTPClient::STATUS_RESOLVING, CONNECT_TIMEOUT_MS) ||
		!poll_until(client, HTTPClient::STATUS_CONNECTING, CONNECT_TIMEOUT_MS) ||
		client->get_status() != HTTPClient::STATUS_CONNECTED) {
		res.error = "Connection to " + host + " failed (status " + String::num_int64(client->get_status()) + ").";
		return res;
	}

	PackedStringArray headers;
	headers.push_back("User-Agent: procedural-city-gdextension");
	headers.push_back("Accept: " + p_accept);
	if (client->request(HTTPClient::METHOD_GET, path, headers) != OK) {
		res.error = "Request to " + host + path + " failed to start.";
		return res;
	}
	if (!poll_until(client, HTTPClient::STATUS_REQUESTING, TRANSFER_TIMEOUT_MS) || !client->has_response()) {
		res.error = "No response from " + host + ".";
		return res;
	}

	res.code = client->get_response_code();
	if (res.code >= 300 && res.code < 400) {
		const String location = header_value(client->get_response_headers_as_dictionary(), "Location");
		client->close();
		if (location.is_empty() || p_redirects_left <= 0) {
			res.error = "Redirect from " + host + " could not be followed.";
			return res;
		}
		return http_get(location, p_accept, p_redirects_left - 1);
	}

	const uint64_t start = Time::get_singleton()->get_ticks_msec();
	while (client->get_status() == HTTPClient::STATUS_BODY) {
		if (Time::get_singleton()->get_ticks_msec() - start > TRANSFER_TIMEOUT_MS) {
			res.error = "Download from " + host + " timed out.";
			return res;
		}
		client->poll();
		const PackedByteArray chunk = client->read_response_body_chunk();
		if (chunk.is_empty()) {
			OS::get_singleton()->delay_msec(BODY_POLL_INTERVAL_MS);
			continue;
		}
		res.body.append_array(chunk);
		if ((int64_t)res.body.size() > MAX_DOWNLOAD_BYTES) {
			res.error = "Download from " + host + " exceeded the size limit.";
			return res;
		}
	}
	client->close();

	if (res.code != 200) {
		res.error = host + path + " answered HTTP " + String::num_int64(res.code) + ".";
	}
	return res;
}

// --- Archive extraction ------------------------------------------------------

// extract_zip_member writes p_member (matched by basename) out of a zip archive
// that only exists in memory; ZIPReader wants a file, so stage one next to the
// destination.
PackedByteArray extract_zip_member(const PackedByteArray &p_zip, const String &p_member, const String &p_stage_dir) {
	const String stage = p_stage_dir.path_join("cli_download.zip");
	Ref<FileAccess> f = FileAccess::open(stage, FileAccess::WRITE);
	if (f.is_null()) {
		return PackedByteArray();
	}
	f->store_buffer(p_zip);
	f->close();

	PackedByteArray out;
	Ref<ZIPReader> zip;
	zip.instantiate();
	if (zip->open(stage) == OK) {
		const PackedStringArray files = zip->get_files();
		for (int i = 0; i < files.size(); i++) {
			if (files[i] == p_member || files[i].ends_with("/" + p_member)) {
				out = zip->read_file(files[i]);
				break;
			}
		}
		zip->close();
	}
	Ref<DirAccess> da = DirAccess::open(p_stage_dir);
	if (da.is_valid()) {
		da->remove(stage.get_file());
	}
	return out;
}

// POSIX ustar layout: 512-byte blocks; the header stores the file name in the
// first 100 bytes, the octal size at offset 124 (12 bytes) and the entry type
// flag at offset 156.
constexpr int64_t TAR_BLOCK_SIZE = 512;
constexpr int TAR_NAME_FIELD_LEN = 100;
constexpr int TAR_SIZE_FIELD_OFFSET = 124;
constexpr int TAR_SIZE_FIELD_LEN = 12;
constexpr int TAR_TYPE_FIELD_OFFSET = 156;

int64_t parse_tar_octal(const uint8_t *p_field, int p_len) {
	int64_t value = 0;
	for (int i = 0; i < p_len; i++) {
		const uint8_t c = p_field[i];
		if (c < '0' || c > '7') {
			break;
		}
		value = value * 8 + (c - '0');
	}
	return value;
}

// extract_tar_member walks the 512-byte block structure of a (already
// gunzipped) tar stream and returns the first regular file whose basename
// matches p_member.
PackedByteArray extract_tar_member(const PackedByteArray &p_tar, const String &p_member) {
	const uint8_t *data = p_tar.ptr();
	const int64_t total = p_tar.size();
	int64_t off = 0;
	while (off + TAR_BLOCK_SIZE <= total) {
		const uint8_t *hdr = data + off;
		if (hdr[0] == 0) {
			break; // end-of-archive zero block
		}
		char name_buf[TAR_NAME_FIELD_LEN + 1];
		memcpy(name_buf, hdr, TAR_NAME_FIELD_LEN);
		name_buf[TAR_NAME_FIELD_LEN] = 0;
		const String name = String::utf8(name_buf);
		const int64_t size = parse_tar_octal(hdr + TAR_SIZE_FIELD_OFFSET, TAR_SIZE_FIELD_LEN);
		const uint8_t type = hdr[TAR_TYPE_FIELD_OFFSET];
		const bool regular = type == '0' || type == 0;
		if (regular && (name == p_member || name.ends_with("/" + p_member)) && off + TAR_BLOCK_SIZE + size <= total) {
			return p_tar.slice(off + TAR_BLOCK_SIZE, off + TAR_BLOCK_SIZE + size);
		}
		off += TAR_BLOCK_SIZE + ((size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
	}
	return PackedByteArray();
}

// --- GitHub release download -------------------------------------------------

String pick_release_asset(const Dictionary &p_release, const String &p_suffix, String &r_tag) {
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

bool write_executable(const String &p_path, const PackedByteArray &p_bytes, const PlatformInfo &p_platform) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_buffer(p_bytes);
	f->close();
	if (p_platform.exe_suffix.is_empty()) {
		// FileAccess cannot set unix permission bits; the downloaded binary
		// must be executable to be of any use.
		PackedStringArray args;
		args.push_back("+x");
		args.push_back(p_path);
		OS::get_singleton()->execute("chmod", args);
	}
	return true;
}

// download_latest_release fetches the newest GitHub release, extracts the
// platform binary and installs it under p_cache_dir. Returns the binary path
// or empty on failure (with a pushed warning explaining why).
String download_latest_release(const String &p_cache_dir, const PlatformInfo &p_platform, const String &p_binary_name, const CliSpec &p_spec) {
	const String display = p_spec.display_name;
	if (p_platform.arch.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] Unsupported CPU architecture for " + display + " auto-download.");
		return String();
	}

	const String api_url = String("https://api.github.com/repos/") + p_spec.repo + "/releases/latest";
	UtilityFunctions::print("[ProcCity] Looking up the latest " + display + " release...");
	const HttpResponse api = http_get(api_url, "application/vnd.github+json", MAX_API_REDIRECTS);
	if (!api.ok()) {
		UtilityFunctions::push_warning("[ProcCity] Release lookup failed: " + (api.error.is_empty() ? "HTTP " + String::num_int64(api.code) : api.error));
		return String();
	}

	const Variant parsed = JSON::parse_string(api.body.get_string_from_utf8());
	if (parsed.get_type() != Variant::DICTIONARY) {
		UtilityFunctions::push_warning("[ProcCity] Release lookup returned unexpected JSON.");
		return String();
	}
	const bool zip = p_platform.os == "windows";
	const String suffix = "_" + p_platform.os + "_" + p_platform.arch + (zip ? ".zip" : ".tar.gz");
	String tag;
	const String asset_url = pick_release_asset(parsed, suffix, tag);
	if (asset_url.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] Release " + tag + " has no asset matching *" + suffix + ".");
		return String();
	}

	UtilityFunctions::print("[ProcCity] Downloading " + display + " " + tag + " (" + asset_url.get_file() + ")...");
	const HttpResponse archive = http_get(asset_url, "application/octet-stream", MAX_ASSET_REDIRECTS);
	if (!archive.ok()) {
		UtilityFunctions::push_warning("[ProcCity] Asset download failed: " + (archive.error.is_empty() ? "HTTP " + String::num_int64(archive.code) : archive.error));
		return String();
	}

	if (DirAccess::make_dir_recursive_absolute(p_cache_dir) != OK) {
		UtilityFunctions::push_warning("[ProcCity] Could not create cache dir " + p_cache_dir + ".");
		return String();
	}

	PackedByteArray binary;
	if (zip) {
		binary = extract_zip_member(archive.body, p_binary_name, p_cache_dir);
	} else {
		const PackedByteArray tar = archive.body.decompress_dynamic(MAX_DOWNLOAD_BYTES, FileAccess::COMPRESSION_GZIP);
		binary = extract_tar_member(tar, p_binary_name);
	}
	if (binary.is_empty()) {
		UtilityFunctions::push_warning("[ProcCity] " + asset_url.get_file() + " did not contain " + p_binary_name + ".");
		return String();
	}

	const String dest = p_cache_dir.path_join(p_binary_name);
	if (!write_executable(dest, binary, p_platform)) {
		UtilityFunctions::push_warning("[ProcCity] Could not write " + dest + ".");
		return String();
	}
	Ref<FileAccess> version = FileAccess::open(p_cache_dir.path_join("VERSION"), FileAccess::WRITE);
	if (version.is_valid()) {
		version->store_string(tag);
		version->close();
	}
	UtilityFunctions::print("[ProcCity] Installed " + display + " " + tag + " at " + dest);
	return dest;
}

} // namespace

String godot::resolve_cli_binary(CliKind p_kind, const String &p_override, bool p_allow_download) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	const CliSpec &spec = spec_for(p_kind);
	const PlatformInfo platform = detect_platform();
	const String binary_name = String(spec.cli_stem) + platform.exe_suffix;

	if (!p_override.is_empty()) {
		const String g = ps->globalize_path(p_override);
		if (FileAccess::file_exists(g)) {
			return g;
		}
	}

	PackedStringArray bundled_names;
	bundled_names.push_back(binary_name);
	if (spec.legacy_stem) {
		bundled_names.push_back(String(spec.legacy_stem) + platform.exe_suffix);
	}
	for (const char *root : spec.bundled_roots) {
		if (!root) {
			continue;
		}
		for (int i = 0; i < bundled_names.size(); i++) {
			const String base = String("res://addons/procedural_city/") + root + "/" + platform.bundle_dir + "/" + bundled_names[i];
			const String g = ps->globalize_path(base);
			if (FileAccess::file_exists(g)) {
				return g;
			}
		}
	}

	const String cache_dir = ps->globalize_path(spec.cache_dir);
	const String cached = cache_dir.path_join(binary_name);
	if (FileAccess::file_exists(cached)) {
		return cached;
	}

	if (p_allow_download) {
		return download_latest_release(cache_dir, platform, binary_name, spec);
	}
	return String();
}

String godot::resolve_cli_binary(const String &p_override, bool p_allow_download) {
	return resolve_cli_binary(CliKind::GODISPLACEMENTX, p_override, p_allow_download);
}
