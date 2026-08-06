# Contract for the culling + query features on ProcCityGenerator:
#   geometry_chunks       — split the block city into MeshInstance3D tiles
#   generate_occluders    — an OccluderInstance3D per tile
#   sample_city_height / is_point_inside_block / find_clear_point
#
#   godot --headless --path demo -s res://tests/chunked_geometry.gd
#
# The load-bearing claim is that chunking changes only how the geometry is
# packaged: the union of the chunks must be the same triangles, in the same
# places, as the single mesh it replaces — chunk borders included, since a wall
# there is culled against a neighbour that lives in a different chunk.
extends SceneTree

const IMG := 256
const GRID := Vector2i(41, 41)
const MESH_SIZE := Vector2(200.0, 200.0)
const HEIGHT_SCALE := 40.0
const BASE_HEIGHT := 1.0
const INSET := 0.2
# High enough that a good share of the field is clipped away: find_clear_point
# needs somewhere clear to land, which is also the shape of the real workload
# (the-beehive sinks the field so only the tallest cells surface as towers).
const CLIP := 20.0
const SEED := 4242

var _failures := 0


func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	_main()
	await _worker_path()
	print("chunked_geometry: %s" % ("OK" if _failures == 0 else "%d FAILURES" % _failures))
	quit(1 if _failures > 0 else 0)


# generate_all is the path the game uses: the meshes are built on the worker and
# installed from the deferred result, not by the synchronous build_geometry the
# rest of this file drives. Chunking has to survive that hand-off too.
func _worker_path() -> void:
	var params := GoplacementxParams.new()
	params.resolution = 128
	params.iterations = 20
	params.sprites_enabled = false
	params.seed = 424242
	params.randomize_seed = false

	var gen: Node = ClassDB.instantiate(&"ProcCityGenerator")
	root.add_child(gen)
	gen.set("params", params)
	gen.set("grid_vertices", Vector2i(13, 13))
	gen.set("mesh_size", MESH_SIZE)
	gen.set("height_scale", HEIGHT_SCALE)
	gen.set("block_inset", INSET)
	gen.set("build_mode", 0)
	gen.set("max_cells", 169)
	gen.set("geometry_chunks", 2)
	gen.set("generate_collision", true)
	gen.set("persist_in_scene", false)
	gen.set("keep_intermediate_png", false)

	var done := {"v": false}
	gen.connect("all_finished", func() -> void: done["v"] = true)
	gen.connect("generation_failed", func(stage: String, message: String) -> void:
		_failures += 1
		printerr("  FAIL: worker generation failed at %s: %s" % [stage, message])
		done["v"] = true)
	gen.call("generate_all")

	var deadline := Time.get_ticks_msec() + 60_000
	while not done["v"]:
		await process_frame
		if Time.get_ticks_msec() >= deadline:
			_failures += 1
			printerr("  FAIL: worker generation timed out")
			return

	var built: Node = gen.get_node_or_null("GeneratedCity")
	var meshes: Array = []
	if built != null:
		_collect_meshes(built, meshes)
	_check(meshes.size() == 4, "worker path installed 4 chunks (got %d)" % meshes.size())
	var bodies: Array = []
	if built != null:
		_count_of_type(built, &"StaticBody3D", bodies)
	_check(bodies.size() == 4, "worker path gave each chunk collision (got %d)" % bodies.size())


func _check(ok: bool, label: String) -> void:
	if ok:
		print("  ok: " + label)
	else:
		_failures += 1
		printerr("  FAIL: " + label)


func _height_image() -> Image:
	var bytes := PackedByteArray()
	bytes.resize(IMG * IMG)
	var v: int = 0x1234567
	for y in IMG:
		for x in IMG:
			v = (v * 1103515245 + 12345) & 0x7FFFFFFF
			bytes[y * IMG + x] = (v >> 16) & 0xFF
	var img := Image.create_from_data(IMG, IMG, false, Image.FORMAT_L8, bytes)
	img.resize(1024, 1024, Image.INTERPOLATE_BILINEAR)
	return img


# Order-independent signature of a mesh's block triangles, plus the ground area
# it covers. Chunking changes the order vertices are emitted in, so the union can
# only be compared as a multiset — and the ground plane is deliberately NOT
# comparable triangle-for-triangle: the single mesh lays one grid-wide quad while
# each chunk lays its own patch, so that the ground culls with the chunk instead
# of keeping a city-sized AABB permanently visible. The patches tile the same
# rectangle, which is what the area total checks.
func _block_signature(mesh: ArrayMesh) -> Array:
	var keys := {}
	var ground_area := 0.0
	if mesh == null:
		return [keys, ground_area]
	for s in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(s)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		for t in range(0, idx.size(), 3):
			var a := verts[idx[t]]
			var b := verts[idx[t + 1]]
			var c := verts[idx[t + 2]]
			if _is_ground(a) and _is_ground(b) and _is_ground(c):
				ground_area += absf((b - a).cross(c - a).y) * 0.5
				continue
			var corners := [a, b, c]
			corners.sort_custom(func(p: Vector3, q: Vector3) -> bool:
				if p.x != q.x: return p.x < q.x
				if p.y != q.y: return p.y < q.y
				return p.z < q.z)
			var key := "%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f" % [
				corners[0].x, corners[0].y, corners[0].z,
				corners[1].x, corners[1].y, corners[1].z,
				corners[2].x, corners[2].y, corners[2].z]
			keys[key] = keys.get(key, 0) + 1
	return [keys, ground_area]


func _is_ground(p: Vector3) -> bool:
	return absf(p.y - CLIP) < 0.0001


func _configure(gen: Node, chunks: int, occluders: bool, img: Image) -> void:
	gen.set("mesh_size", MESH_SIZE)
	gen.set("grid_vertices", GRID)
	gen.set("height_scale", HEIGHT_SCALE)
	gen.set("base_height", BASE_HEIGHT)
	gen.set("block_inset", INSET)
	gen.set("clip_below_height", CLIP)
	gen.set("build_mode", 0)
	gen.set("sample_filter", 1)
	gen.set("max_cells", GRID.x * GRID.y)
	gen.set("geometry_chunks", chunks)
	gen.set("generate_occluders", occluders)
	gen.set("generate_collision", true)
	gen.set("persist_in_scene", false)
	# height_image is a bound method, not an inspector property.
	gen.call("set_height_image", img)


func _collect_meshes(node: Node, out: Array) -> void:
	if node is MeshInstance3D:
		out.append(node.mesh)
	for child in node.get_children():
		_collect_meshes(child, out)


func _count_of_type(node: Node, type_name: StringName, acc: Array) -> void:
	if node.is_class(type_name):
		acc.append(node)
	for child in node.get_children():
		_count_of_type(child, type_name, acc)


func _main() -> void:
	if not ClassDB.class_exists(&"ProcCityGenerator"):
		printerr("ProcCityGenerator missing — extension not loaded")
		_failures += 1
		return

	var img := _height_image()
	var host := Node3D.new()
	root.add_child(host)

	# --- baseline: one mesh -------------------------------------------------
	var single: Node = ClassDB.instantiate(&"ProcCityGenerator")
	host.add_child(single)
	_configure(single, 1, false, img)
	single.call("build_geometry")
	var single_root: Node = single.get_node_or_null("GeneratedCity")
	_check(single_root is MeshInstance3D, "unchunked city is still a bare MeshInstance3D")
	var single_meshes: Array = []
	_collect_meshes(single_root, single_meshes)
	var baseline_sig := _block_signature(single_meshes[0] as ArrayMesh)
	var want: Dictionary = baseline_sig[0]
	var want_ground: float = baseline_sig[1]
	_check(want.size() > 0, "baseline mesh has block triangles (%d unique)" % want.size())

	# --- chunked ------------------------------------------------------------
	var chunked: Node = ClassDB.instantiate(&"ProcCityGenerator")
	host.add_child(chunked)
	_configure(chunked, 3, true, img)
	chunked.call("build_geometry")
	var chunk_root: Node = chunked.get_node_or_null("GeneratedCity")
	_check(chunk_root != null and not (chunk_root is MeshInstance3D),
			"chunked city is a container, not a single mesh")

	var chunk_meshes: Array = []
	_collect_meshes(chunk_root, chunk_meshes)
	_check(chunk_meshes.size() == 9, "3x3 produced 9 chunk meshes (got %d)" % chunk_meshes.size())

	var got := {}
	var got_ground := 0.0
	for m in chunk_meshes:
		var sig := _block_signature(m as ArrayMesh)
		var chunk_keys: Dictionary = sig[0]
		got_ground += sig[1] as float
		for k in chunk_keys:
			got[k] = got.get(k, 0) + chunk_keys[k]
	_check(got.size() == want.size(),
			"chunked union has the same block triangle set (%d vs %d)" % [got.size(), want.size()])
	var mismatched := 0
	for k in want:
		if got.get(k, 0) != want[k]:
			mismatched += 1
	_check(mismatched == 0,
			"every baseline block triangle appears the same number of times (%d off)" % mismatched)
	_check(absf(got_ground - want_ground) < 0.01,
			"the chunk ground patches tile the same area (%.1f vs %.1f)" % [got_ground, want_ground])

	# Each chunk must own a tight AABB — that is the entire point of chunking.
	var baseline_mesh: ArrayMesh = single_meshes[0]
	var whole: AABB = baseline_mesh.get_aabb()
	var biggest := 0.0
	for m in chunk_meshes:
		var chunk_mesh: ArrayMesh = m
		biggest = maxf(biggest, chunk_mesh.get_aabb().size.x)
	_check(biggest < whole.size.x * 0.75,
			"chunk AABBs are tighter than the whole city (%.1f vs %.1f)" % [biggest, whole.size.x])

	var occluders: Array = []
	_count_of_type(chunk_root, &"OccluderInstance3D", occluders)
	_check(occluders.size() == chunk_meshes.size(),
			"one occluder per chunk (%d of %d)" % [occluders.size(), chunk_meshes.size()])
	var occluder_ok := true
	for o in occluders:
		if o.occluder == null or o.occluder.get_vertices().is_empty():
			occluder_ok = false
	_check(occluder_ok, "every occluder carries geometry")

	var bodies: Array = []
	_count_of_type(chunk_root, &"StaticBody3D", bodies)
	_check(bodies.size() == chunk_meshes.size(),
			"one collision body per chunk (%d of %d)" % [bodies.size(), chunk_meshes.size()])

	# --- query API ----------------------------------------------------------
	var cells: Dictionary = chunked.call("get_cell_heights")
	var cols: int = cells["columns"]
	var heights: PackedFloat32Array = cells["heights"]
	var origin: Vector2 = cells["origin"]
	var cell_size: Vector2 = cells["cell_size"]

	# Pick a tall cell and probe its centre.
	var tall := 0
	for i in heights.size():
		if heights[i] > heights[tall]:
			tall = i
	var ti := tall % cols
	var tj := tall / cols
	var centre := Vector3(
			origin.x + (float(ti) + 0.5) * cell_size.x,
			0.0,
			origin.y + (float(tj) + 0.5) * cell_size.y)

	var sampled: float = chunked.call("sample_city_height", centre)
	_check(absf(sampled - heights[tall]) < 0.001,
			"sample_city_height matches the cell grid (%.3f vs %.3f)" % [sampled, heights[tall]])

	_check(chunked.call("is_point_inside_block",
			Vector3(centre.x, heights[tall] * 0.5, centre.z)),
			"a point inside the tallest tower reads as blocked")
	_check(not chunked.call("is_point_inside_block",
			Vector3(centre.x, heights[tall] + 5.0, centre.z)),
			"a point above the roof reads as clear")
	# The inset leaves a street; the cell edge must not be inside the block.
	_check(not chunked.call("is_point_inside_block",
			Vector3(origin.x + float(ti) * cell_size.x + 0.01, heights[tall] * 0.5,
					origin.y + float(tj) * cell_size.y + 0.01)),
			"the street inside an inset cell reads as clear")
	_check(chunked.call("sample_city_height", Vector3(9999.0, 0.0, 9999.0)) == 0.0,
			"a point outside the field samples 0")

	var cleared: Vector3 = chunked.call("find_clear_point", centre, 200.0)
	_check(chunked.call("sample_city_height", cleared) <= CLIP,
			"find_clear_point lands on a cell at or below the clip height")
	_check(cleared.distance_to(centre) > 0.0, "find_clear_point moved off the tower")
