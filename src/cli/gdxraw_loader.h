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

// load_map_image loads a goplacementx output map: the .gdxraw interchange
// format (no encode or decode work) or any Image-supported file for other
// extensions.
Ref<Image> load_map_image(const String &p_path);

String map_extension_for(bool p_keep_inspectable);

} // namespace godot

#endif // GDXRAW_LOADER_H
