#ifndef HIVE_GEN_CORE_H
#define HIVE_GEN_CORE_H

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <unordered_set>
#include <vector>

namespace godot {

class HiveGenCore : public RefCounted {
	GDCLASS(HiveGenCore, RefCounted)

	using CellSet = std::unordered_set<uint64_t>;

	static uint64_t _cell_key(const Vector2i &p_cell);
	static bool _occupied_has(const CellSet &p_occupied, const Vector2i &p_cell);

	Array _try_plan(const Ref<RandomNumberGenerator> &p_rng, int p_main_length,
			int p_branch_count, bool p_include_boss, const Array &p_pool_data) const;
	bool _walk_main_path(const Ref<RandomNumberGenerator> &p_rng, int p_main_length,
			std::vector<Vector2i> &r_path, CellSet &r_occupied, int &r_heading) const;
	Array _rooms_along_path(const std::vector<Vector2i> &p_path) const;
	bool _append_boss_room(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
			int p_heading, CellSet &r_occupied) const;
	void _append_branch_rooms(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
			int p_branch_count, CellSet &r_occupied) const;
	bool _assign_room_templates(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
			const Array &p_pool_data) const;
	static Dictionary _make_room(const Vector2i &p_cell, const String &p_kind, int p_order, int p_parent_order);
	int _pick_step(const Ref<RandomNumberGenerator> &p_rng, const Vector2i &p_from,
			int p_heading, const CellSet &p_occupied) const;
	int _free_side(const Ref<RandomNumberGenerator> &p_rng, const Vector2i &p_cell,
			int p_preferred, const CellSet &p_occupied) const;
	void _connect_rooms(Dictionary p_room_a, Dictionary p_room_b) const;
	int _pick_template(const Ref<RandomNumberGenerator> &p_rng, const Array &p_pool_data,
			const String &p_kind, int p_avoid_index) const;
	void _shuffle(const Ref<RandomNumberGenerator> &p_rng, Array &p_arr) const;

protected:
	static void _bind_methods();

public:
	Array plan_floor(int64_t p_seed, int p_main_length, int p_branch_count,
			bool p_include_boss, const Array &p_pool_data) const;

	HiveGenCore() {}
	~HiveGenCore() {}
};

} // namespace godot

#endif // HIVE_GEN_CORE_H
