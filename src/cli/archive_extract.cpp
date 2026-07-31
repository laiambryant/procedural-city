#include "cli/archive_extract.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstring>

using namespace godot;

// Name of the scratch file extract_zip_member stages the in-memory archive as.
static const char *ZIP_STAGE_NAME = "cli_download.zip";

// POSIX ustar layout: 512-byte blocks, with the header storing the file name in
// the first 100 bytes, the octal size at offset 124 (12 bytes) and the entry
// type flag at offset 156. A type of '0' or NUL means a regular file.
static constexpr int64_t TAR_BLOCK_SIZE = 512;
static constexpr int TAR_NAME_FIELD_LEN = 100;
static constexpr int TAR_SIZE_FIELD_OFFSET = 124;
static constexpr int TAR_SIZE_FIELD_LEN = 12;
static constexpr int TAR_TYPE_FIELD_OFFSET = 156;

// stage_archive writes the in-memory archive out so ZIPReader has a path to
// open, and returns that path (empty when the write failed).
static String stage_archive(const PackedByteArray &p_zip, const String &p_stage_dir) {
	const String stage = p_stage_dir.path_join(ZIP_STAGE_NAME);
	Ref<FileAccess> f = FileAccess::open(stage, FileAccess::WRITE);
	if (f.is_null()) {
		return String();
	}
	f->store_buffer(p_zip);
	f->close();
	return stage;
}

static PackedByteArray read_zip_member(const String &p_stage, const String &p_member) {
	PackedByteArray out;
	Ref<ZIPReader> zip;
	zip.instantiate();
	if (zip->open(p_stage) != OK) {
		return out;
	}
	const PackedStringArray files = zip->get_files();
	for (int i = 0; i < files.size(); i++) {
		if (files[i] == p_member || files[i].ends_with("/" + p_member)) {
			out = zip->read_file(files[i]);
			break;
		}
	}
	zip->close();
	return out;
}

PackedByteArray godot::extract_zip_member(const PackedByteArray &p_zip, const String &p_member, const String &p_stage_dir) {
	const String stage = stage_archive(p_zip, p_stage_dir);
	if (stage.is_empty()) {
		return PackedByteArray();
	}
	const PackedByteArray out = read_zip_member(stage, p_member);
	Ref<DirAccess> da = DirAccess::open(p_stage_dir);
	if (da.is_valid()) {
		da->remove(stage.get_file());
	}
	return out;
}

static int64_t parse_tar_octal(const uint8_t *p_field, int p_len) {
	int64_t value = 0;
	for (int i = 0; i < p_len; i++) {
		const uint8_t c = p_field[i];
		if (c < '0' || c > '7') {
			break;
		}
		value = value * 8 + (c - '0');
	}
	return value;
}

// A tar stream terminates with zero-filled blocks, so a header whose first byte
// is NUL means there are no more entries.
static bool is_end_of_archive(const uint8_t *p_header) {
	return p_header[0] == 0;
}

static String tar_entry_name(const uint8_t *p_header) {
	char name_buf[TAR_NAME_FIELD_LEN + 1];
	memcpy(name_buf, p_header, TAR_NAME_FIELD_LEN);
	name_buf[TAR_NAME_FIELD_LEN] = 0;
	return String::utf8(name_buf);
}

// blocks_for rounds a member's byte size up to whole tar blocks, which is the
// stride from one header to the next.
static int64_t blocks_for(int64_t p_size) {
	return ((p_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
}

PackedByteArray godot::extract_tar_member(const PackedByteArray &p_tar, const String &p_member) {
	const uint8_t *data = p_tar.ptr();
	const int64_t total = p_tar.size();
	int64_t off = 0;
	while (off + TAR_BLOCK_SIZE <= total) {
		const uint8_t *hdr = data + off;
		if (is_end_of_archive(hdr)) {
			break;
		}
		const String name = tar_entry_name(hdr);
		const int64_t size = parse_tar_octal(hdr + TAR_SIZE_FIELD_OFFSET, TAR_SIZE_FIELD_LEN);
		const uint8_t type = hdr[TAR_TYPE_FIELD_OFFSET];
		const bool regular = type == '0' || type == 0;
		if (regular && (name == p_member || name.ends_with("/" + p_member)) && off + TAR_BLOCK_SIZE + size <= total) {
			return p_tar.slice(off + TAR_BLOCK_SIZE, off + TAR_BLOCK_SIZE + size);
		}
		off += TAR_BLOCK_SIZE + blocks_for(size);
	}
	return PackedByteArray();
}
