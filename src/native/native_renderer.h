#ifndef PROC_CITY_NATIVE_RENDERER_H
#define PROC_CITY_NATIVE_RENDERER_H

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "cli/goplacementx_params.h"

namespace godot {

Dictionary run_native_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
		bool p_prefer_gpu, bool &r_used_gpu);

} // namespace godot

#endif // PROC_CITY_NATIVE_RENDERER_H
