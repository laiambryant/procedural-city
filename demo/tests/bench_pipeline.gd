# End-to-end timing of the generate_all pipeline with the exact main.tscn
# parameters (resolution 8192, sprites on, all packs, all 16 composition
# modes, ORM material, 64x64 grid). Run from the repo root:
#   godot --headless --path demo -s res://tests/bench_pipeline.gd -- <old_cli> <new_cli> <out_dir> [gpu_cli]
# Compares the old bundled CLI against a freshly built one, then times every
# Godot-side stage on the produced maps. With the optional 4th argument the
# same benchmarks also run against the gpudisplacementx CLI.
extends SceneTree

const SEED := 424242
const GRID := Vector2i(64, 64)
const MESH_SIZE := Vector2(20, 20)
const HEIGHT_SCALE := 8.32

var out_dir := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		push_error("usage: -- <old_cli> <new_cli> <out_dir>")
		quit(1)
		return
	out_dir = args[2]
	_run(args[0], args[1], args[3] if args.size() > 3 else "")


func _run(old_cli: String, new_cli: String, gpu_cli: String) -> void:
	await _main(old_cli, new_cli, gpu_cli)
	quit(0)


func _scene_params(resolution: int, sprites: bool) -> GoplacementxParams:
	var p := GoplacementxParams.new()
	p.resolution = resolution
	p.palette_preset = 3
	p.sprites_enabled = sprites
	p.sprite_packs = 15
	p.composition_modes = 65535
	p.seed = SEED
	p.randomize_seed = false
	return p


func _bench_bundle(label: String, binary: String, resolution: int, sprites: bool) -> Array:
	var runner := GoplacementxRunner.new()
	var p := _scene_params(resolution, sprites)
	var config := runner.write_config(out_dir, p)
	var paths := [
		out_dir.path_join(label + "_height.png"),
		out_dir.path_join(label + "_albedo.png"),
		out_dir.path_join(label + "_normal.png"),
	]
	var emits := [
		{"mode": "grayscale", "seed": SEED, "path": paths[0]},
		{"mode": "color", "seed": SEED, "path": paths[1]},
		{"mode": "normal", "seed": SEED, "path": paths[2]},
	]
	var t0 := Time.get_ticks_msec()
	var r: Dictionary = runner.run_bundle(binary, config, emits, p)
	var dt := Time.get_ticks_msec() - t0
	var sizes := []
	for path in paths:
		sizes.append(FileAccess.get_file_as_bytes(path).size() if FileAccess.file_exists(path) else -1)
	print("cli %-28s | %6d ms | code %s | png bytes %s" % [label, dt, str(r.get("code")), str(sizes)])
	return paths


func _time_ms(label: String, f: Callable) -> Variant:
	var t0 := Time.get_ticks_usec()
	var out: Variant = f.call()
	print("%-32s | %8.1f ms" % [label, float(Time.get_ticks_usec() - t0) / 1000.0])
	return out


func _main(old_cli: String, new_cli: String, gpu_cli: String) -> void:
	print("=== CLI: scene params (sprites on, 16 modes) ===")
	_bench_bundle("old_2048", old_cli, 2048, true)
	_bench_bundle("new_2048", new_cli, 2048, true)
	var paths: Array = _bench_bundle("old_8192", old_cli, 8192, true)
	var new_paths: Array = _bench_bundle("new_8192", new_cli, 8192, true)
	print("=== CLI: sprites off, 8192 (per isolare il costo sprite) ===")
	_bench_bundle("old_8192_nosprites", old_cli, 8192, false)
	_bench_bundle("new_8192_nosprites", new_cli, 8192, false)

	print("=== Lato Godot (mappe 8192 della nuova CLI) ===")
	var height: Image = _time_ms("load height.png", func(): return Image.load_from_file(new_paths[0]))
	var albedo: Image = _time_ms("load albedo.png", func(): return Image.load_from_file(new_paths[1]))
	var normal: Image = _time_ms("load normal.png", func(): return Image.load_from_file(new_paths[2]))

	var mesher := HeightmapMesher.new()
	var mesh: ArrayMesh = _time_ms("build_array_mesh 64x64", func(): return mesher.build_array_mesh(
			height, MESH_SIZE, GRID, HEIGHT_SCALE, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE, SEED, 0.0, 0.0))
	print("    mesh verts: ", mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size())

	_time_ms("ImageTexture albedo 8192", func(): return ImageTexture.create_from_image(albedo))
	_time_ms("ImageTexture normal 8192", func(): return ImageTexture.create_from_image(normal))
	_time_ms("ImageTexture height 8192", func(): return ImageTexture.create_from_image(height))

	# Parità: stessa seed, stessi parametri -> le due CLI devono produrre pixel identici.
	var old_h: Image = Image.load_from_file(paths[0])
	print("height pixel-parity old vs new: ", old_h.get_data() == height.get_data())

	await _bench_generate_all(new_cli)

	if gpu_cli != "":
		print("=== CLI GPU: scene params (sprites on, 16 modes) ===")
		_bench_bundle("gpu_2048", gpu_cli, 2048, true)
		_bench_bundle("gpu_8192", gpu_cli, 8192, true)
		_bench_bundle("gpu_8192_nosprites", gpu_cli, 8192, false)
		await _bench_generate_all(gpu_cli, ProcCityGenerator.GEN_GPU_LEGACY)

	print("bench done")


# End-to-end del nodo vero: generate_all con i parametri di main.tscn, dalla
# chiamata al segnale all_finished (CLI + decode parallelo + mesh + materiale).
func _bench_generate_all(cli: String, mode: int = ProcCityGenerator.GEN_CPU_LEGACY) -> void:
	await process_frame
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.params = _scene_params(8192, true)
	gen.mesh_size = MESH_SIZE
	gen.grid_vertices = GRID
	gen.height_scale = HEIGHT_SCALE
	gen.material_mode = 1
	gen.generation_mode = mode
	gen.binary_path_override = cli
	gen.output_dir = out_dir
	gen.generation_failed.connect(func(stage, msg): push_error("failed %s: %s" % [stage, msg]))
	var t0 := Time.get_ticks_msec()
	gen.generate_all()
	await gen.all_finished
	var label := "GPU" if mode == ProcCityGenerator.GEN_GPU_LEGACY else "CPU"
	print("generate_all end-to-end (8192, %s) | %6d ms" % [label, Time.get_ticks_msec() - t0])
	gen.queue_free()
