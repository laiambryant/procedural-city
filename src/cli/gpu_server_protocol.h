#ifndef GPU_SERVER_PROTOCOL_H
#define GPU_SERVER_PROTOCOL_H

#include "cli/goplacementx_params.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

PackedStringArray serve_args();

String build_request_line(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params);

Dictionary reply_ok(const Dictionary &p_images);
Dictionary reply_error(const String &p_message);

bool read_frame_locked(const Ref<FileAccess> &p_pipe, String &r_path, Ref<Image> &r_image);

} // namespace godot

#endif // GPU_SERVER_PROTOCOL_H
