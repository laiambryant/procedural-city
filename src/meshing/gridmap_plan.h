#ifndef GRIDMAP_PLAN_H
#define GRIDMAP_PLAN_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <vector>

namespace godot {

inline constexpr int64_t GRIDMAP_CELL_WARNING_THRESHOLD = 65536;
inline constexpr int64_t GRIDMAP_MAX_CELLS = 1048576;

struct GridMapPlan {
	Vector3 cell_size = Vector3(1, 1, 1);
	Vector3 origin;
	std::vector<Vector3i> cells;
	int columns = 0;
	int rows = 0;
	int levels = 0;
};

bool build_gridmap_plan(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter, double p_height_power,
		double p_level_height, bool p_fill_columns, GridMapPlan &r_plan, String &r_error);

} // namespace godot

#endif // GRIDMAP_PLAN_H
