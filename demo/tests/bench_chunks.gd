extends SceneTree

func _init() -> void:
	var image := Image.create(2048, 2048, false, Image.FORMAT_L8)
	image.fill(Color(0.5, 0.5, 0.5))
	var mesher := HeightmapMesher.new()
	for grid in [97, 257]:
		for chunks in [1, 2, 4, 8]:
			var times: Array[float] = []
			var fingerprint := 0
			for run in 6:
				var start := Time.get_ticks_usec()
				var meshes := mesher.build_array_mesh_chunks(image, Vector2(140, 140), Vector2i(grid, grid),
					26.0, 0.0, HeightmapMesher.FILTER_BOX_AVERAGE, 1.55, 0.11, 424242, 0.8, 0.16, 0.0, chunks)
				var elapsed := (Time.get_ticks_usec() - start) / 1000.0
				var current := 0
				for mesh in meshes:
					current = hash([current, mesh.surface_get_arrays(0)])
				if run > 0:
					assert(current == fingerprint)
					times.append(elapsed)
				fingerprint = current
			times.sort()
			print("BENCH chunks grid=%d chunks=%d median=%.3f ms fingerprint=%d" % [grid, chunks, times[2], fingerprint])
	quit()
