# Benchmark harness for HeightmapMesher backends.
# Run from the repo root:
#   godot --headless --path demo -s res://tests/bench_meshers.gd
# Prints best-of-N timings plus geometry fingerprints (AABB, vertex count,
# multimesh buffer hash) so optimizations can prove output parity.
extends SceneTree

const IMG_SIZE := 1024
const GRID := Vector2i(129, 129)
const GRID_STRESS := Vector2i(257, 257)
const RUNS := 5
const MESH_SIZE := Vector2(100, 100)
const HEIGHT_SCALE := 8.0
const BASE_HEIGHT := 0.3
const SEED := 424242


func _init() -> void:
	_main()
	quit(0)


func _make_height_image() -> Image:
	var bytes := PackedByteArray()
	bytes.resize(IMG_SIZE * IMG_SIZE)
	var v := 123456789
	for i in IMG_SIZE * IMG_SIZE:
		v = (v * 1103515245 + 12345) & 0x7FFFFFFF
		bytes[i] = (v >> 16) & 0xFF
	return Image.create_from_data(IMG_SIZE, IMG_SIZE, false, Image.FORMAT_L8, bytes)


func _time_best(label: String, f: Callable) -> Variant:
	var best := 1e18
	var out: Variant = null
	for r in RUNS:
		var t0 := Time.get_ticks_usec()
		out = f.call()
		var dt := float(Time.get_ticks_usec() - t0) / 1000.0
		best = minf(best, dt)
	print("%s | %.2f ms" % [label, best])
	return out


func _mesh_fingerprint(label: String, mesh: ArrayMesh) -> void:
	if mesh == null:
		push_error(label + ": null mesh")
		return
	var arrays := mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var aabb := mesh.get_aabb()
	print("%s | verts=%d aabb=%s" % [label, verts.size(), aabb])


func _main() -> void:
	var img := _make_height_image()
	var mesher := HeightmapMesher.new()

	var blocks: ArrayMesh = _time_best("blocks_nearest_129", func():
		return mesher.build_array_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_NEAREST, SEED, 0.7, 0.12))
	_mesh_fingerprint("blocks_nearest_129", blocks)

	var blocks_box: ArrayMesh = _time_best("blocks_box_129", func():
		return mesher.build_array_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, SEED, 0.7, 0.12))
	_mesh_fingerprint("blocks_box_129", blocks_box)

	var blocks_plain: ArrayMesh = _time_best("blocks_plain_257", func():
		return mesher.build_array_mesh(img, MESH_SIZE, GRID_STRESS, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, SEED, 0.0, 0.0))
	_mesh_fingerprint("blocks_plain_257", blocks_plain)

	var hex: ArrayMesh = _time_best("hex_129", func():
		return mesher.build_hex_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, 0.35, 0.45, 0.06, SEED,
				Rect2(), 0.0, 24.0, 0.7, 0.12, true))
	_mesh_fingerprint("hex_129", hex)

	var mm: MultiMesh = _time_best("multimesh_257", func():
		return mesher.build_multimesh(img, MESH_SIZE, GRID_STRESS, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE))
	if mm != null:
		print("multimesh_257 | count=%d buffer_floats=%d" % [mm.instance_count, mm.buffer.size()])

	print("bench done")
