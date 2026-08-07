#ifndef PROC_CITY_NATIVE_RENDERER_H
#define PROC_CITY_NATIVE_RENDERER_H

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "cli/goplacementx_params.h"

namespace godot {

// Renders a whole emit bundle in-process, returning the same
// { code, output, images } shape the CLI runners produce so the pipeline
// downstream cannot tell which backend ran. Maps come back as Images keyed by
// their planned path, so nothing is ever written to or read from disk.
//
// r_used_gpu reports which backend actually served the request: asking for the
// GPU and getting the CPU is a normal outcome on renderers without a
// RenderingDevice, not an error.
Dictionary run_native_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
		bool p_prefer_gpu, bool &r_used_gpu);

} // namespace godot

#endif // PROC_CITY_NATIVE_RENDERER_H
