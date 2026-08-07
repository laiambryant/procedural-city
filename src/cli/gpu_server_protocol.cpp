#include "cli/gpu_server_protocol.h"

#include "cli/gdxraw_loader.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <vector>

using namespace godot;

// Sanity caps on the length-prefixed frames: reject anything claiming a path
// over 64 KiB or a map beyond the decoder's supported 8192 RGBA envelope as a
// corrupt stream.
static constexpr uint32_t MAX_FRAME_PATH_BYTES = 1u << 16;
static constexpr uint32_t MAX_FRAME_BLOB_BYTES = (uint32_t)GDXRAW_MAX_STREAM_BYTES;

// Width of the little-endian length prefix in front of each frame field.
static constexpr uint64_t FRAME_LENGTH_BYTES = 4;

PackedStringArray godot::serve_args() {
	PackedStringArray args;
	args.push_back("serve");
	return args;
}

// append_size sends an explicit width/height when the params carry one, and
// falls back to the square resolution otherwise — the same choice the one-shot
// CLI makes from its config file.
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

String godot::build_request_line(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params) {
	Dictionary req;
	req["config"] = p_config;
	append_size(req, p_params);
	req["invert"] = p_params.is_valid() && p_params->get_invert();
	req["gradient"] = p_params.is_valid() ? p_params->gradient_to_string() : String();
	req["emits"] = p_emits;
	return JSON::stringify(req);
}

Dictionary godot::reply_ok(const Dictionary &p_images) {
	Dictionary out;
	out["code"] = 0;
	out["output"] = String();
	out["images"] = p_images;
	return out;
}

Dictionary godot::reply_error(const String &p_message) {
	Dictionary out;
	out["code"] = 1;
	out["output"] = p_message;
	return out;
}

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
	uint8_t b[FRAME_LENGTH_BYTES];
	if (!read_exact(p_pipe, b, FRAME_LENGTH_BYTES)) {
		return false;
	}
	r_value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return true;
}

// read_chunk pulls one length-prefixed field, refusing a length past p_max as a
// desynchronized stream rather than trying to allocate it.
static bool read_chunk(const Ref<FileAccess> &p_pipe, uint32_t p_max, PackedByteArray &r_bytes) {
	uint32_t len;
	if (!read_u32le(p_pipe, len) || len > p_max) {
		return false;
	}
	r_bytes.resize(len);
	return read_exact(p_pipe, r_bytes.ptrw(), len);
}

static bool read_map_image(const Ref<FileAccess> &p_pipe, Ref<Image> &r_image) {
	uint32_t len;
	if (!read_u32le(p_pipe, len) || len > MAX_FRAME_BLOB_BYTES) {
		return false;
	}
	constexpr uint32_t READ_CHUNK_BYTES = 1u << 20;
	std::vector<uint8_t> chunk(MIN(len, READ_CHUNK_BYTES));
	GdxrawStreamDecoder decoder((int64_t)len);
	uint32_t remaining = len;
	while (remaining > 0) {
		const uint32_t take = MIN(remaining, READ_CHUNK_BYTES);
		if (!read_exact(p_pipe, chunk.data(), take) || !decoder.append(chunk.data(), take)) {
			return false;
		}
		remaining -= take;
	}
	r_image = decoder.finish();
	return r_image.is_valid();
}

bool godot::read_frame(const Ref<FileAccess> &p_pipe, String &r_path, Ref<Image> &r_image) {
	PackedByteArray path_bytes;
	if (!read_chunk(p_pipe, MAX_FRAME_PATH_BYTES, path_bytes)) {
		return false;
	}
	r_path = String::utf8((const char *)path_bytes.ptr(), path_bytes.size());

	return read_map_image(p_pipe, r_image);
}
