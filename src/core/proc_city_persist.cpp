#include "core/proc_city_generator.h"

#include "core/proc_city_log.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <iterator>

using namespace godot;

struct ExternalSlot {
	const char *class_name;
	const char *property;
	const char *suffix;
};

static const ExternalSlot EXTERNAL_SLOTS[] = {
	{ "GeometryInstance3D", "material_override", "material" },
	{ "MeshInstance3D", "mesh", "mesh" },
	{ "MultiMeshInstance3D", "multimesh", "multimesh" },
	{ "CollisionShape3D", "shape", "shape" },
	{ "OccluderInstance3D", "occluder", "occluder" },
	{ "GridMap", "mesh_library", "meshlib" },
};

static bool ensure_directory(const String &p_dir) {
	if (DirAccess::dir_exists_absolute(p_dir)) {
		return true;
	}
	return DirAccess::make_dir_recursive_absolute(p_dir) == OK;
}

static String slot_path(const String &p_dir, const String &p_owner_name, const char *p_suffix, int p_index) {
	return p_dir.path_join(p_owner_name + String("_") + String(p_suffix) + String("_") + String::num_int64(p_index) + String(".res"));
}

static int64_t written_file_size(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	return file.is_valid() ? (int64_t)file->get_length() : 0;
}

static bool store_externally(const Ref<Resource> &p_resource, const String &p_path) {
	StageTimer save_timer("  save " + p_path.get_file());
	if (ResourceSaver::get_singleton()->save(p_resource, p_path, ResourceSaver::FLAG_COMPRESS) != OK) {
		return false;
	}
	p_resource->take_over_path(p_path);
	save_timer.report(format_byte_size(written_file_size(p_path)));
	return true;
}

static bool needs_externalizing(const Ref<Resource> &p_resource) {
	return p_resource.is_valid() && p_resource->get_path().is_empty();
}

static void externalize_material_textures(const Ref<Resource> &p_resource, const String &p_dir, const String &p_owner_name, int &r_index, int &r_failures) {
	Ref<BaseMaterial3D> material = p_resource;
	if (material.is_null()) {
		return;
	}
	for (int param = 0; param < BaseMaterial3D::TEXTURE_MAX; param++) {
		Ref<Resource> texture = material->get_texture((BaseMaterial3D::TextureParam)param);
		if (!needs_externalizing(texture)) {
			continue;
		}
		if (!store_externally(texture, slot_path(p_dir, p_owner_name, "texture", r_index))) {
			r_failures++;
			continue;
		}
		r_index++;
	}
}

static void externalize_node(Node *p_node, const String &p_dir, const String &p_owner_name, int &r_index, int &r_failures) {
	for (const ExternalSlot &slot : EXTERNAL_SLOTS) {
		if (!p_node->is_class(slot.class_name)) {
			continue;
		}
		Ref<Resource> resource = p_node->get(slot.property);
		if (!needs_externalizing(resource)) {
			continue;
		}
		externalize_material_textures(resource, p_dir, p_owner_name, r_index, r_failures);
		if (!store_externally(resource, slot_path(p_dir, p_owner_name, slot.suffix, r_index))) {
			r_failures++;
			continue;
		}
		r_index++;
	}
}

static void externalize_tree(Node *p_node, const String &p_dir, const String &p_owner_name, int &r_index, int &r_failures) {
	if (p_node == nullptr) {
		return;
	}
	externalize_node(p_node, p_dir, p_owner_name, r_index, r_failures);
	TypedArray<Node> children = p_node->get_children();
	for (int i = 0; i < children.size(); i++) {
		externalize_tree(Object::cast_to<Node>(children[i]), p_dir, p_owner_name, r_index, r_failures);
	}
}

void ProcCityGenerator::externalize_generated_resources() {
	Node *container = _get_generated();
	if (external_resource_dir.is_empty() || container == nullptr) {
		return;
	}
	if (!ensure_directory(external_resource_dir)) {
		UtilityFunctions::push_warning("[ProcCity] Could not create " + external_resource_dir +
				" - generated meshes will be embedded in the scene text, which makes saving slow.");
		return;
	}

	int index = 0;
	int failures = 0;
	StageTimer externalize_timer("externalize");
	externalize_tree(container, external_resource_dir, get_name(), index, failures);
	if (index > 0) {
		externalize_timer.report(String::num_int64(index) + " resource(s) to " + external_resource_dir);
	}
	if (failures > 0) {
		UtilityFunctions::push_warning("[ProcCity] " + String::num_int64(failures) +
				" generated resource(s) could not be written to " + external_resource_dir + " and stay embedded in the scene.");
	}
}
