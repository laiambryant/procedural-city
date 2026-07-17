#include "cli/gdxraw_loader.h"

#include <godot_cpp/classes/file_access.hpp>

using namespace godot;

namespace {

// Header layout: bytes 0-3 magic "GDXR", 4 version, 5 format code,
// 6-7 reserved, 8-11 width (u32 LE), 12-15 height (u32 LE).
constexpr int GDXRAW_HEADER_SIZE = 16;
constexpr uint8_t GDXRAW_VERSION = 1;
constexpr int GDXRAW_VERSION_OFFSET = 4;
constexpr int GDXRAW_FORMAT_OFFSET = 5;
constexpr int GDXRAW_WIDTH_OFFSET = 8;
constexpr int GDXRAW_HEIGHT_OFFSET = 12;

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

bool gdxraw_header_matches(const PackedByteArray &p_bytes) {
	const uint8_t *b = p_bytes.ptr();
	return b[0] == 'G' && b[1] == 'D' && b[2] == 'X' && b[3] == 'R' && b[GDXRAW_VERSION_OFFSET] == GDXRAW_VERSION;
}

uint32_t read_u32le(const uint8_t *p_bytes) {
	return (uint32_t)p_bytes[0] | ((uint32_t)p_bytes[1] << 8) | ((uint32_t)p_bytes[2] << 16) | ((uint32_t)p_bytes[3] << 24);
}

} // namespace

Ref<Image> godot::decode_gdxraw(const PackedByteArray &p_bytes) {
	if (p_bytes.size() < GDXRAW_HEADER_SIZE || !gdxraw_header_matches(p_bytes)) {
		return Ref<Image>();
	}
	Image::Format format;
	int bytes_per_pixel;
	if (!gdxraw_format(p_bytes[GDXRAW_FORMAT_OFFSET], format, bytes_per_pixel)) {
		return Ref<Image>();
	}
	const uint8_t *b = p_bytes.ptr();
	const int64_t width = read_u32le(b + GDXRAW_WIDTH_OFFSET);
	const int64_t height = read_u32le(b + GDXRAW_HEIGHT_OFFSET);
	if (width <= 0 || height <= 0) {
		return Ref<Image>();
	}
	const int64_t expected = width * height * bytes_per_pixel;
	if (p_bytes.size() - GDXRAW_HEADER_SIZE < expected) {
		return Ref<Image>();
	}
	const PackedByteArray payload = p_bytes.slice(GDXRAW_HEADER_SIZE, GDXRAW_HEADER_SIZE + expected);
	return Image::create_from_data(width, height, false, format, payload);
}

Ref<Image> godot::load_map_image(const String &p_path) {
	if (p_path.to_lower().ends_with(".gdxraw")) {
		return decode_gdxraw(FileAccess::get_file_as_bytes(p_path));
	}
	return Image::load_from_file(p_path);
}

String godot::map_extension_for(bool p_keep_inspectable) {
	return p_keep_inspectable ? String(".png") : String(".gdxraw");
}
