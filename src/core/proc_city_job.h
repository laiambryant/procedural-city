#ifndef PROC_CITY_JOB_H
#define PROC_CITY_JOB_H

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

Array plan_bundle_emits(const String &p_dir, uint64_t p_base_seed, bool p_want_height, bool p_want_material,
		int p_texture_mode, const String &p_ext, Dictionary &r_result);

bool load_result_images(Dictionary &r_result, const Dictionary &p_images, int p_material_max_size,
		bool p_mipmaps, String &r_fail_stage, String &r_fail_message);

void prepare_material_images(Dictionary &r_result, int p_max_size, bool p_mipmaps);

bool build_worker_mesh(const Dictionary &p_job, Dictionary &r_result);

} // namespace godot

#endif // PROC_CITY_JOB_H
