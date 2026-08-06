#include "core/proc_city_generator.h"

#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

static void add_trimesh_body(Node3D *p_parent, const Ref<Mesh> &p_mesh, int p_collision_layer) {
	if (p_mesh.is_null()) {
		return;
	}
	Ref<ConcavePolygonShape3D> shape = p_mesh->create_trimesh_shape();
	if (shape.is_null()) {
		return;
	}
	StaticBody3D *body = memnew(StaticBody3D);
	body->set_name("CollisionBody");
	body->set_collision_layer(p_collision_layer);
	body->set_collision_mask(0);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("CollisionShape");
	cs->set_shape(shape);
	body->add_child(cs);
	p_parent->add_child(body);
}

void ProcCityGenerator::_attach_trimesh_body(MeshInstance3D *p_mesh_instance) {
	add_trimesh_body(p_mesh_instance, p_mesh_instance->get_mesh(), collision_layer);
}

// MultiMesh instances have no merged mesh; build the equivalent extruded-blocks
// mesh purely as a collision source.
void ProcCityGenerator::_attach_multimesh_body(Node3D *p_container) {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> source = mesher->build_array_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height,
												 sample_filter, height_power, block_inset, 0, 0.0, 0.0,
												 clip_below_height);
	add_trimesh_body(p_container, source, collision_layer);
}

// A GridMap is its own collider: the shapes came from its MeshLibrary items, so
// only the layers are left to match what the StaticBody3D backends set.
static void set_gridmap_layers(GridMap *p_gridmap, int p_collision_layer) {
	p_gridmap->set_collision_layer((uint32_t)p_collision_layer);
	p_gridmap->set_collision_mask(0);
}

void ProcCityGenerator::_attach_collision(Node3D *p_container) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_container);
	if (mi != nullptr) {
		_attach_trimesh_body(mi);
		return;
	}
	if (Object::cast_to<MultiMeshInstance3D>(p_container) != nullptr) {
		_attach_multimesh_body(p_container);
		return;
	}
	GridMap *gridmap = Object::cast_to<GridMap>(p_container);
	if (gridmap != nullptr) {
		set_gridmap_layers(gridmap, collision_layer);
		return;
	}
	TypedArray<Node> kids = p_container->get_children();
	for (int i = 0; i < kids.size(); i++) {
		Node3D *child = Object::cast_to<Node3D>(kids[i]);
		if (child != nullptr && Object::cast_to<StaticBody3D>(child) == nullptr) {
			_attach_collision(child);
		}
	}
}
