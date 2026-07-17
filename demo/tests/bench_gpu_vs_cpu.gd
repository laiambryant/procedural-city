# GPU vs CPU timing of the generate_all pipeline at small (512), medium (2048)
# and very large (8192) resolutions, plus per-CLI bundle timings to split the
# CLI cost from the Godot-side decode+mesh cost. Run from the repo root:
#   godot --headless --path demo -s res://tests/bench_gpu_vs_cpu.gd -- <cpu_cli> <gpu_cli> <out_dir>
extends SceneTree

const SEED := 424242
const GRID := Vector2i(64, 64)
const MESH_SIZE := Vector2(20, 20)
const HEIGHT_SCALE := 8.32
const RESOLUTIONS := [512, 2048, 8192]
const RUNS := 3

var out_dir := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		push_error("usage: -- <cpu_cli> <gpu_cli> <out_dir>")
		quit(1)
		return
	out_dir = args[2]
	_run(args[0], args[1])


func _run(cpu_cli: String, gpu_cli: String) -> void:
	await _main(cpu_cli, gpu_cli)
	quit(0)


func _scene_params(resolution: int) -> GoplacementxParams:
	var p := GoplacementxParams.new()
	p.resolution = resolution
	p.palette_preset = 3
	p.sprites_enabled = true
	p.sprite_packs = 15
	p.composition_modes = 65535
	p.seed = SEED
	p.randomize_seed = false
	return p


func _bench_bundle(label: String, binary: String, resolution: int) -> void:
	var runner := GoplacementxRunner.new()
	var p := _scene_params(resolution)
	var config := runner.write_config(out_dir, p)
	var emits := []
	for mode in ["grayscale", "color", "normal"]:
		emits.append({"mode": mode, "seed": SEED, "path": out_dir.path_join("%s_%s.png" % [label, mode])})
	var t0 := Time.get_ticks_msec()
	var r: Dictionary = runner.run_bundle(binary, config, emits, p)
	print("cli   %-24s | %6d ms | code %s" % [label, Time.get_ticks_msec() - t0, str(r.get("code"))])


func _bench_generate_all(cli: String, mode: int, resolution: int, use_server: bool = false) -> int:
	await process_frame
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.params = _scene_params(resolution)
	gen.mesh_size = MESH_SIZE
	gen.grid_vertices = GRID
	gen.height_scale = HEIGHT_SCALE
	gen.material_mode = 1
	gen.generation_mode = mode
	gen.use_gpu_server = use_server
	gen.binary_path_override = cli
	gen.output_dir = out_dir
	gen.generation_failed.connect(func(stage, msg): push_error("failed %s: %s" % [stage, msg]))
	var t0 := Time.get_ticks_msec()
	gen.generate_all()
	await gen.all_finished
	var dt := Time.get_ticks_msec() - t0
	var ran := gen.get_last_generation_mode_used()
	gen.queue_free()
	if ran != mode:
		push_error("requested mode %d but ran %d" % [mode, ran])
	return dt


func _median(times: Array) -> int:
	var ordered := times.duplicate()
	ordered.sort()
	return ordered[ordered.size() >> 1]


func _main(cpu_cli: String, gpu_cli: String) -> void:
	for resolution in RESOLUTIONS:
		print("=== %d x %d ===" % [resolution, resolution])
		_bench_bundle("cpu_%d" % resolution, cpu_cli, resolution)
		_bench_bundle("gpu_%d" % resolution, gpu_cli, resolution)
		for leg in [["CPU", ProcCityGenerator.GEN_CPU, cpu_cli, false], ["GPU", ProcCityGenerator.GEN_GPU, gpu_cli, false], ["GPU_SRV", ProcCityGenerator.GEN_GPU, gpu_cli, true]]:
			var times := []
			for i in RUNS:
				times.append(await _bench_generate_all(leg[2], leg[1], resolution, leg[3]))
			print("e2e   %-24s | runs %s ms | median %d ms" % ["%s_%d" % [leg[0], resolution], str(times), _median(times)])
	print("bench done")
