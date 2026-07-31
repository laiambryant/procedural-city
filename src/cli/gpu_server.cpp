#include "cli/gpu_server.h"

#include "cli/gpu_server_protocol.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

ProcCityGpuServer *ProcCityGpuServer::singleton = nullptr;

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
