#ifndef PROC_CITY_GRIDMAP_H
#define PROC_CITY_GRIDMAP_H

#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh_library.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "meshing/gridmap_plan.h"

namespace godot {

inline constexpr int GRIDMAP_BLOCK_ITEM = 0;

Ref<MeshLibrary> build_block_library(const Vector3 &p_cell_size, double p_inset, bool p_with_collision);

GridMap *build_gridmap_node(const GridMapPlan &p_plan, const Ref<MeshLibrary> &p_library, int p_item);

void apply_library_material(const Ref<MeshLibrary> &p_library, const Ref<Material> &p_material);

} // namespace godot

#endif // PROC_CITY_GRIDMAP_H
