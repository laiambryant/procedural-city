#ifndef ARCHIVE_EXTRACT_H
#define ARCHIVE_EXTRACT_H

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

PackedByteArray extract_zip_member(const PackedByteArray &p_zip, const String &p_member, const String &p_stage_dir);

PackedByteArray extract_tar_member(const PackedByteArray &p_tar, const String &p_member);

} // namespace godot

#endif // ARCHIVE_EXTRACT_H
