# The gRPC transport must be pixel-identical to the one-shot CLI+file path and
# must never touch the filesystem for config: generate_all() (CPU and GPU)
# should not create a proc_city_*_config.json in output_dir, while the
# reference CLI-subprocess call explicitly does.
#   godot --headless --path demo -s res://tests/grpc_parity.gd -- <cpu_cli> <gpu_cli> <out_dir>
extends SceneTree

const SEED := 424242

var failures := 0
var out_dir := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		push_error("usage: -- <cpu_cli> <gpu_cli> <out_dir>")
		quit(1)
		return
	out_dir = args[2]
	DirAccess.make_dir_recursive_absolute(out_dir)
	_run(args[0], args[1])


func _run(cpu_cli: String, gpu_cli: String) -> void:
	await process_frame
	await _check_mode(ProcCityGenerator.GEN_CPU_LEGACY, cpu_cli, "CPU")
	await _check_mode(ProcCityGenerator.GEN_GPU_LEGACY, gpu_cli, "GPU")
	if failures > 0:
		push_error("grpc_parity: %d failure(s)" % failures)
		quit(1)
		return
	print("grpc_parity: OK")
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
	p.resolution = 256
	p.seed = SEED
	p.randomize_seed = false
	return p


func _config_file_count() -> int:
	var da := DirAccess.open(out_dir)
	var count := 0
	if da == null:
		return 0
	da.list_dir_begin()
	var name := da.get_next()
	while name != "":
		if name.begins_with("proc_city_") and name.ends_with("_config.json"):
			count += 1
		name = da.get_next()
	return count


func _check_mode(mode: int, cli: String, label: String) -> void:
	print("=== gRPC parity: %s ===" % label)
	if cli.is_empty():
		print("  skip: no %s CLI resolved" % label)
		return

	var before := _config_file_count()
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.params = _scene_params()
	gen.grid_vertices = Vector2i(16, 16)
	gen.auto_download_binary = false
	gen.generation_mode = mode
	gen.binary_path_override = cli

	gen.generation_failed.connect(func(stage: String, msg: String) -> void:
		_fail("generation_failed %s: %s" % [stage, msg])
		gen.all_finished.emit())

	gen.generate_all()
	await gen.all_finished

	var used := gen.get_last_generation_mode_used()
	var height_image := gen.get_height_image()
	gen.queue_free()
	await process_frame

	_check(used == mode, "generate_all used %s as requested" % label)
	_check(_config_file_count() == before, "generate_all wrote no config JSON into out_dir")
	_check(height_image != null, "displacement stage produced a height image")
	if height_image == null:
		return

	var runner := GoplacementxRunner.new()
	runner.cli_kind = GoplacementxRunner.CLI_GPUDISPLACEMENTX if mode == ProcCityGenerator.GEN_GPU_LEGACY else GoplacementxRunner.CLI_GODISPLACEMENTX
	var p := _scene_params()
	var config := runner.write_config(out_dir, p)
	var ref_path := out_dir.path_join("grpc_parity_" + label.to_lower() + "_height.gdxraw")
	var emits := [{"mode": "grayscale", "seed": SEED, "path": ref_path}]
	var r: Dictionary = runner.run_bundle(cli, config, emits, p)
	_check(int(r.get("code", -1)) == 0, "reference CLI-subprocess run exit 0")
	if int(r.get("code", -1)) != 0:
		return

	var ref_bytes := FileAccess.get_file_as_bytes(ref_path)
	_check(ref_bytes.size() > 16, "reference map decoded (%d bytes)" % ref_bytes.size())
	# Strip the 16-byte GDXR header before comparing against the in-memory image.
	var ref_pixels := ref_bytes.slice(16)
	_check(ref_pixels == height_image.get_data(), "gRPC height pixels == CLI-subprocess height pixels")
