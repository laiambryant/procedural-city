#ifndef PROC_CITY_NATIVE_GPU_H
#define PROC_CITY_NATIVE_GPU_H

#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cppdisplacementx/canvas.h>
#include <cppdisplacementx/draw_command.h>
#include <cppdisplacementx/sprite_atlas.h>

namespace godot {

class RenderingDevice;

// Runs the engine's compute shader on a local RenderingDevice.
//
// A session owns its device for the whole bundle, so several seeds share one
// bring-up and one shader compile, and releases it on the thread that created
// it. That thread affinity is why the device is not cached across generations:
// a RenderingDevice must be torn down from its own thread, and the module
// unloads on the main one.
//
// begin() failing is a normal outcome, not an error: renderers without a
// RenderingDevice (the Compatibility renderer, web exports) simply have no GPU
// backend, and the caller falls back to the CPU one.
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
