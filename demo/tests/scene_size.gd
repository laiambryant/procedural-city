# A persisted city must not be embedded in the scene text: externalising its
# sub-resources is what keeps a .tscn a few kilobytes instead of hundreds of
# megabytes of base64. Run headless:
#   godot --headless --path demo -s res://tests/scene_size.gd
extends SceneTree

const SEED := 424242
const EXTERNAL_DIR := "user://scene_size_probe"
const EMBEDDED_PATH := "user://scene_size_embedded.tscn"
const EXTERNAL_PATH := "user://scene_size_external.tscn"
# A 96x96 grid embeds as several megabytes of base64; anything near that means
# the mesh is still inline.
const EXTERNAL_LIMIT := 64 * 1024

var failures := 0


func _init() -> void:
	_run()


func _run() -> void:
	await process_frame
	var embedded := await _packed_size("", EMBEDDED_PATH)
	var external := await _packed_size(EXTERNAL_DIR, EXTERNAL_PATH)

	print("  embedded scene: %d bytes" % embedded)
	print("  external scene: %d bytes" % external)
	_check(embedded > 0 and external > 0, "both probe scenes were written")
	_check(external < EXTERNAL_LIMIT, "externalised scene stays under %d bytes" % EXTERNAL_LIMIT)
	_check(external * 8 < embedded, "externalising shrinks the scene by at least 8x")
	_check(_dir_has_resources(EXTERNAL_DIR), "sub-resources were written as .res files")
	_check(not _has_embedded_pixels(EXTERNAL_PATH), "no image data stayed inline in the externalised scene")

	if failures > 0:
		push_error("scene_size: %d failure(s)" % failures)
		quit(1)
		return
	print("scene_size: OK")
	quit(0)


func _fail(message: String) -> void:
	failures += 1
	push_error(message)


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		_fail("check failed: " + label)


func _packed_size(external_dir: String, scene_path: String) -> int:
	var holder := Node3D.new()
	root.add_child(holder)
	var gen := ProcCityGenerator.new()
	holder.add_child(gen)

	var p := GoplacementxParams.new()
	p.resolution = 256
	p.seed = SEED
	p.randomize_seed = false
	gen.params = p
	gen.grid_vertices = Vector2i(96, 96)
	gen.max_cells = 65536
	gen.auto_download_binary = false
	gen.generation_mode = ProcCityGenerator.GEN_CPU_NATIVE
	gen.generate_collision = true
	gen.external_resource_dir = external_dir

	gen.generation_failed.connect(func(stage: String, msg: String) -> void:
		_fail("generation_failed %s: %s" % [stage, msg])
		gen.all_finished.emit())
	gen.generate_all()
	await gen.all_finished

	if not external_dir.is_empty():
		gen.externalize_generated_resources()

	_own_recursive(holder, holder)
	var packed := PackedScene.new()
	if packed.pack(holder) != OK:
		_fail("could not pack the probe scene")
		return 0
	if ResourceSaver.save(packed, scene_path) != OK:
		_fail("could not save " + scene_path)
		return 0

	var size := FileAccess.get_file_as_bytes(scene_path).size()
	holder.queue_free()
	await process_frame
	return size


func _own_recursive(node: Node, owner_node: Node) -> void:
	for child in node.get_children():
		child.owner = owner_node
		_own_recursive(child, owner_node)


func _has_embedded_pixels(scene_path: String) -> bool:
	var text := FileAccess.get_file_as_string(scene_path)
	return text.contains('[sub_resource type="Image')


func _dir_has_resources(dir_path: String) -> bool:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return false
	for file in dir.get_files():
		if file.ends_with(".res"):
			return true
	return false
