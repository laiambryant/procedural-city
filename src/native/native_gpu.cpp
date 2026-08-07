#include "native/native_gpu.h"

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cppdisplacementx/composite_glsl.h>
#include <cppdisplacementx/tile_bins.h>

#include <cstring>
#include <vector>

using namespace godot;

static constexpr uint32_t WORKGROUP_SIDE = 16;
static constexpr uint32_t PUSH_CONSTANT_WORDS = 4;

namespace {

struct BufferSet {
	RID canvas;
	RID commands;
	RID atlas;
	RID sprite_meta;
	RID bins;
	RID tile_ranges;
	RID uniform_set;

	void free_all(RenderingDevice *p_device) {
		for (const RID &rid : { uniform_set, canvas, commands, atlas, sprite_meta, bins, tile_ranges }) {
			if (rid.is_valid()) {
				p_device->free_rid(rid);
			}
		}
	}
};

} // namespace

static PackedByteArray bytes_from_words(const std::vector<uint32_t> &p_words) {
	PackedByteArray bytes;
	if (p_words.empty()) {
		bytes.resize(sizeof(uint32_t));
		bytes.fill(0);
		return bytes;
	}
	bytes.resize((int64_t)(p_words.size() * sizeof(uint32_t)));
	memcpy(bytes.ptrw(), p_words.data(), p_words.size() * sizeof(uint32_t));
	return bytes;
}

static PackedByteArray bytes_from_commands(const cppdx::CommandList &p_commands) {
	PackedByteArray bytes;
	const size_t size = p_commands.size() * sizeof(cppdx::DrawCommand);
	bytes.resize((int64_t)(size > 0 ? size : sizeof(cppdx::DrawCommand)));
	bytes.fill(0);
	if (size > 0) {
		memcpy(bytes.ptrw(), p_commands.data(), size);
	}
	return bytes;
}

static Ref<RDUniform> storage_uniform(int p_binding, const RID &p_buffer) {
	Ref<RDUniform> uniform;
	uniform.instantiate();
	uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	uniform->set_binding(p_binding);
	uniform->add_id(p_buffer);
	return uniform;
}

static RID create_storage(RenderingDevice *p_device, const PackedByteArray &p_data) {
	return p_device->storage_buffer_create((uint32_t)p_data.size(), p_data);
}

static PackedByteArray push_constant_bytes(uint32_t p_width, uint32_t p_height, uint32_t p_tile_cols) {
	const uint32_t values[PUSH_CONSTANT_WORDS] = { p_width, p_height, p_tile_cols, 0u };
	PackedByteArray bytes;
	bytes.resize(sizeof(values));
	memcpy(bytes.ptrw(), values, sizeof(values));
	return bytes;
}

static uint32_t workgroups_for(uint32_t p_extent) {
	return (p_extent + WORKGROUP_SIDE - 1) / WORKGROUP_SIDE;
}

bool NativeGpuSession::begin(String &r_error) {
	if (pipeline.is_valid()) {
		return true;
	}
	RenderingServer *server = RenderingServer::get_singleton();
	if (server == nullptr) {
		r_error = "the rendering server is not available";
		return false;
	}
	device = server->create_local_rendering_device();
	if (device == nullptr) {
		r_error = "the active renderer provides no RenderingDevice (Compatibility renderer or web export)";
		return false;
	}

	Ref<RDShaderSource> source;
	source.instantiate();
	source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(cppdx::composite_compute_glsl()));

	Ref<RDShaderSPIRV> spirv = device->shader_compile_spirv_from_source(source);
	const String compile_error = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
	if (!compile_error.is_empty()) {
		r_error = "compositor shader failed to compile: " + compile_error;
		end();
		return false;
	}

	shader = device->shader_create_from_spirv(spirv);
	pipeline = shader.is_valid() ? device->compute_pipeline_create(shader) : RID();
	if (!pipeline.is_valid()) {
		r_error = "compositor pipeline could not be created";
		end();
		return false;
	}
	return true;
}

bool NativeGpuSession::composite(const cppdx::CommandList &p_commands, const cppdx::SpriteAtlas &p_atlas,
		uint32_t p_width, uint32_t p_height, cppdx::Canvas &r_canvas, String &r_error) {
	if (!pipeline.is_valid()) {
		r_error = "no compositor pipeline";
		return false;
	}

	const cppdx::TileBins bins = cppdx::bin_commands(p_commands, p_width, p_height);

	PackedByteArray canvas_bytes;
	canvas_bytes.resize((int64_t)r_canvas.byte_size());
	canvas_bytes.fill(0);

	BufferSet buffers;
	buffers.canvas = create_storage(device, canvas_bytes);
	buffers.commands = create_storage(device, bytes_from_commands(p_commands));
	buffers.atlas = create_storage(device, bytes_from_words(p_atlas.pixel_data()));
	buffers.sprite_meta = create_storage(device, bytes_from_words(p_atlas.meta_data()));
	buffers.bins = create_storage(device, bytes_from_words(bins.indices));
	buffers.tile_ranges = create_storage(device, bytes_from_words(bins.ranges));

	TypedArray<RDUniform> uniforms;
	uniforms.push_back(storage_uniform(0, buffers.canvas));
	uniforms.push_back(storage_uniform(1, buffers.commands));
	uniforms.push_back(storage_uniform(2, buffers.atlas));
	uniforms.push_back(storage_uniform(3, buffers.sprite_meta));
	uniforms.push_back(storage_uniform(4, buffers.bins));
	uniforms.push_back(storage_uniform(5, buffers.tile_ranges));
	buffers.uniform_set = device->uniform_set_create(uniforms, shader, 0);
	if (!buffers.uniform_set.is_valid()) {
		buffers.free_all(device);
		r_error = "compositor uniform set could not be created";
		return false;
	}

	const PackedByteArray push_constant = push_constant_bytes(p_width, p_height, bins.tile_cols);
	const int64_t list = device->compute_list_begin();
	device->compute_list_bind_compute_pipeline(list, pipeline);
	device->compute_list_bind_uniform_set(list, buffers.uniform_set, 0);
	device->compute_list_set_push_constant(list, push_constant, (uint32_t)push_constant.size());
	device->compute_list_dispatch(list, workgroups_for(p_width), workgroups_for(p_height), 1);
	device->compute_list_end();

	device->submit();
	device->sync();

	const PackedByteArray result = device->buffer_get_data(buffers.canvas);
	buffers.free_all(device);

	if ((size_t)result.size() != r_canvas.byte_size()) {
		r_error = "compositor readback returned " + String::num_int64(result.size()) + " bytes";
		return false;
	}
	memcpy(r_canvas.pixels.data(), result.ptr(), r_canvas.byte_size());
	return true;
}

void NativeGpuSession::end() {
	if (device == nullptr) {
		return;
	}
	if (pipeline.is_valid()) {
		device->free_rid(pipeline);
		pipeline = RID();
	}
	if (shader.is_valid()) {
		device->free_rid(shader);
		shader = RID();
	}
	memdelete(device);
	device = nullptr;
}
