#ifndef GDXRAW_LOADER_H
#define GDXRAW_LOADER_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

inline constexpr int GDXRAW_MAX_DIMENSION = 8192;
inline constexpr int64_t GDXRAW_HEADER_BYTES = 16;
inline constexpr int64_t GDXRAW_MAX_PAYLOAD_BYTES =
		(int64_t)GDXRAW_MAX_DIMENSION * (int64_t)GDXRAW_MAX_DIMENSION * 4;
inline constexpr int64_t GDXRAW_MAX_STREAM_BYTES = GDXRAW_HEADER_BYTES + GDXRAW_MAX_PAYLOAD_BYTES;

class GdxrawStreamDecoder {
	uint8_t header[GDXRAW_HEADER_BYTES] = {};
	int64_t header_bytes = 0;
	PackedByteArray payload;
	int64_t payload_bytes = 0;
	int64_t expected_payload_bytes = -1;
	int64_t known_stream_bytes = -1;
	int64_t max_payload_bytes = GDXRAW_MAX_PAYLOAD_BYTES;
	int width = 0;
	int height = 0;
	Image::Format format = Image::FORMAT_L8;
	bool failed = false;

	bool has_known_stream_length() const;
	bool parse_header();

public:
	explicit GdxrawStreamDecoder(int64_t p_known_stream_bytes = -1,
			int64_t p_max_payload_bytes = GDXRAW_MAX_PAYLOAD_BYTES);
	bool append(const uint8_t *p_bytes, int64_t p_size);
	Ref<Image> finish() const;
};

Ref<Image> decode_gdxraw(const PackedByteArray &p_bytes);

Ref<Image> decode_gdxraw_bytes(const uint8_t *p_bytes, int64_t p_size);

Ref<Image> load_map_image(const String &p_path);

String map_extension_for(bool p_keep_inspectable, bool p_cli_supports_raw);

} // namespace godot

#endif // GDXRAW_LOADER_H
