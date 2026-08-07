# The persistent GPU server (opt-in via use_gpu_server) must serve consecutive
# generations from one long-lived gpudisplacementx `serve` process, keeping
# get_last_generation_mode_used() == GEN_GPU_LEGACY and ProcCityGpuServer.is_running()
# true across runs. With no GPU binary it must fall back cleanly to the CPU.
# Run headless:
#   godot --headless --path demo -s res://tests/gpu_server.gd
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
	if gpu_available:
		await _check_server_reuse()
	else:
		await _check_server_absent_fallback()
	if failures > 0:
		push_error("gpu_server: %d failure(s)" % failures)
		quit(1)
		return
	print("gpu_server: OK")
	quit(0)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		_fail("check failed: " + label)


func _make_generator(mode: int, use_server: bool) -> ProcCityGenerator:
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
	gen.use_gpu_server = use_server
	return gen


func _generate(gen: ProcCityGenerator) -> int:
	var finished_stages: Array = []
	var on_finished := func(stage: String, _path: String) -> void:
		finished_stages.append(stage)
	var on_failed := func(stage: String, msg: String) -> void:
		_fail("generation_failed %s: %s" % [stage, msg])
		gen.all_finished.emit()
	gen.generation_finished.connect(on_finished)
	gen.generation_failed.connect(on_failed)

	gen.generate_all()
	await gen.all_finished

	gen.generation_finished.disconnect(on_finished)
	gen.generation_failed.disconnect(on_failed)
	_check(finished_stages.has("displacement"), "displacement stage finished")
	_check(finished_stages.has("geometry"), "geometry stage finished")
	_check(finished_stages.has("material"), "material stage finished")
	return gen.get_last_generation_mode_used()


func _check_server_reuse() -> void:
	print("=== GPU persistent server: due run consecutivi ===")
	var gen := _make_generator(ProcCityGenerator.GEN_GPU_LEGACY, true)

	var used1 := await _generate(gen)
	_check(used1 == ProcCityGenerator.GEN_GPU_LEGACY, "run 1 last_generation_mode_used == GEN_GPU_LEGACY")
	_check(ProcCityGpuServer.is_running(), "server in piedi dopo run 1")

	var used2 := await _generate(gen)
	_check(used2 == ProcCityGenerator.GEN_GPU_LEGACY, "run 2 last_generation_mode_used == GEN_GPU_LEGACY")
	_check(ProcCityGpuServer.is_running(), "server ancora in piedi dopo run 2 (processo riusato)")

	gen.queue_free()
	await process_frame


func _check_server_absent_fallback() -> void:
	print("=== use_gpu_server con binario GPU assente (fallback CPU atteso) ===")
	var gen := _make_generator(ProcCityGenerator.GEN_GPU_LEGACY, true)
	var used := await _generate(gen)
	_check(used == ProcCityGenerator.GEN_CPU_LEGACY, "fallback pulito a GEN_CPU_LEGACY senza binario GPU")
	gen.queue_free()
	await process_frame
