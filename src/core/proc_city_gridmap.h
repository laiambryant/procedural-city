#ifndef PROC_CITY_GRIDMAP_H
#define PROC_CITY_GRIDMAP_H

#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh_library.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "meshing/gridmap_plan.h"

namespace godot {

// Item id of the single block in a generated MeshLibrary. Also the default
// gridmap_item_id, so a hand-made library only needs one item to work.
inline constexpr int GRIDMAP_BLOCK_ITEM = 0;

// build_block_library makes the one-item MeshLibrary the GridMap backend stamps
// when the user supplies none. Shapes are baked in only when collision is
// wanted: a GridMap collides through its item shapes, so a shape-free item is
// how the backend says "no collision".
Ref<MeshLibrary> build_block_library(const Vector3 &p_cell_size, double p_inset, bool p_with_collision);

// build_gridmap_node stamps p_item over every cell in p_plan.
GridMap *build_gridmap_node(const GridMapPlan &p_plan, const Ref<MeshLibrary> &p_library, int p_item);

// apply_library_material retints every primitive item in p_library. A GridMap
// takes no material override — its look comes from the meshes in its
// MeshLibrary — so this is how the material stage reaches a GridMap city.
void apply_library_material(const Ref<MeshLibrary> &p_library, const Ref<Material> &p_material);

} // namespace godot

#endif // PROC_CITY_GRIDMAP_H
