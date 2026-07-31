#include "cli/gdxraw_loader.h"

#include <godot_cpp/classes/file_access.hpp>

#include <iterator>

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

// Leading bytes identifying the two formats the CLIs emit. load_map_image
// matches these against file content rather than against the filename, because
// a CLI build without raw support writes PNG bytes to whatever path it is
// handed -- including one named .gdxraw.
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

bool starts_with(const PackedByteArray &p_bytes, const uint8_t *p_magic, int64_t p_size) {
	if (p_bytes.size() < p_size) {
		return false;
	}
	const uint8_t *b = p_bytes.ptr();
	for (int64_t i = 0; i < p_size; i++) {
		if (b[i] != p_magic[i]) {
			return false;
		}
	}
	return true;
}

bool gdxraw_header_matches(const PackedByteArray &p_bytes) {
	return starts_with(p_bytes, GDXRAW_MAGIC, std::size(GDXRAW_MAGIC)) && p_bytes[GDXRAW_VERSION_OFFSET] == GDXRAW_VERSION;
}

uint32_t read_u32le(const uint8_t *p_bytes) {
	return (uint32_t)p_bytes[0] | ((uint32_t)p_bytes[1] << 8) | ((uint32_t)p_bytes[2] << 16) | ((uint32_t)p_bytes[3] << 24);
}

// decode_png_bytes decodes PNG content already held in memory. Image::load_from_file
// cannot stand in here: it picks a decoder from the filename extension, so it
// rejects PNG bytes stored under a .gdxraw name.
Ref<Image> decode_png_bytes(const PackedByteArray &p_bytes) {
	Ref<Image> image;
	image.instantiate();
	if (image->load_png_from_buffer(p_bytes) != OK) {
		return Ref<Image>();
	}
	return image;
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
	const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
	if (starts_with(bytes, GDXRAW_MAGIC, std::size(GDXRAW_MAGIC))) {
		return decode_gdxraw(bytes);
	}
	if (starts_with(bytes, PNG_MAGIC, std::size(PNG_MAGIC))) {
		return decode_png_bytes(bytes);
	}
	return Image::load_from_file(p_path);
}

String godot::map_extension_for(bool p_keep_inspectable, bool p_cli_supports_raw) {
	return (p_keep_inspectable || !p_cli_supports_raw) ? String(".png") : String(".gdxraw");
}
