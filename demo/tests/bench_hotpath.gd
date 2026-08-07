# Microbenchmark for the meshing hot path at the resolution the-beehive ships:
# an 8192 source displacement map sampled down onto a 72x72 block grid.
#
#   godot --headless --path demo -s res://tests/bench_hotpath.gd
#
# Prints best-of-N per stage plus a geometry fingerprint, so an optimization has
# to prove it left the output alone.
extends SceneTree

const SRC_SIZE := 8192
const GRID := Vector2i(73, 73)
const MESH_SIZE := Vector2(1200.0, 1200.0)
const HEIGHT_SCALE := 220.0
const BASE_HEIGHT := 6.0
const HEIGHT_POWER := 2.2
const INSET := 0.18
const CLIP_BELOW := 8.0
const SEED := 1337
const RUNS := 7


func _init() -> void:
	_main()
	quit(0)


# Deterministic L8 displacement map. Built small and bilinearly upscaled: a
# GDScript loop over 8192^2 bytes would dominate the run, and the upscale gives
# the spatial structure a real displacement map has (box-averaging a cell of
# white noise would converge to a constant and hide nothing).
const NOISE_SIZE := 256


func _make_height_image(p_size: int) -> Image:
	var bytes := PackedByteArray()
	bytes.resize(NOISE_SIZE * NOISE_SIZE)
	var v: int = 0x853C49E6
	for y in NOISE_SIZE:
		var row := y * NOISE_SIZE
		var band := int(120.0 + 90.0 * sin(float(y) * 0.09))
		for x in NOISE_SIZE:
			v = (v * 1103515245 + 12345) & 0x7FFFFFFF
			bytes[row + x] = clampi(band + ((v >> 16) & 0x7F) - 64, 0, 255)
	var img := Image.create_from_data(NOISE_SIZE, NOISE_SIZE, false, Image.FORMAT_L8, bytes)
	img.resize(p_size, p_size, Image.INTERPOLATE_BILINEAR)
	return img


func _time_best(label: String, f: Callable) -> Variant:
	var best := 1.0e18
	var out: Variant = null
	for r in RUNS:
		var t0 := Time.get_ticks_usec()
		out = f.call()
		var dt := float(Time.get_ticks_usec() - t0) / 1000.0
		best = minf(best, dt)
	print("%-28s | %8.3f ms" % [label, best])
	return out


func _fingerprint(label: String, mesh: ArrayMesh) -> void:
	if mesh == null:
		push_error(label + ": null mesh")
		return
	var arrays := mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	# Cheap order-sensitive checksum over the vertex stream.
	var acc := 0.0
	for i in range(0, verts.size(), 97):
		var p := verts[i]
		acc += p.x * 1.0 + p.y * 3.0 + p.z * 7.0 + float(i) * 0.001
	print("%-28s | verts=%d tris=%d aabb=%s sum=%.4f"
			% [label, verts.size(), idx.size() / 3, mesh.get_aabb(), acc])


func _main() -> void:
	print("=== procedural-city hot path (src=%d grid=%s) ===" % [SRC_SIZE, GRID])
	var t0 := Time.get_ticks_usec()
	var img := _make_height_image(SRC_SIZE)
	print("%-28s | %8.3f ms (setup)" % ["make_source_image", float(Time.get_ticks_usec() - t0) / 1000.0])

	var mesher := HeightmapMesher.new()

	# Isolates the box-average downsample: one sample_height call does the same
	# per-cell work the full resolve does, times cols*rows.
	_time_best("sample_height_box_x4096", func():
		var acc := 0.0
		for k in 4096:
			acc += mesher.sample_height(img, k % 72, k / 72, 72, 72,
					HeightmapMesher.FILTER_BOX_AVERAGE)
		return acc)

	var mesh: ArrayMesh = _time_best("build_array_mesh_box", func():
		return mesher.build_array_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, HEIGHT_POWER, INSET, SEED, 0.7, 0.12,
				CLIP_BELOW))
	_fingerprint("build_array_mesh_box", mesh)

	_time_best("build_array_mesh_nearest", func():
		return mesher.build_array_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_NEAREST, HEIGHT_POWER, INSET, SEED, 0.7, 0.12,
				CLIP_BELOW))

	_time_best("create_trimesh_shape", func():
		return mesh.create_trimesh_shape())

	# Split of the above: how much is Godot pulling the arrays back out of the
	# RenderingServer and de-indexing them, versus the physics server's BVH.
	var faces: PackedVector3Array = _time_best("  .get_faces (readback)", func():
		return mesh.get_faces())
	_time_best("  .set_faces (bvh only)", func():
		var s := ConcavePolygonShape3D.new()
		s.set_faces(faces)
		return s)
	# Where the trimesh cost actually sits, and why it stays a trimesh: the
	# readback is noise next to the physics server's BVH build. Replacing it with
	# one BoxShape3D per standing cell (geometrically identical for extruded
	# blocks, no BVH at all) measured ~4x *worse*, because
	# CollisionObject3D.shape_owner_add_shape rebuilds the whole shape list per
	# call. A cheaper collider would have to be assembled at the PhysicsServer3D
	# level, and would then cost more in broadphase than it saves here.
	print("%-28s | faces=%d" % ["  trimesh faces", faces.size()])

	# Chunked geometry: what the renderer gains in culling granularity it also
	# gains here, because each tile's collision BVH is built over its own slice
	# instead of one grid-wide triangle soup.
	for chunks in [2, 4]:
		_time_best("build_chunks_%dx%d" % [chunks, chunks], func():
			return mesher.build_array_mesh_chunks(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
					HeightmapMesher.FILTER_BOX_AVERAGE, HEIGHT_POWER, INSET, SEED, 0.7, 0.12,
					CLIP_BELOW, chunks).size())
		# Built outside the timed closure: GDScript lambdas capture by value, so a
		# tile array assigned inside one is not visible to the next.
		var tiles: Array = mesher.build_array_mesh_chunks(img, MESH_SIZE, GRID, HEIGHT_SCALE,
				BASE_HEIGHT, HeightmapMesher.FILTER_BOX_AVERAGE, HEIGHT_POWER, INSET, SEED, 0.7,
				0.12, CLIP_BELOW, chunks)
		_time_best("  .trimesh x%d" % (chunks * chunks), func():
			var shapes := []
			for t in tiles:
				shapes.append((t as ArrayMesh).create_trimesh_shape())
			return shapes.size())

	var hex: ArrayMesh = _time_best("build_hex_mesh", func():
		return mesher.build_hex_mesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, 0.35, 0.45, 0.06, SEED,
				Rect2(), 0.0, 24.0, HEIGHT_POWER, 0.7, 0.12, true))
	_fingerprint("build_hex_mesh", hex)

	_time_best("build_multimesh", func():
		return mesher.build_multimesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
				HeightmapMesher.FILTER_BOX_AVERAGE, HEIGHT_POWER, INSET))

	print("bench done")
