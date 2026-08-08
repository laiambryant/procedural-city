#ifndef HIVE_DIRECTIONS_H
#define HIVE_DIRECTIONS_H

#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

inline constexpr int DIR_N = 0;
inline constexpr int DIR_E = 1;
inline constexpr int DIR_S = 2;
inline constexpr int DIR_W = 3;
inline constexpr int DIR_COUNT = 4;
inline const Vector2i DIR_VECTORS[DIR_COUNT] = {
	Vector2i(0, 1),
	Vector2i(1, 0),
	Vector2i(0, -1),
	Vector2i(-1, 0),
};

} // namespace godot

#endif // HIVE_DIRECTIONS_H
