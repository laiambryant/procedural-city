#ifndef ARCHIVE_EXTRACT_H
#define ARCHIVE_EXTRACT_H

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// Release assets arrive in memory, never on disk, so both extractors take the
// whole archive as bytes and hand back one member. Both match p_member by
// basename, since the archives nest the binary under a versioned directory.

// extract_zip_member stages the archive as a file first: ZIPReader can only
// open a path, so p_stage_dir gets a scratch copy that is deleted on the way
// out.
PackedByteArray extract_zip_member(const PackedByteArray &p_zip, const String &p_member, const String &p_stage_dir);

// extract_tar_member walks the block structure of an already-gunzipped tar
// stream and returns the first regular file matching p_member.
PackedByteArray extract_tar_member(const PackedByteArray &p_tar, const String &p_member);

} // namespace godot

#endif // ARCHIVE_EXTRACT_H
