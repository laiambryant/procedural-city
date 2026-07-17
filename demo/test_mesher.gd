extends SceneTree
# Headless verification of the mesher rewrite + new features.
# Run: godot --headless --path demo --script test_mesher.gd

var fails := 0

func check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		fails += 1
		printerr("FAIL  ", label)

func make_noise_image(w: int, h: int) -> Image:
	var img := Image.create(w, h, false, Image.FORMAT_L8)
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	for y in h:
		for x in w:
			img.set_pixel(x, y, Color(rng.randf(), 0, 0))
	return img

func _init() -> void:
	var img := make_noise_image(256, 256)
	var mesher := HeightmapMesher.new()
	var size := Vector2(20, 20)
	var verts := Vector2i(33, 33) # 32x32 = 1024 cells

	# --- ArrayMesh, merged mode ---
	var t0 := Time.get_ticks_usec()
	var mesh: ArrayMesh = mesher.build_array_mesh(img, size, verts, 4.0, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE)
	var t1 := Time.get_ticks_usec()
	check(mesh != null and mesh.get_surface_count() == 1, "array mesh built (%.1f ms)" % ((t1 - t0) / 1000.0))
	var arrays := mesh.surface_get_arrays(0)
	var vtx: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var tan: PackedFloat32Array = arrays[Mesh.ARRAY_TANGENT]
	check(idx.size() > 0 and idx.size() % 6 == 0, "indexed quads emitted (%d indices)" % idx.size())
	check(vtx.size() * 4 == tan.size(), "tangents present for every vertex")
	check(vtx.size() == idx.size() / 6 * 4, "exact preallocation (no waste)")
	var aabb := mesh.get_aabb()
	check(absf(aabb.size.x - 20.0) < 0.001 and aabb.size.y <= 4.0 + 0.001, "AABB matches mesh_size/height_scale")

	# 1024 cells: tops = 1024 quads; walls bounded by 4*1024
	var quads := idx.size() / 6
	check(quads >= 1024 and quads <= 5 * 1024, "quad count in expected range (%d)" % quads)

	# --- height_power shapes the skyline ---
	var mesh_pow: ArrayMesh = mesher.build_array_mesh(img, size, verts, 4.0, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE, 4.0)
	var top_pow := mesh_pow.get_aabb().size.y
	# mean of v^4 << mean of v, but max stays comparable
	check(top_pow <= 4.0 + 0.001, "height_power keeps peak within scale")
	var sum_y := 0.0
	for v in vtx:
		sum_y += v.y
	var arrays_pow := mesh_pow.surface_get_arrays(0)
	var vtx_pow: PackedVector3Array = arrays_pow[Mesh.ARRAY_VERTEX]
	var sum_y_pow := 0.0
	for v in vtx_pow:
		sum_y_pow += v.y
	check(sum_y_pow / vtx_pow.size() < sum_y / vtx.size(), "height_power=4 lowers average height")

	# --- block_inset: freestanding blocks + ground plane ---
	var mesh_inset: ArrayMesh = mesher.build_array_mesh(img, size, verts, 4.0, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE, 1.0, 0.15)
	var arrays_i := mesh_inset.surface_get_arrays(0)
	var idx_i: PackedInt32Array = arrays_i[Mesh.ARRAY_INDEX]
	check(idx_i.size() / 6 == 1024 * 5 + 1, "inset mode emits 5 quads/cell + ground (%d quads)" % (idx_i.size() / 6))

	# --- hex mesh still works, with power ---
	var hex: ArrayMesh = mesher.build_hex_mesh(img, size, verts, 4.0, 0.0, 1, 0.35, 0.45, 0.06, 42, Rect2(), 0.0, 24.0, 2.0)
	check(hex != null and hex.get_aabb().size.y > 0.0, "hex mesh built with height_power")

	# --- multimesh via bulk buffer ---
	var mm: MultiMesh = mesher.build_multimesh(img, size, verts, 4.0, 0.0, 1, 1.0, 0.1)
	check(mm != null and mm.instance_count == 1024, "multimesh instance count")
	# get_instance_transform is a no-op on the headless dummy renderer, so
	# validate the raw buffer (12 floats/instance, Transform3D rows).
	var buf := mm.buffer
	check(buf.size() == 1024 * 12, "multimesh buffer sized for all instances")
	check(absf(buf[0] - (20.0 / 32.0) * 0.8) < 0.001, "multimesh inset footprint via buffer")
	check(buf[5] > 0.0 and absf(buf[7] - buf[5] * 0.5) < 0.001, "multimesh height/origin consistent")

	# --- generator: user-supplied heightmap + collision ---
	var gen := ProcCityGenerator.new()
	get_root().add_child(gen)
	gen.mesh_size = size
	gen.grid_vertices = verts
	gen.height_scale = 4.0
	gen.generate_collision = true
	gen.set_height_image(img)
	gen.build_geometry()
	var city := gen.get_node_or_null("GeneratedCity")
	check(city != null and city is MeshInstance3D, "build_geometry from injected height image")
	var body := city.get_node_or_null("CollisionBody") if city else null
	check(body != null and body is StaticBody3D, "collision StaticBody3D generated")
	if body:
		var shape_node := body.get_node_or_null("CollisionShape")
		var shape: ConcavePolygonShape3D = shape_node.shape if shape_node else null
		check(shape != null and shape.get_faces().size() > 0, "trimesh shape has faces")

	# --- multimesh collision path ---
	gen.build_mode = ProcCityGenerator.BUILD_MULTIMESH
	gen.build_geometry()
	var city2 := gen.get_node_or_null("GeneratedCity")
	check(city2 is MultiMeshInstance3D, "multimesh container installed")
	check(city2.get_node_or_null("CollisionBody") != null, "multimesh collision generated")

	# --- perf probe: large grid ---
	var big := Vector2i(129, 129) # 16384 cells
	var p0 := Time.get_ticks_usec()
	var big_mesh: ArrayMesh = mesher.build_array_mesh(img, size, big, 4.0, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE)
	var p1 := Time.get_ticks_usec()
	check(big_mesh != null, "16384-cell mesh in %.1f ms" % ((p1 - p0) / 1000.0))

	print("---")
	if fails == 0:
		print("ALL TESTS PASSED")
	else:
		printerr(str(fails) + " TEST(S) FAILED")
	quit(1 if fails > 0 else 0)
