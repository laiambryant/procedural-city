#ifndef GOPLACEMENTX_PARAMS_PROTO_H
#define GOPLACEMENTX_PARAMS_PROTO_H

#include "cli/goplacementx_params.h"
#include "displacement.pb.h"

namespace godot {

// params_to_proto builds a displacement::v1::Params directly from a
// GoplacementxParams resource, field for field - the in-memory equivalent of
// GoplacementxParams::to_json(), used by the gRPC transport so no config file
// ever touches disk.
displacement::v1::Params params_to_proto(const Ref<GoplacementxParams> &p_params);

// render_options_from builds the RenderOptions message shared by every RPC
// (width/height/resolution, invert, fast, gradient), matching
// append_size_flags/append_invert_flag/append_fast_flag/append_gradient_flag
// in goplacementx_runner.cpp.
displacement::v1::RenderOptions render_options_from(const Ref<GoplacementxParams> &p_params);

// proto_params_to_dict is the inverse of params_to_proto: the same camelCase
// keys GoplacementxParams::from_dict() already accepts, so a server's
// Randomize reply can be applied straight onto a GoplacementxParams resource.
Dictionary proto_params_to_dict(const displacement::v1::Params &p_params);

} // namespace godot

#endif // GOPLACEMENTX_PARAMS_PROTO_H
