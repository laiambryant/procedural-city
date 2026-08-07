# GPU mode must use the gpudisplacementx CLI when one resolves locally and
# fall back to the CPU pipeline (with a warning) when none does; CPU mode must
# stay untouched. Run headless:
#   godot --headless --path demo -s res://tests/gpu_fallback.gd
extends SceneTree

const SEED := 424242

var failures := 0


func _init() -> void:
	_run()


func _run() -> void:
	await process_frame
	var probe := GoplacementxRunner.new()
	probe.cli_kind = GoplacementxRunner.CLI_GPUDISPLACEMENTX
	var gpu_available := not probe.find_binary("").is_empty()
	var expected := ProcCityGenerator.GEN_GPU_LEGACY if gpu_available else ProcCityGenerator.GEN_CPU_LEGACY
	var title := "GPU mode con binario bundled (GPU attesa)" if gpu_available \
			else "GPU mode senza binario gpudisplacementx (fallback atteso)"
	await _check_generation(ProcCityGenerator.GEN_GPU_LEGACY, expected, title)
	await _check_generation(ProcCityGenerator.GEN_CPU_LEGACY, ProcCityGenerator.GEN_CPU_LEGACY, "CPU mode end-to-end")
	if failures > 0:
		push_error("gpu_fallback: %d failure(s)" % failures)
		quit(1)
		return
	print("gpu_fallback: OK")
	quit(0)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		_fail("check failed: " + label)


func _check_generation(mode: int, expected_used: int, title: String) -> void:
	print("=== " + title + " ===")
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	var p := GoplacementxParams.new()
	p.resolution = 256
	p.seed = SEED
	p.randomize_seed = false
	gen.params = p
	gen.grid_vertices = Vector2i(16, 16)
	gen.auto_download_binary = false
	gen.generation_mode = mode

	var finished_stages: Array = []
	gen.generation_finished.connect(func(stage: String, _path: String) -> void:
		finished_stages.append(stage))
	gen.generation_failed.connect(func(stage: String, msg: String) -> void:
		_fail("generation_failed %s: %s" % [stage, msg])
		gen.all_finished.emit())

	gen.generate_all()
	await gen.all_finished

	_check(finished_stages.has("displacement"), "displacement stage finished")
	_check(finished_stages.has("geometry"), "geometry stage finished")
	_check(finished_stages.has("material"), "material stage finished")
	var used := gen.get_last_generation_mode_used()
	var expected_label := "GEN_GPU_LEGACY" if expected_used == ProcCityGenerator.GEN_GPU_LEGACY else "GEN_CPU_LEGACY"
	_check(used == expected_used, "last_generation_mode_used == " + expected_label)
	gen.queue_free()
	await process_frame
