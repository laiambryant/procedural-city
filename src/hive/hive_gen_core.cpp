#include "hive/hive_gen_core.h"

#include "hive/hive_directions.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;

static const int MAX_PLAN_ATTEMPTS = 20;
// A floor needs at least start, one middle room and reward to be playable.
static const int MIN_MAIN_PATH_LENGTH = 3;
// Step weights of the self-avoiding walk: forward twice as likely as either
// turn, so the path keeps snaking north instead of curling into itself. The
// candidates are forward, left and right — never back onto itself.
static const int STEP_CANDIDATES = 3;
static const double STEP_WEIGHT_FORWARD = 0.5;
static const double STEP_WEIGHT_TURN = 0.25;

void HiveGenCore::_bind_methods() {
	ClassDB::bind_method(
			D_METHOD("plan_floor", "seed", "main_length", "branch_count", "include_boss", "pool_data"),
			&HiveGenCore::plan_floor);
}

// Two int32 grid coordinates packed into one key: x takes the high half, y the
// low, so every cell maps to a distinct value with no hashing.
static constexpr int CELL_KEY_X_SHIFT = 32;

uint64_t HiveGenCore::_cell_key(const Vector2i &p_cell) {
	return ((uint64_t)(uint32_t)p_cell.x << CELL_KEY_X_SHIFT) | (uint64_t)(uint32_t)p_cell.y;
}

bool HiveGenCore::_occupied_has(const CellSet &p_occupied, const Vector2i &p_cell) {
	return p_occupied.find(_cell_key(p_cell)) != p_occupied.end();
}

// retry_seed mirrors the GDScript reseed exactly — hash([seed_value, attempt])
// — so a retried plan lands on the same RNG stream the original does. Frozen:
// any other mixing would regenerate every existing floor.
static uint64_t retry_seed(int64_t p_seed, int p_attempt) {
	Array key;
	key.push_back(p_seed);
	key.push_back(p_attempt);
	return (uint64_t)(int64_t)UtilityFunctions::hash(key);
}

static Ref<RandomNumberGenerator> rng_for_attempt(int64_t p_seed, int p_attempt) {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->set_seed(p_attempt == 0 ? (uint64_t)p_seed : retry_seed(p_seed, p_attempt));
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

// DIR_VECTORS runs clockwise, so the neighbouring index one step back is a left
// turn and one step on is a right turn.
static int turn_left(int p_heading) {
	return (p_heading + DIR_COUNT - 1) % DIR_COUNT;
}

static int turn_right(int p_heading) {
	return (p_heading + 1) % DIR_COUNT;
}

// Candidate step weighted forward 0.5 / left 0.25 / right 0.25 (never back),
// restricted to free cells. Returns -1 when boxed in.
int HiveGenCore::_pick_step(const Ref<RandomNumberGenerator> &p_rng, const Vector2i &p_from,
							int p_heading, const CellSet &p_occupied) const {
	const int dirs[STEP_CANDIDATES] = { p_heading, turn_left(p_heading), turn_right(p_heading) };
	const double weights[STEP_CANDIDATES] = { STEP_WEIGHT_FORWARD, STEP_WEIGHT_TURN, STEP_WEIGHT_TURN };
	int usable_dirs[STEP_CANDIDATES];
	double usable_weights[STEP_CANDIDATES];
	int usable_count = 0;
	double total = 0.0;
	for (int i = 0; i < STEP_CANDIDATES; i++) {
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
	int free[DIR_COUNT];
	int free_count = 0;
	for (int dir = 0; dir < DIR_COUNT; dir++) {
		if (!_occupied_has(p_occupied, p_cell + DIR_VECTORS[dir])) {
			free[free_count++] = dir;
		}
	}
	if (free_count == 0) {
		return -1;
	}
	return free[p_rng->randi_range(0, free_count - 1)];
}
