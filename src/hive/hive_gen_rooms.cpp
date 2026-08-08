#include "hive/hive_gen_core.h"

#include "hive/hive_directions.h"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

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

static String room_kind_for_order(int p_order, int p_shop_order, int p_last_order) {
	if (p_order == 0) {
		return "start";
	}
	if (p_order == p_shop_order) {
		return "shop";
	}
	if (p_order == p_last_order) {
		return "reward";
	}
	return "combat";
}

Array HiveGenCore::_rooms_along_path(const std::vector<Vector2i> &p_path) const {
	const int shop_order = (int)std::floor((double)p_path.size() / 2.0);
	const int last_order = (int)p_path.size() - 1;
	Array rooms;
	for (int order = 0; order < (int)p_path.size(); order++) {
		const String kind = room_kind_for_order(order, shop_order, last_order);
		rooms.push_back(_make_room(p_path[(size_t)order], kind, order, order - 1));
	}
	for (int order = 1; order < (int)p_path.size(); order++) {
		_connect_rooms(rooms[order - 1], rooms[order]);
	}
	return rooms;
}

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

static Array combat_rooms(const Array &p_rooms) {
	Array hosts;
	for (int i = 0; i < p_rooms.size(); i++) {
		Dictionary room = p_rooms[i];
		if (String(room["kind"]) == String("combat")) {
			hosts.push_back(room);
		}
	}
	return hosts;
}

void HiveGenCore::_append_branch_rooms(const Ref<RandomNumberGenerator> &p_rng, Array &r_rooms,
		int p_branch_count, CellSet &r_occupied) const {
	const String branch_kinds[2] = { String("branch_treasure"), String("branch_altar") };
	Array hosts = combat_rooms(r_rooms);
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

void HiveGenCore::_connect_rooms(Dictionary p_room_a, Dictionary p_room_b) const {
	const Vector2i delta = Vector2i(p_room_b["cell"]) - Vector2i(p_room_a["cell"]);
	int dir = -1;
	for (int i = 0; i < DIR_COUNT; i++) {
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

void HiveGenCore::_shuffle(const Ref<RandomNumberGenerator> &p_rng, Array &p_arr) const {
	for (int i = (int)p_arr.size() - 1; i > 0; i--) {
		const int j = p_rng->randi_range(0, i);
		Variant tmp = p_arr[i];
		p_arr[i] = p_arr[j];
		p_arr[j] = tmp;
	}
}
