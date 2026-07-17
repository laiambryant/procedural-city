#include "cli/gpu_server.h"

#include "cli/gdxraw_loader.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

ProcCityGpuServer *ProcCityGpuServer::singleton = nullptr;

static PackedStringArray serve_args() {
	PackedStringArray args;
	args.push_back("serve");
	return args;
}

static void append_size(Dictionary &r_req, const Ref<GoplacementxParams> &p_params) {
	const int width = p_params.is_valid() ? p_params->get_out_width() : 0;
	const int height = p_params.is_valid() ? p_params->get_out_height() : 0;
	if (width > 0 && height > 0) {
		r_req["width"] = width;
		r_req["height"] = height;
	} else {
		r_req["resolution"] = p_params.is_valid() ? p_params->get_resolution() : GPX_DEFAULT_RESOLUTION;
	}
}

static String build_request_line(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params) {
	Dictionary req;
	req["config"] = p_config;
	append_size(req, p_params);
	req["invert"] = p_params.is_valid() && p_params->get_invert();
	req["fast"] = p_params.is_valid() && p_params->get_fast();
	req["gradient"] = p_params.is_valid() ? p_params->gradient_to_string() : String();
	req["emits"] = p_emits;
	return JSON::stringify(req);
}

static Dictionary reply_ok(const Dictionary &p_images) {
	Dictionary out;
	out["code"] = 0;
	out["output"] = String();
	out["images"] = p_images;
	return out;
}

static Dictionary reply_error(const String &p_message) {
	Dictionary out;
	out["code"] = 1;
	out["output"] = p_message;
	return out;
}

// The GPU device is single and the pipe carries one request/response at a time,
// so reads here run under io_mutex and block until the whole framed reply lands.
static bool read_exact(const Ref<FileAccess> &p_pipe, uint8_t *p_dst, uint64_t p_len) {
	uint64_t got = 0;
	while (got < p_len) {
		const uint64_t n = p_pipe->get_buffer(p_dst + got, p_len - got);
		if (n == 0) {
			return false;
		}
		got += n;
	}
	return true;
}

static bool read_u32le(const Ref<FileAccess> &p_pipe, uint32_t &r_value) {
	uint8_t b[4];
	if (!read_exact(p_pipe, b, 4)) {
		return false;
	}
	r_value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return true;
}

// Sanity caps on the length-prefixed frames: reject anything claiming a path
// over 64 KiB or a map blob over 1 GiB as a corrupt stream.
static constexpr uint32_t MAX_FRAME_PATH_BYTES = 1u << 16;
static constexpr uint32_t MAX_FRAME_BLOB_BYTES = 1u << 30;

static bool read_frame(const Ref<FileAccess> &p_pipe, String &r_path, Ref<Image> &r_image) {
	uint32_t path_len;
	if (!read_u32le(p_pipe, path_len) || path_len > MAX_FRAME_PATH_BYTES) {
		return false;
	}
	PackedByteArray path_bytes;
	path_bytes.resize(path_len);
	if (!read_exact(p_pipe, path_bytes.ptrw(), path_len)) {
		return false;
	}
	r_path = String::utf8((const char *)path_bytes.ptr(), path_len);

	uint32_t blob_len;
	if (!read_u32le(p_pipe, blob_len) || blob_len > MAX_FRAME_BLOB_BYTES) {
		return false;
	}
	PackedByteArray blob;
	blob.resize(blob_len);
	if (!read_exact(p_pipe, blob.ptrw(), blob_len)) {
		return false;
	}
	r_image = decode_gdxraw(blob);
	return true;
}

void ProcCityGpuServer::close_locked() {
	if (pipe.is_valid()) {
		pipe->close();
		pipe.unref();
	}
	if (pid >= 0 && OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	pid = -1;
	ready = false;
	started_binary = String();
}

bool ProcCityGpuServer::read_handshake_locked() {
	const String line = pipe->get_line();
	if (line.is_empty()) {
		return false;
	}
	const Variant parsed = JSON::parse_string(line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary handshake = parsed;
	return (bool)handshake.get("ready", false);
}

bool ProcCityGpuServer::ensure_started(const String &p_binary) {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (ready && pipe.is_valid() && pipe->is_open() && started_binary == p_binary) {
		return true;
	}
	close_locked();

	const Dictionary proc = OS::get_singleton()->execute_with_pipe(p_binary, serve_args());
	if (proc.is_empty()) {
		return false;
	}
	pipe = proc.get("stdio", Ref<FileAccess>());
	pid = (int32_t)(int64_t)proc.get("pid", -1);
	if (pipe.is_null() || !read_handshake_locked()) {
		close_locked();
		return false;
	}
	started_binary = p_binary;
	ready = true;
	return true;
}

Dictionary ProcCityGpuServer::run_bundle(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params) {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!ready || pipe.is_null() || !pipe->is_open()) {
		return reply_error("gpu server not running");
	}

	pipe->store_line(build_request_line(p_config, p_emits, p_params));
	pipe->flush();

	const String line = pipe->get_line();
	if (line.is_empty()) {
		close_locked();
		return reply_error("gpu server closed the pipe");
	}
	const Variant parsed = JSON::parse_string(line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		close_locked();
		return reply_error("gpu server sent an unparseable header");
	}
	const Dictionary header = parsed;
	if (!(bool)header.get("ok", false)) {
		return reply_error(String(header.get("error", "gpu server error")));
	}

	const int count = (int)header.get("count", 0);
	Dictionary images;
	for (int i = 0; i < count; i++) {
		String path;
		Ref<Image> image;
		if (!read_frame(pipe, path, image)) {
			close_locked();
			return reply_error("gpu server pipe closed mid-stream");
		}
		if (image.is_null()) {
			close_locked();
			return reply_error("gpu server sent an undecodable map");
		}
		images[path] = image;
	}
	return reply_ok(images);
}

bool ProcCityGpuServer::is_running() const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!ready || pipe.is_null() || !pipe->is_open()) {
		return false;
	}
	return pid < 0 || OS::get_singleton()->is_process_running(pid);
}

void ProcCityGpuServer::shutdown() {
	std::lock_guard<std::mutex> guard(io_mutex);
	close_locked();
}

void ProcCityGpuServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("ensure_started", "binary"), &ProcCityGpuServer::ensure_started);
	ClassDB::bind_method(D_METHOD("is_running"), &ProcCityGpuServer::is_running);
	ClassDB::bind_method(D_METHOD("shutdown"), &ProcCityGpuServer::shutdown);
}

ProcCityGpuServer::ProcCityGpuServer() {
	singleton = this;
}

ProcCityGpuServer::~ProcCityGpuServer() {
	close_locked();
	if (singleton == this) {
		singleton = nullptr;
	}
}
