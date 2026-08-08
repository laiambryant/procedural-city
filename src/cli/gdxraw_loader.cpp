#include "cli/gdxraw_loader.h"

#include <godot_cpp/classes/file_access.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

using namespace godot;

namespace {

constexpr int GDXRAW_HEADER_SIZE = (int)GDXRAW_HEADER_BYTES;
constexpr uint8_t GDXRAW_VERSION = 1;
constexpr int GDXRAW_VERSION_OFFSET = 4;
constexpr int GDXRAW_FORMAT_OFFSET = 5;
constexpr int GDXRAW_WIDTH_OFFSET = 8;
constexpr int GDXRAW_HEIGHT_OFFSET = 12;

constexpr uint8_t GDXRAW_MAGIC[] = { 'G', 'D', 'X', 'R' };
constexpr uint8_t PNG_MAGIC[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

bool gdxraw_format(uint8_t p_code, Image::Format &r_format, int &r_bytes_per_pixel) {
	switch (p_code) {
		case 0:
			r_format = Image::FORMAT_L8;
			r_bytes_per_pixel = 1;
			return true;
		case 1:
			r_format = Image::FORMAT_RGB8;
			r_bytes_per_pixel = 3;
			return true;
		case 2:
			r_format = Image::FORMAT_RGBA8;
			r_bytes_per_pixel = 4;
			return true;
		default:
			return false;
	}
}

bool starts_with(const uint8_t *p_bytes, int64_t p_bytes_size, const uint8_t *p_magic, int64_t p_magic_size) {
	if (p_bytes == nullptr || p_bytes_size < p_magic_size) {
		return false;
	}
	return memcmp(p_bytes, p_magic, (size_t)p_magic_size) == 0;
}

uint32_t read_u32le(const uint8_t *p_bytes) {
	return (uint32_t)p_bytes[0] | ((uint32_t)p_bytes[1] << 8) | ((uint32_t)p_bytes[2] << 16) | ((uint32_t)p_bytes[3] << 24);
}

bool parse_gdxraw_header(const uint8_t *p_bytes, int64_t p_size, int &r_width, int &r_height,
		Image::Format &r_format, int64_t &r_payload_size) {
	if (!starts_with(p_bytes, p_size, GDXRAW_MAGIC, std::size(GDXRAW_MAGIC)) ||
			p_size < GDXRAW_HEADER_SIZE || p_bytes[GDXRAW_VERSION_OFFSET] != GDXRAW_VERSION) {
		return false;
	}
	int bytes_per_pixel = 0;
	if (!gdxraw_format(p_bytes[GDXRAW_FORMAT_OFFSET], r_format, bytes_per_pixel)) {
		return false;
	}
	const uint64_t width = read_u32le(p_bytes + GDXRAW_WIDTH_OFFSET);
	const uint64_t height = read_u32le(p_bytes + GDXRAW_HEIGHT_OFFSET);
	if (width == 0 || height == 0 || width > (uint64_t)GDXRAW_MAX_DIMENSION ||
			height > (uint64_t)GDXRAW_MAX_DIMENSION) {
		return false;
	}
	const uint64_t pixels = width * height;
	if (height != 0 && pixels / height != width ||
			pixels > (uint64_t)std::numeric_limits<int64_t>::max() / (uint64_t)bytes_per_pixel) {
		return false;
	}
	r_width = (int)width;
	r_height = (int)height;
	r_payload_size = (int64_t)(pixels * (uint64_t)bytes_per_pixel);
	return true;
}

Ref<Image> decode_png_bytes(const PackedByteArray &p_bytes) {
	Ref<Image> image;
	image.instantiate();
	if (image->load_png_from_buffer(p_bytes) != OK) {
		return Ref<Image>();
	}
	return image;
}

} // namespace

GdxrawStreamDecoder::GdxrawStreamDecoder(int64_t p_known_stream_bytes, int64_t p_max_payload_bytes) :
		known_stream_bytes(p_known_stream_bytes),
		max_payload_bytes(MIN(p_max_payload_bytes, GDXRAW_MAX_PAYLOAD_BYTES)) {
	if (known_stream_bytes < -1 || known_stream_bytes > GDXRAW_MAX_STREAM_BYTES ||
			max_payload_bytes < 0) {
		failed = true;
	}
}

bool GdxrawStreamDecoder::has_known_stream_length() const {
	return known_stream_bytes >= 0;
}

bool GdxrawStreamDecoder::parse_header() {
	if (!parse_gdxraw_header(header, GDXRAW_HEADER_SIZE, width, height, format, expected_payload_bytes)) {
		failed = true;
		return false;
	}
	if (expected_payload_bytes > max_payload_bytes ||
			(has_known_stream_length() && known_stream_bytes - GDXRAW_HEADER_SIZE != expected_payload_bytes)) {
		failed = true;
		return false;
	}
	payload.resize(expected_payload_bytes);
	return true;
}

bool GdxrawStreamDecoder::append(const uint8_t *p_bytes, int64_t p_size) {
	if (failed || p_size < 0 || (p_size > 0 && p_bytes == nullptr)) {
		failed = true;
		return false;
	}
	const int64_t received = header_bytes + payload_bytes;
	if (has_known_stream_length() && (received > known_stream_bytes || p_size > known_stream_bytes - received)) {
		failed = true;
		return false;
	}
	int64_t offset = 0;
	if (header_bytes < GDXRAW_HEADER_SIZE) {
		const int64_t take = std::min<int64_t>(p_size, GDXRAW_HEADER_SIZE - header_bytes);
		if (take > 0) {
			memcpy(header + header_bytes, p_bytes, (size_t)take);
			header_bytes += take;
			offset += take;
		}
		if (header_bytes == GDXRAW_HEADER_SIZE && expected_payload_bytes < 0 && !parse_header()) {
			return false;
		}
	}
	if (offset < p_size) {
		const int64_t remaining = expected_payload_bytes - payload_bytes;
		const int64_t incoming = p_size - offset;
		if (expected_payload_bytes < 0 || incoming > remaining) {
			failed = true;
			return false;
		}
		memcpy(payload.ptrw() + payload_bytes, p_bytes + offset, (size_t)incoming);
		payload_bytes += incoming;
	}
	return true;
}

Ref<Image> GdxrawStreamDecoder::finish() const {
	if (failed || expected_payload_bytes < 0 || payload_bytes != expected_payload_bytes) {
		return Ref<Image>();
	}
	return Image::create_from_data(width, height, false, format, payload);
}

Ref<Image> godot::decode_gdxraw(const PackedByteArray &p_bytes) {
	return decode_gdxraw_bytes(p_bytes.ptr(), p_bytes.size());
}

Ref<Image> godot::decode_gdxraw_bytes(const uint8_t *p_bytes, int64_t p_size) {
	GdxrawStreamDecoder decoder(p_size);
	if (!decoder.append(p_bytes, p_size)) {
		return Ref<Image>();
	}
	return decoder.finish();
}

Ref<Image> godot::load_map_image(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return Ref<Image>();
	}
	uint8_t header[GDXRAW_HEADER_SIZE] = {};
	const uint64_t header_size = file->get_buffer(header, GDXRAW_HEADER_SIZE);
	if (starts_with(header, (int64_t)header_size, GDXRAW_MAGIC, std::size(GDXRAW_MAGIC))) {
		const uint64_t file_length = file->get_length();
		if (file_length > (uint64_t)GDXRAW_MAX_STREAM_BYTES) {
			return Ref<Image>();
		}
		GdxrawStreamDecoder decoder((int64_t)file_length);
		if (!decoder.append(header, (int64_t)header_size)) {
			return Ref<Image>();
		}
		constexpr uint64_t READ_CHUNK_BYTES = 1u << 20;
		std::vector<uint8_t> chunk(READ_CHUNK_BYTES);
		while (file->get_position() < file->get_length()) {
			const uint64_t remaining = file->get_length() - file->get_position();
			const uint64_t got = file->get_buffer(chunk.data(), MIN(remaining, READ_CHUNK_BYTES));
			if (got == 0 || !decoder.append(chunk.data(), (int64_t)got)) {
				return Ref<Image>();
			}
		}
		return decoder.finish();
	}
	if (starts_with(header, (int64_t)header_size, PNG_MAGIC, std::size(PNG_MAGIC))) {
		if (p_path.get_extension().to_lower() == "png") {
			return Image::load_from_file(p_path);
		}
		return decode_png_bytes(FileAccess::get_file_as_bytes(p_path));
	}
	return Image::load_from_file(p_path);
}

String godot::map_extension_for(bool p_keep_inspectable, bool p_cli_supports_raw) {
	return (p_keep_inspectable || !p_cli_supports_raw) ? String(".png") : String(".gdxraw");
}
