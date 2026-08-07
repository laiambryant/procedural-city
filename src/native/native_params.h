#ifndef PROC_CITY_NATIVE_PARAMS_H
#define PROC_CITY_NATIVE_PARAMS_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cppdisplacementx/params.h>
#include <cppdisplacementx/post.h>
#include <cppdisplacementx/sprite_atlas.h>

#include "cli/goplacementx_params.h"

namespace godot {

// Bridges the inspector-facing resource onto the engine core's plain structs.
// The two field sets are the same generator contract expressed twice, so the
// mapping is deliberately mechanical.
cppdx::Params to_native_params(const Ref<GoplacementxParams> &p_params);
Vector2i native_canvas_size(const Ref<GoplacementxParams> &p_params);
std::vector<cppdx::ColorRgb> to_native_gradient(const Ref<GoplacementxParams> &p_params);
cppdx::OutputMode to_native_output_mode(const String &p_mode);

// Loads the selected sprite packs in canonical pack order, which is the order
// the draw commands index into. Returns an atlas with one empty sprite when
// sprites are off, since a zero-sized buffer cannot be uploaded.
cppdx::SpriteAtlas build_native_atlas(const cppdx::Params &p_params);

Ref<Image> canvas_to_image(const cppdx::Canvas &p_canvas);

} // namespace godot

#endif // PROC_CITY_NATIVE_PARAMS_H
