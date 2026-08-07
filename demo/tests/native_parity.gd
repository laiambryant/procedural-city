# The two native backends must be interchangeable: same seed, same bytes. Run
# with a real window so a RenderingDevice exists (headless has none, and the
# GPU mode then legitimately falls back to the CPU one):
#   godot --path demo -s res://tests/native_parity.gd
extends SceneTree

const SEED := 424242
const RESOLUTION := 256

var failures := 0


func _init() -> void:
	_run()


func _run() -> void:
	await process_frame
	var cpu := await _generate(ProcCityGenerator.GEN_CPU_NATIVE, "CPU nativo")
	var gpu := await _generate(ProcCityGenerator.GEN_GPU_NATIVE, "GPU nativo")

	_check(cpu["used"] == ProcCityGenerator.GEN_CPU_NATIVE, "CPU mode stays on the CPU backend")
	if gpu["used"] == ProcCityGenerator.GEN_GPU_NATIVE:
		_check(cpu["height"] == gpu["height"], "GPU and CPU height fields are byte-identical")
		_check(cpu["albedo"] == gpu["albedo"], "GPU and CPU albedo maps are byte-identical")
	else:
		print("  skip: no RenderingDevice, GPU mode fell back to the CPU backend")

	var repeat := await _generate(ProcCityGenerator.GEN_CPU_NATIVE, "CPU nativo, stesso seed")
	_check(cpu["height"] == repeat["height"], "the same seed reproduces the same field")

	if failures > 0:
		push_error("native_parity: %d failure(s)" % failures)
		quit(1)
		return
	print("native_parity: OK")
	quit(0)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		_fail("check failed: " + label)


func _generate(mode: int, title: String) -> Dictionary:
	print("=== " + title + " ===")
	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	var p := GoplacementxParams.new()
	p.resolution = RESOLUTION
	p.seed = SEED
	p.randomize_seed = false
	gen.params = p
	gen.grid_vertices = Vector2i(16, 16)
	gen.auto_download_binary = false
	gen.generation_mode = mode

	var stages: Array = []
	gen.generation_finished.connect(func(stage: String, path: String) -> void:
		stages.append(stage)
		if not path.is_empty():
			_fail("native backend wrote an intermediate file: " + path))
	gen.generation_failed.connect(func(stage: String, msg: String) -> void:
		_fail("generation_failed %s: %s" % [stage, msg])
		gen.all_finished.emit())

	gen.generate_all()
	await gen.all_finished

	_check(stages.has("displacement"), "displacement stage finished")
	_check(stages.has("geometry"), "geometry stage finished")
	_check(stages.has("material"), "material stage finished")

	var height: PackedByteArray = gen.get_height_image().get_data() if gen.get_height_image() != null else PackedByteArray()
	var generated := gen.get_node_or_null("GeneratedCity")
	_check(generated != null, "GeneratedCity installed")

	var result := {
		"used": gen.get_last_generation_mode_used(),
		"height": height,
		"albedo": _albedo_bytes(generated),
	}
	gen.queue_free()
	await process_frame
	return result


func _albedo_bytes(generated: Node) -> PackedByteArray:
	if generated == null:
		return PackedByteArray()
	for child in generated.get_children():
		if child is MeshInstance3D and child.material_override is BaseMaterial3D:
			var texture: Texture2D = (child.material_override as BaseMaterial3D).albedo_texture
			if texture != null:
				return texture.get_image().get_data()
	return PackedByteArray()
