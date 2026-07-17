# Determinism contract of the gpudisplacementx CLI as observed from the
# plugin: same seed => byte-identical maps across two runs. The CPU CLI with
# the same seed is expected to differ (independent stream by design).
#   godot --headless --path demo -s res://tests/gpu_parity.gd -- <gpu_cli> <out_dir>
extends SceneTree

const SEED := 424242

var failures := 0
var out_dir := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		push_error("usage: -- <gpu_cli> <out_dir>")
		quit(1)
		return
	out_dir = args[1]
	_run(args[0])


func _run(gpu_cli: String) -> void:
	await process_frame
	_check_two_run_parity(gpu_cli)
	_report_cpu_divergence(gpu_cli)
	_check_fast_mode_runs(gpu_cli)
	if failures > 0:
		push_error("gpu_parity: %d failure(s)" % failures)
		quit(1)
		return
	print("gpu_parity: OK")
	quit(0)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		_fail("check failed: " + label)


func _scene_params() -> GoplacementxParams:
	var p := GoplacementxParams.new()
	p.resolution = 512
	p.palette_preset = 3
	p.sprites_enabled = true
	p.sprite_packs = 15
	p.composition_modes = 65535
	p.seed = SEED
	p.randomize_seed = false
	return p


func _bundle(cli: String, label: String, p: GoplacementxParams) -> Dictionary:
	var runner := GoplacementxRunner.new()
	var config := runner.write_config(out_dir, p)
	var paths := {
		"height": out_dir.path_join(label + "_height.gdxraw"),
		"albedo": out_dir.path_join(label + "_albedo.gdxraw"),
		"normal": out_dir.path_join(label + "_normal.gdxraw"),
	}
	var emits := [
		{"mode": "grayscale", "seed": SEED, "path": paths["height"]},
		{"mode": "color", "seed": SEED, "path": paths["albedo"]},
		{"mode": "normal", "seed": SEED, "path": paths["normal"]},
	]
	var r: Dictionary = runner.run_bundle(cli, config, emits, p)
	return {"code": int(r.get("code", -1)), "output": str(r.get("output", "")), "paths": paths}


func _check_two_run_parity(gpu_cli: String) -> void:
	print("=== GPU same-seed parity (run A vs run B) ===")
	var a := _bundle(gpu_cli, "gpu_a", _scene_params())
	var b := _bundle(gpu_cli, "gpu_b", _scene_params())
	_check(a["code"] == 0, "run A exit 0 (got %d: %s)" % [a["code"], a["output"]])
	_check(b["code"] == 0, "run B exit 0 (got %d: %s)" % [b["code"], b["output"]])
	if a["code"] != 0 or b["code"] != 0:
		return
	for map_name in a["paths"]:
		var bytes_a := FileAccess.get_file_as_bytes(a["paths"][map_name])
		var bytes_b := FileAccess.get_file_as_bytes(b["paths"][map_name])
		_check(bytes_a.size() > 16 and bytes_a == bytes_b, "%s byte-identical (%d bytes)" % [map_name, bytes_a.size()])


func _report_cpu_divergence(gpu_cli: String) -> void:
	print("=== CPU CLI same seed (informational, divergence expected) ===")
	var cpu_runner := GoplacementxRunner.new()
	var cpu_cli := cpu_runner.find_binary("")
	if cpu_cli.is_empty():
		print("  skip: no bundled CPU CLI resolved")
		return
	var gpu := _bundle(gpu_cli, "div_gpu", _scene_params())
	var cpu := _bundle(cpu_cli, "div_cpu", _scene_params())
	if gpu["code"] != 0 or cpu["code"] != 0:
		print("  skip: one of the runs failed (gpu %d, cpu %d)" % [gpu["code"], cpu["code"]])
		return
	var same := FileAccess.get_file_as_bytes(gpu["paths"]["height"]) == FileAccess.get_file_as_bytes(cpu["paths"]["height"])
	print("  height GPU==CPU: %s (different streams by design)" % str(same))


func _check_fast_mode_runs(gpu_cli: String) -> void:
	print("=== GPU --fast smoke ===")
	var p := _scene_params()
	p.fast = true
	var r := _bundle(gpu_cli, "gpu_fast", p)
	_check(r["code"] == 0, "--fast exit 0 (got %d: %s)" % [r["code"], r["output"]])
