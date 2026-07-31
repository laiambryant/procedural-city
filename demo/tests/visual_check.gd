# Headed smoke test: builds a small city with a fixed seed, captures one
# screenshot per mesh backend, and exercises the CLI resolution chain.
# Run from the repo root:
#   godot --path demo --script res://tests/visual_check.gd
# Screenshots land in PC_SHOT_DIR (env var) or user://.
extends SceneTree

var _failed := false


func _init() -> void:
	_main()


func _out_dir() -> String:
	var dir := OS.get_environment("PC_SHOT_DIR")
	return dir if not dir.is_empty() else OS.get_user_data_dir()


func _main() -> void:
	await process_frame

	var world := Node3D.new()
	root.add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52, -35, 0)
	sun.shadow_enabled = true
	world.add_child(sun)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	e.sky = Sky.new()
	e.sky.sky_material = ProceduralSkyMaterial.new()
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.environment = e
	world.add_child(env)

	var cam := Camera3D.new()
	world.add_child(cam)
	cam.position = Vector3(13, 9, 13)
	cam.look_at(Vector3(0, 2.5, 0))

	var gen := ProcCityGenerator.new()
	world.add_child(gen)
	gen.mesh_size = Vector2(24, 24)
	gen.grid_vertices = Vector2i(33, 33)
	gen.height_scale = 6.0
	gen.base_height = 0.3
	gen.build_mode = ProcCityGenerator.BUILD_ARRAY_MESH
	gen.texture_mode = ProcCityGenerator.TEX_SHARED

	var params := GoplacementxParams.new()
	params.resolution = 512
	params.randomize_seed = false
	params.seed = 1234
	gen.params = params

	gen.generation_failed.connect(func(stage: String, msg: String) -> void:
		push_error("visual_check FAILED at %s: %s" % [stage, msg])
		_failed = true
	)

	# The resolution chain itself (no download here: the bundled binary wins).
	var runner := GoplacementxRunner.new()
	var found := runner.find_binary("")
	print("visual_check: resolved CLI -> ", found)
	if found.is_empty():
		push_error("visual_check: no CLI binary resolved")
		quit(1)
		return

	gen.generate_all()
	await gen.all_finished
	if _failed:
		quit(1)
		return
	await process_frame
	await process_frame
	var shot := _out_dir().path_join("blocks.png")
	root.get_viewport().get_texture().get_image().save_png(shot)
	print("visual_check: wrote ", shot)

	gen.build_mode = ProcCityGenerator.BUILD_HEX
	gen.hive_gap = 0.08
	gen.generate_all()
	await gen.all_finished
	if _failed:
		quit(1)
		return
	await process_frame
	await process_frame
	shot = _out_dir().path_join("hex.png")
	root.get_viewport().get_texture().get_image().save_png(shot)
	print("visual_check: wrote ", shot)

	gen.build_mode = ProcCityGenerator.BUILD_MULTIMESH
	gen.generate_all()
	await gen.all_finished
	if _failed:
		quit(1)
		return
	await process_frame
	await process_frame
	shot = _out_dir().path_join("multimesh.png")
	root.get_viewport().get_texture().get_image().save_png(shot)
	print("visual_check: wrote ", shot)

	gen.build_mode = ProcCityGenerator.BUILD_GRIDMAP
	gen.generate_all()
	await gen.all_finished
	if _failed:
		quit(1)
		return
	await process_frame
	await process_frame
	shot = _out_dir().path_join("gridmap.png")
	root.get_viewport().get_texture().get_image().save_png(shot)
	print("visual_check: wrote ", shot)
	# A GridMap takes no material override: the material has to reach the block
	# mesh inside the generated MeshLibrary, or the city renders untextured.
	var gm: GridMap = gen.get_node_or_null("GeneratedCity")
	if gm == null or gm.mesh_library.get_item_mesh(0).material == null:
		push_error("visual_check: gridmap material did not reach the mesh library")
		quit(1)
		return

	var gpu_runner := GoplacementxRunner.new()
	gpu_runner.cli_kind = GoplacementxRunner.CLI_GPUDISPLACEMENTX
	var gpu_found := gpu_runner.find_binary("")
	if gpu_found.is_empty():
		print("visual_check: no bundled gpudisplacementx CLI, skipping the GPU screenshot")
	else:
		gen.build_mode = ProcCityGenerator.BUILD_ARRAY_MESH
		gen.generation_mode = ProcCityGenerator.GEN_GPU
		gen.generate_all()
		await gen.all_finished
		if _failed:
			quit(1)
			return
		await process_frame
		await process_frame
		shot = _out_dir().path_join("blocks_gpu.png")
		root.get_viewport().get_texture().get_image().save_png(shot)
		print("visual_check: wrote ", shot)

	quit(0)
