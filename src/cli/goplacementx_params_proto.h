#ifndef GOPLACEMENTX_PARAMS_PROTO_H
#define GOPLACEMENTX_PARAMS_PROTO_H

#ifdef PROC_CITY_HAVE_GRPC

#include "cli/goplacementx_params.h"
#include "displacement.pb.h"

namespace godot {

displacement::v1::Params params_to_proto(const Ref<GoplacementxParams> &p_params);

displacement::v1::RenderOptions render_options_from(const Ref<GoplacementxParams> &p_params);

Dictionary proto_params_to_dict(const displacement::v1::Params &p_params);

} // namespace godot

#endif // PROC_CITY_HAVE_GRPC

#endif // GOPLACEMENTX_PARAMS_PROTO_H
