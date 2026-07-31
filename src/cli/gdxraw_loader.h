#ifndef GDXRAW_LOADER_H
#define GDXRAW_LOADER_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// decode_gdxraw builds an Image from an in-memory .gdxraw blob (16-byte GDXR
// header + tightly packed L8/RGB8/RGBA8 rows), so a map streamed over the GPU
// server pipe decodes identically to one read from a .gdxraw file. Returns a
// null Ref on a malformed blob.
Ref<Image> decode_gdxraw(const PackedByteArray &p_bytes);

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
