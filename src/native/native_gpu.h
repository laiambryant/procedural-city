#ifndef PROC_CITY_NATIVE_GPU_H
#define PROC_CITY_NATIVE_GPU_H

#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cppdisplacementx/canvas.h>
#include <cppdisplacementx/draw_command.h>
#include <cppdisplacementx/sprite_atlas.h>

namespace godot {

class RenderingDevice;

class NativeGpuSession {
public:
	bool begin(String &r_error);
	bool composite(const cppdx::CommandList &p_commands, const cppdx::SpriteAtlas &p_atlas,
			uint32_t p_width, uint32_t p_height, cppdx::Canvas &r_canvas, String &r_error);
	void end();

	~NativeGpuSession() { end(); }

private:
	RenderingDevice *device = nullptr;
	RID shader;
	RID pipeline;
};

} // namespace godot

#endif // PROC_CITY_NATIVE_GPU_H
