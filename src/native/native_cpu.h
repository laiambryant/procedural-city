#ifndef NATIVE_CPU_H
#define NATIVE_CPU_H

#include "meshing/parallel_rows.h"

#include <cppdisplacementx/cpu_compositor.h>

#include <algorithm>

namespace godot {

inline void composite_native_cpu(cppdx::Canvas &r_canvas, const cppdx::CommandList &p_commands,
		const cppdx::AtlasView &p_atlas) {
	constexpr int MIN_PIXELS_PER_BAND = 65536;
	const int min_rows = std::max(1, MIN_PIXELS_PER_BAND / std::max(1, (int)r_canvas.width));
	parallel_for_rows((int)r_canvas.height, [&](int p_begin, int p_end) {
		if (p_begin == 0 && p_end == (int)r_canvas.height) {
			cppdx::composite_cpu(r_canvas, p_commands, p_atlas, 1);
			return;
		}
		cppdx::Canvas band(r_canvas.width, (uint32_t)(p_end - p_begin));
		const size_t offset = (size_t)p_begin * r_canvas.width;
		std::copy_n(r_canvas.pixels.data() + offset, band.pixels.size(), band.pixels.data());
		cppdx::CommandList commands;
		commands.reserve(p_commands.size());
		for (const cppdx::DrawCommand &source : p_commands) {
			const int begin = std::max(p_begin, source.clip_y);
			const int end = std::min(p_end, source.clip_y + source.clip_h);
			if (begin >= end) {
				continue;
			}
			cppdx::DrawCommand command = source;
			command.y -= p_begin;
			command.clip_y = begin - p_begin;
			command.clip_h = end - begin;
			commands.push_back(command);
		}
		cppdx::composite_cpu(band, commands, p_atlas, 1);
		std::copy(band.pixels.begin(), band.pixels.end(), r_canvas.pixels.data() + offset);
	},
			min_rows);
}

} // namespace godot

#endif // NATIVE_CPU_H
