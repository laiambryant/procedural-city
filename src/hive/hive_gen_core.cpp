#include "hive/hive_gen_core.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;

// Grid directions. cell.y + 1 is north, which the generator maps to world -Z.
static const int DIR_N = 0;
static const Vector2i DIR_VECTORS[4] = {
	Vector2i(0, 1),	 // N
	Vector2i(1, 0),	 // E
	Vector2i(0, -1), // S
	Vector2i(-1, 0), // W
};
static const int MAX_PLAN_ATTEMPTS = 20;
// A floor needs at least start, one middle room and reward to be playable.
static const int MIN_MAIN_PATH_LENGTH = 3;
// Step weights of the self-avoiding walk: forward twice as likely as either
// turn, so the path keeps snaking north instead of curling into itself.
static const double STEP_WEIGHT_FORWARD = 0.5;
static const double STEP_WEIGHT_TURN = 0.25;

void HiveGenCore::_bind_methods() {
	ClassDB::bind_method(
			D_METHOD("plan_floor", "seed", "main_length", "branch_count", "include_boss", "pool_data"),
			&HiveGenCore::plan_floor);
}

uint64_t HiveGenCore::_cell_key(const Vector2i &p_cell) {
	return ((uint64_t)(uint32_t)p_cell.x << 32) | (uint64_t)(uint32_t)p_cell.y;
}

bool HiveGenCore::_occupied_has(const CellSet &p_occupied, const Vector2i &p_cell) {
	return p_occupied.find(_cell_key(p_cell)) != p_occupied.end();
}

static Ref<RandomNumberGenerator> rng_for_attempt(int64_t p_seed, int p_attempt) {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (p_attempt == 0) {
		rng->set_seed((uint64_t)p_seed);
		return rng;
	}
	// Mirrors the GDScript retry reseed: hash([seed_value, attempt]).
	Array key;
	key.push_back(p_seed);
	key.push_back(p_attempt);
	rng->set_seed((uint64_t)(int64_t)UtilityFunctions::hash(key));
	return rng;
}

Array HiveGenCore::plan_floor(int64_t p_seed, int p_main_length, int p_branch_count,
							  bool p_include_boss, const Array &p_pool_data) const {
	p_main_length = MAX(MIN_MAIN_PATH_LENGTH, p_main_length);
	for (int attempt = 0; attempt < MAX_PLAN_ATTEMPTS; attempt++) {
		Array plan = _try_plan(rng_for_attempt(p_seed, attempt), p_main_length, p_branch_count, p_include_boss, p_pool_data);
		if (!plan.is_empty()) {
			return plan;
		}
	}
	return Array();
}

Array HiveGenCore::_try_plan(const Ref<RandomNumberGenerator> &p_rng, int p_main_length,
							 int p_branch_count, bool p_include_boss, const Array &p_pool_data) const {
	std::vector<Vector2i> path;
	CellSet occupied;
	int heading = DIR_N;
	if (!_walk_main_path(p_rng, p_main_length, path, occupied, heading)) {
		return Array();
	}

	Array rooms = _rooms_along_path(path);

	if (p_include_boss && !_append_boss_room(p_rng, rooms, heading, occupied)) {
		return Array();
	}

	_append_branch_rooms(p_rng, rooms, p_branch_count, occupied);

	if (!_assign_room_templates(p_rng, rooms, p_pool_data)) {
		return Array();
	}
	return rooms;
}

// A self-avoiding walk, forward-biased so it keeps snaking north instead of
// curling into itself. Fails when the walk traps itself; the caller retries
// with a fresh seed.
bool HiveGenCore::_walk_main_path(const Ref<RandomNumberGenerator> &p_rng, int p_main_length,
								  std::vector<Vector2i> &r_path, CellSet &r_occupied, int &r_heading) const {
	r_path.push_back(Vector2i(0, 0));
	r_occupied.insert(_cell_key(Vector2i(0, 0)));
	while ((int)r_path.size() < p_main_length) {
		const int dir = _pick_step(p_rng, r_path.back(), r_heading, r_occupied);
		if (dir < 0) {
			return false;
		}
		r_heading = dir;
		const Vector2i next = r_path.back() + DIR_VECTORS[dir];
		r_occupied.insert(_cell_key(next));
		r_path.push_back(next);
	}
	return true;
}

Dictionary HiveGenCore::_make_room(const Vector2i &p_cell, const String &p_kind, int p_order, int p_parent_order) {
	Dictionary room;
	room["cell"] = p_cell;
	room["pool_index"] = -1;
	room["connections"] = 0;
	room["kind"] = p_kind;
	room["order"] = p_order;
	room["parent_order"] = p_parent_order;
	return room;
}

// Rooms in path order: start, then combat with one shop at the midpoint, and
// the reward at the end, each connected to its predecessor.
Array HiveGenCore::_rooms_along_path(const std::vector<Vector2i> &p_path) const {
	const int shop_order = (int)std::floor((double)p_path.size() / 2.0);
	Array rooms;
	for (int order = 0; order < (int)p_path.size(); order++) {
		String kind = "combat";
		if (order == 0) {
			kind = "start";
		} else if (order == shop_order) {
			kind = "shop";
		} else if (order == (int)p_path.size() - 1) {
			kind = "reward";
		}
		rooms.push_back(_make_room(p_path[(size_t)order], kind, order, order - 1));
	}
	for (int order = 1; order < (int)p_path.size(); order++) {
		_connect_rooms(rooms[order - 1], rooms[order]);
	}
	return rooms;
}

// The boss room takes a free neighbour of the reward cell, preferring the
// walk's forward side. Fails when the reward cell is fully surrounded.
bool HiveGenCore::_append_boss_room(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
									int p_heading, CellSet &r_occupied) const {
	Dictionary reward = r_rooms[r_rooms.size() - 1];
	const int boss_dir = _free_side(p_rng, reward["cell"], p_heading, r_occupied);
	if (boss_dir < 0) {
		return false;
	}
	const Vector2i boss_cell = Vector2i(reward["cell"]) + DIR_VECTORS[boss_dir];
	r_occupied.insert(_cell_key(boss_cell));
	Dictionary boss = _make_room(boss_cell, "boss", (int)r_rooms.size(), (int)reward["order"]);
	_connect_rooms(reward, boss);
	r_rooms.push_back(boss);
	return true;
}

// Branches hang off shuffled mid-path combat cells. Missing space is tolerated
// (a floor without branches is valid, just less generous).
void HiveGenCore::_append_branch_rooms(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
									   int p_branch_count, CellSet &r_occupied) const {
	const String branch_kinds[2] = { String("branch_treasure"), String("branch_altar") };
	Array hosts;
	for (int i = 0; i < r_rooms.size(); i++) {
		Dictionary room = r_rooms[i];
		if (String(room["kind"]) == String("combat")) {
			hosts.push_back(room);
		}
	}
	_shuffle(p_rng, hosts);
	int placed_branches = 0;
	for (int i = 0; i < hosts.size(); i++) {
		if (placed_branches >= p_branch_count) {
			break;
		}
		Dictionary host = hosts[i];
		const int side = _free_side(p_rng, host["cell"], -1, r_occupied);
		if (side < 0) {
			continue;
		}
		const Vector2i branch_cell = Vector2i(host["cell"]) + DIR_VECTORS[side];
		r_occupied.insert(_cell_key(branch_cell));
		Dictionary branch = _make_room(branch_cell, branch_kinds[placed_branches % 2], (int)r_rooms.size(), (int)host["order"]);
		_connect_rooms(host, branch);
		r_rooms.push_back(branch);
		placed_branches += 1;
	}
}

// Combat rooms avoid repeating the previous template pick; a start room with
// no dedicated template falls back to the combat pool. Fails when a required
// role has no usable template at all.
bool HiveGenCore::_assign_room_templates(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
										 const Array &p_pool_data) const {
	int previous_combat = -1;
	for (int i = 0; i < r_rooms.size(); i++) {
		Dictionary room = r_rooms[i];
		const String kind = room["kind"];
		const int avoid = (kind == String("combat")) ? previous_combat : -1;
		int idx = _pick_template(p_rng, p_pool_data, kind, avoid);
		if (idx < 0 && kind == String("start")) {
			idx = _pick_template(p_rng, p_pool_data, "combat", -1);
		}
		if (idx < 0) {
			return false;
		}
		room["pool_index"] = idx;
		if (kind == String("combat")) {
			previous_combat = idx;
		}
	}
	return true;
}

// Candidate step weighted forward 0.5 / left 0.25 / right 0.25 (never back),
// restricted to free cells. Returns -1 when boxed in.
int HiveGenCore::_pick_step(const Ref<RandomNumberGenerator> &p_rng, const Vector2i &p_from,
							int p_heading, const CellSet &p_occupied) const {
	const int dirs[3] = { p_heading, (p_heading + 3) % 4, (p_heading + 1) % 4 };
	const double weights[3] = { STEP_WEIGHT_FORWARD, STEP_WEIGHT_TURN, STEP_WEIGHT_TURN };
	int usable_dirs[3];
	double usable_weights[3];
	int usable_count = 0;
	double total = 0.0;
	for (int i = 0; i < 3; i++) {
		if (!_occupied_has(p_occupied, p_from + DIR_VECTORS[dirs[i]])) {
			usable_dirs[usable_count] = dirs[i];
			usable_weights[usable_count] = weights[i];
			usable_count++;
			total += weights[i];
		}
	}
	if (usable_count == 0) {
		return -1;
	}
	double pick = (double)p_rng->randf() * total;
	for (int i = 0; i < usable_count; i++) {
		pick -= usable_weights[i];
		if (pick <= 0.0) {
			return usable_dirs[i];
		}
	}
	return usable_dirs[usable_count - 1];
}

// A free side of the cell; `preferred` (when >= 0) wins if available,
// otherwise a seeded pick among the free sides. -1 when fully surrounded.
int HiveGenCore::_free_side(const Ref<RandomNumberGenerator> &p_rng, const Vector2i &p_cell,
							int p_preferred, const CellSet &p_occupied) const {
	if (p_preferred >= 0 && !_occupied_has(p_occupied, p_cell + DIR_VECTORS[p_preferred])) {
		return p_preferred;
	}
	int free[4];
	int free_count = 0;
	for (int dir = 0; dir < 4; dir++) {
		if (!_occupied_has(p_occupied, p_cell + DIR_VECTORS[dir])) {
			free[free_count++] = dir;
		}
	}
	if (free_count == 0) {
		return -1;
	}
	return free[p_rng->randi_range(0, free_count - 1)];
}

// Sets the doorway bit on both rooms of an adjacent pair. Dictionaries are
// shared references, so mutating the local copy updates the plan in place
// (matching the GDScript pass-by-reference semantics).
void HiveGenCore::_connect_rooms(Dictionary p_room_a, Dictionary p_room_b) const {
	const Vector2i delta = Vector2i(p_room_b["cell"]) - Vector2i(p_room_a["cell"]);
	int dir = -1;
	for (int i = 0; i < 4; i++) {
		if (DIR_VECTORS[i] == delta) {
			dir = i;
			break;
		}
	}
	if (dir < 0) {
		return;
	}
	p_room_a["connections"] = (int)p_room_a["connections"] | (1 << dir);
	p_room_b["connections"] = (int)p_room_b["connections"] | (1 << ((dir + 2) % 4));
}

// Weighted template pick among pool entries of the given kind, avoiding
// `avoid_index` when alternatives exist. -1 when the kind has no usable entry.
int HiveGenCore::_pick_template(const Ref<RandomNumberGenerator> &p_rng, const Array &p_pool_data,
								const String &p_kind, int p_avoid_index) const {
	std::vector<int> candidates;
	for (int i = 0; i < p_pool_data.size(); i++) {
		Dictionary entry = p_pool_data[i];
		if (String(entry.get("kind", "combat")) != p_kind) {
			continue;
		}
		if ((double)entry.get("weight", 0.0) <= 0.0) {
			continue;
		}
		candidates.push_back(i);
	}
	if (candidates.empty()) {
		return -1;
	}
	std::vector<int> usable;
	for (int idx : candidates) {
		if (idx != p_avoid_index) {
			usable.push_back(idx);
		}
	}
	if (usable.empty()) {
		usable = candidates;
	}
	double total = 0.0;
	for (int idx : usable) {
		Dictionary entry = p_pool_data[idx];
		total += (double)entry.get("weight", 0.0);
	}
	double pick = (double)p_rng->randf() * total;
	for (int idx : usable) {
		Dictionary entry = p_pool_data[idx];
		pick -= (double)entry.get("weight", 0.0);
		if (pick <= 0.0) {
			return idx;
		}
	}
	return usable.back();
}

// Seeded Fisher-Yates so plans stay deterministic.
void HiveGenCore::_shuffle(const Ref<RandomNumberGenerator> &p_rng, Array &p_arr) const {
	for (int i = (int)p_arr.size() - 1; i > 0; i--) {
		const int j = p_rng->randi_range(0, i);
		Variant tmp = p_arr[i];
		p_arr[i] = p_arr[j];
		p_arr[j] = tmp;
	}
}
