#ifndef HIVE_DIRECTIONS_H
#define HIVE_DIRECTIONS_H

#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

// Grid directions, shared by the walk and the room decoration. cell.y + 1 is
// north, which the generator maps to world -Z.
inline constexpr int DIR_N = 0;
inline constexpr int DIR_COUNT = 4;
inline const Vector2i DIR_VECTORS[DIR_COUNT] = {
	Vector2i(0, 1), // N
	Vector2i(1, 0), // E
	Vector2i(0, -1), // S
	Vector2i(-1, 0), // W
};

} // namespace godot

#endif // HIVE_DIRECTIONS_H
