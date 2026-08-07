#ifndef GDXRAW_LOADER_H
#define GDXRAW_LOADER_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// GoplacementxParams exposes 8192 as the largest supported map edge. Keeping
// the transport decoder on that same contract makes an untrusted header's
// worst-case allocation explicit: one 8192x8192 RGBA8 payload (256 MiB).
inline constexpr int GDXRAW_MAX_DIMENSION = 8192;
inline constexpr int64_t GDXRAW_HEADER_BYTES = 16;
inline constexpr int64_t GDXRAW_MAX_PAYLOAD_BYTES =
		(int64_t)GDXRAW_MAX_DIMENSION * (int64_t)GDXRAW_MAX_DIMENSION * 4;
inline constexpr int64_t GDXRAW_MAX_STREAM_BYTES = GDXRAW_HEADER_BYTES + GDXRAW_MAX_PAYLOAD_BYTES;

// Incremental decoder for chunked GDXR transports. It buffers only the fixed
// header, allocates the final Image payload exactly once, and copies incoming
// bytes directly into that payload.
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

	bool parse_header();

public:
	// p_known_stream_bytes is the complete header+payload length when framing
	// already supplied it (memory blobs, files, legacy pipe frames). Passing it
	// rejects a lying header before allocation. Chunked transports may leave it
	// unknown and rely on the dimensions/payload cap instead.
	explicit GdxrawStreamDecoder(int64_t p_known_stream_bytes = -1,
			int64_t p_max_payload_bytes = GDXRAW_MAX_PAYLOAD_BYTES);
	bool append(const uint8_t *p_bytes, int64_t p_size);
	Ref<Image> finish() const;
};

// decode_gdxraw builds an Image from an in-memory .gdxraw blob (16-byte GDXR
// header + tightly packed L8/RGB8/RGBA8 rows), so a map streamed over the GPU
// server pipe decodes identically to one read from a .gdxraw file. Returns a
// null Ref on a malformed blob.
Ref<Image> decode_gdxraw(const PackedByteArray &p_bytes);

// Pointer variant for gRPC/protobuf strings, avoiding a full framed-blob copy
// into PackedByteArray before the Image payload is created.
Ref<Image> decode_gdxraw_bytes(const uint8_t *p_bytes, int64_t p_size);

// load_map_image loads a goplacementx output map, picking the decoder from the
// file's leading magic bytes rather than its extension: the .gdxraw interchange
// format (no encode or decode work), PNG, or any Image-supported file
// otherwise. Sniffing keeps a map readable even when the CLI ignored the
// requested extension and wrote a different format to the path.
Ref<Image> load_map_image(const String &p_path);

// map_extension_for picks the interchange format for the CLI's output maps.
// .gdxraw skips the PNG encode/decode round-trip, so it is worth asking for --
// but only from a CLI that can actually write it, and only when the user is not
// keeping intermediates around to inspect.
String map_extension_for(bool p_keep_inspectable, bool p_cli_supports_raw);

} // namespace godot

#endif // GDXRAW_LOADER_H
