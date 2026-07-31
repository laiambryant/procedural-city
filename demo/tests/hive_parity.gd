extends SceneTree
# Pins HiveGenCore's RNG stream. The planner must stay call-for-call identical
# to its GDScript counterpart, so the same seed has to keep producing the same
# floor forever; any change to the walk, the room order or the template picks
# shows up here as a different digest.
#
# Run: godot --headless --path demo --script tests/hive_parity.gd
# Pass PC_HIVE_DUMP=<path> to write the full plans out instead of digesting
# them, which is how a refactor is diffed against the previous build.

const SEEDS := [1, 7, 12345, -99, 2 ** 31]
const MAIN_LENGTHS := [3, 6, 11]
const BRANCH_COUNTS := [0, 3]

# Digest of every plan below, from the build that first pinned them.
const EXPECTED_DIGEST := "1a0243e1b8b53003cbdc3a29731a878c"


func pool_data() -> Array:
	return [
		{"kind": "start", "weight": 1.0, "name": "start_a"},
		{"kind": "combat", "weight": 2.0, "name": "combat_a"},
		{"kind": "combat", "weight": 1.0, "name": "combat_b"},
		{"kind": "combat", "weight": 0.5, "name": "combat_c"},
		{"kind": "shop", "weight": 1.0, "name": "shop_a"},
		{"kind": "reward", "weight": 1.0, "name": "reward_a"},
		{"kind": "boss", "weight": 1.0, "name": "boss_a"},
	]


func all_plans() -> Array:
	var core := HiveGenCore.new()
	var pool := pool_data()
	var out := []
	for s in SEEDS:
		for length in MAIN_LENGTHS:
			for branches in BRANCH_COUNTS:
				for boss in [true, false]:
					out.append({
						"seed": s, "length": length, "branches": branches, "boss": boss,
						"plan": core.plan_floor(s, length, branches, boss, pool),
					})
	return out


func _init() -> void:
	var plans := all_plans()
	var text := JSON.stringify(plans)

	var dump := OS.get_environment("PC_HIVE_DUMP")
	if not dump.is_empty():
		var f := FileAccess.open(dump, FileAccess.WRITE)
		f.store_string(text)
		f.close()
		print("hive_parity: wrote ", plans.size(), " plans to ", dump)
		quit(0)
		return

	var digest := text.md5_text()
	var non_empty := 0
	for entry in plans:
		if not entry["plan"].is_empty():
			non_empty += 1
	print("hive_parity: %d plans, %d non-empty, digest %s" % [plans.size(), non_empty, digest])
	if non_empty == 0:
		printerr("FAIL  every plan came back empty; the planner is not running")
		quit(1)
		return
	if digest != EXPECTED_DIGEST:
		printerr("FAIL  digest changed (expected %s)" % EXPECTED_DIGEST)
		printerr("      The planner's RNG stream moved. If that was intended, note it in the PR")
		printerr("      and update EXPECTED_DIGEST; otherwise the refactor broke determinism.")
		quit(1)
		return
	print("PASS  hive plans match the pinned RNG stream")
	quit(0)
