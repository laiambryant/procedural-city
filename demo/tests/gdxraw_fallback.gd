# Pins the interchange-format contract between the pipeline and the CLI.
#
# Case A: the CPU CLI cannot write .gdxraw, so the pipeline must ask it for
# .png. Requesting .gdxraw got a PNG written to a .gdxraw path, which then
# failed the GDXR header check ("Failed to load ...").
#
# Case B: a CLI that ignores the requested extension must still load. The fake
# GPU binary below is a shim over the CPU one, so the pipeline asks for .gdxraw
# and gets PNG bytes back; load_map_image sniffs content, not the filename.
#
# Run: godot --headless --path demo -s res://tests/gdxraw_fallback.gd
extends SceneTree

const RESOLUTION := 64
const SEED := 424242
const TIMEOUT_SEC := 120.0

var _failures: Array[String] = []


func _init() -> void:
	_main()


func _cpu_binary() -> String:
	var path := OS.get_user_data_dir().path_join("godisplacementx/bin/godisplacementx-cli")
	return path if FileAccess.file_exists(path) else ""


# _write_fake_gpu_binary produces a shim that answers to gpudisplacementx's name
# while running the CPU binary, which emits PNG whatever extension it is handed.
func _write_fake_gpu_binary(p_cpu: String) -> String:
	var path := OS.get_user_data_dir().path_join("fake_gpudisplacementx-cli")
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("#!/bin/sh\nexec \"%s\" \"$@\"\n" % p_cpu)
	f.close()
	OS.execute("chmod", ["+x", path])
	return path


func _make_generator(p_mode: int, p_override: String) -> ProcCityGenerator:
	var gen := ProcCityGenerator.new()
	var params := GoplacementxParams.new()
	params.resolution = RESOLUTION
	params.seed = SEED
	params.randomize_seed = false
	gen.params = params
	gen.generation_mode = p_mode
	gen.keep_intermediate_png = false
	gen.binary_path_override = p_override
	gen.auto_download_binary = false
	root.add_child(gen)
	return gen


# _run_displacement drives one generation to completion and reports the height
# path it produced, or "" when the pipeline reported a failure.
func _run_displacement(p_gen: ProcCityGenerator) -> String:
	var outcome := {"done": false, "path": ""}
	p_gen.generation_finished.connect(
		func(stage: String, path: String) -> void:
			if stage == "displacement":
				outcome["path"] = path
				outcome["done"] = true
	)
	p_gen.generation_failed.connect(
		func(stage: String, message: String) -> void:
			_failures.append("pipeline failed at %s: %s" % [stage, message])
			outcome["done"] = true
	)
	p_gen.generate_displacement()

	var waited := 0.0
	while not outcome["done"] and waited < TIMEOUT_SEC:
		await process_frame
		waited += 0.05
		OS.delay_msec(50)
	if not outcome["done"]:
		_failures.append("timed out after %.0fs" % TIMEOUT_SEC)
	return outcome["path"]


func _check(p_condition: bool, p_message: String) -> void:
	if not p_condition:
		_failures.append(p_message)


func _case_cpu_asks_for_png(p_cpu: String) -> void:
	var gen := _make_generator(ProcCityGenerator.GEN_CPU, p_cpu)
	var path := await _run_displacement(gen)
	_check(path.ends_with(".png"), "CPU run should emit .png, got: " + path)
	_check(gen.get_cell_heights().size() > 0, "CPU run produced no height data")
	gen.queue_free()


func _case_png_bytes_under_gdxraw_name(p_cpu: String) -> void:
	var gen := _make_generator(ProcCityGenerator.GEN_GPU, _write_fake_gpu_binary(p_cpu))
	var path := await _run_displacement(gen)
	_check(path.ends_with(".gdxraw"), "fake GPU run should emit .gdxraw, got: " + path)
	_check(gen.get_cell_heights().size() > 0, "PNG bytes under a .gdxraw name failed to load")
	gen.queue_free()


func _main() -> void:
	var cpu := _cpu_binary()
	if cpu.is_empty():
		print("SKIP: godisplacementx-cli not installed under user://")
		quit(0)
		return

	await _case_cpu_asks_for_png(cpu)
	await _case_png_bytes_under_gdxraw_name(cpu)

	if _failures.is_empty():
		print("PASS: gdxraw_fallback")
		quit(0)
	else:
		for failure in _failures:
			printerr("FAIL: " + failure)
		quit(1)
