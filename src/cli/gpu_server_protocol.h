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

// The wire format spoken to `gpudisplacementx serve`: one JSON request line in,
// one length-prefixed frame per emitted map back. Kept apart from the server's
// process lifecycle so the framing can be reasoned about on its own.

// Argv that puts the CLI into server mode.
PackedStringArray serve_args();

// build_request_line renders one bundle request as the single JSON line the
// server reads per request.
String build_request_line(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params);

// Result dictionaries in the shape GoplacementxRunner::run_bundle returns, so
// callers cannot tell a served run from a one-shot CLI run.
Dictionary reply_ok(const Dictionary &p_images);
Dictionary reply_error(const String &p_message);

// read_frame blocks until one whole path+blob frame has arrived, decoding the
// blob as .gdxraw. The GPU device is single and the pipe carries one
// request/response at a time, so every read here runs under the server's
// io_mutex. Returns false on a short read or a length that fails the sanity
// caps, which is how a dead or desynchronized server is detected.
bool read_frame(const Ref<FileAccess> &p_pipe, String &r_path, Ref<Image> &r_image);

} // namespace godot

#endif // GPU_SERVER_PROTOCOL_H
