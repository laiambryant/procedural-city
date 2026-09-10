extends SceneTree

const RUNS := 5

func _init() -> void:
	_run()

func _run() -> void:
	await process_frame
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.persist_in_scene = false
	gen.auto_download_binary = false
	gen.params = GoplacementxParams.new()
	gen.params.randomize_seed = false
	gen.params.seed = 424242
	gen.params.resolution = 2048
	gen.params.iterations = 420
	gen.grid_vertices = Vector2i(97, 97)
	gen.max_cells = 16384
	gen.mesh_size = Vector2(140, 140)
	gen.height_scale = 26.0
	gen.height_power = 1.55
	gen.block_inset = 0.11
	gen.generation_failed.connect(func(stage: String, message: String):
		push_error("%s: %s" % [stage, message])
		quit(1))
	for mode in [ProcCityGenerator.GEN_CPU_NATIVE, ProcCityGenerator.GEN_GPU_NATIVE]:
		gen.generation_mode = mode
		var times: Array[float] = []
		var fingerprint := ""
		for run in RUNS + 1:
			var start := Time.get_ticks_usec()
			gen.generate_all()
			await gen.all_finished
			var elapsed := (Time.get_ticks_usec() - start) / 1000.0
			var hash_context := HashingContext.new()
			hash_context.start(HashingContext.HASH_SHA256)
			hash_context.update(gen.get_height_image().get_data())
			var current := hash_context.finish().hex_encode()
			if run > 0:
				assert(current == fingerprint, "Repeated generation changed its output")
				times.append(elapsed)
			fingerprint = current
		times.sort()
		print("BENCH native requested=%d used=%d median=%.2f ms min=%.2f max=%.2f height=%s" % [
			mode, gen.get_last_generation_mode_used(), times[RUNS / 2], times[0], times[-1], fingerprint])
	gen.queue_free()
	await process_frame
	quit()
