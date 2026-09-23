# carve_rects contract: rectangles flattened in the displacement field leave
# open ground in the cell heights, the clear-point queries and the collision
# body, while cells outside every rect keep the uncarved heights.
# Run: godot --headless --path demo --script res://tests/carve_rects_contract.gd
extends SceneTree

const MESH_SIZE := Vector2(80.0, 80.0)
const GRID := Vector2i(17, 17)
## Plaza in the north-west quadrant plus a full-width avenue through the
## middle, both on 5 m cell boundaries.
const PLAZA := Rect2(-30.0, -30.0, 20.0, 20.0)
const AVENUE := Rect2(-40.0, -5.0, 80.0, 10.0)

var failures := 0


func _init() -> void:
	_run.call_deferred()


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		failures += 1
		push_error("check failed: " + label)


func _make_generator(carves: Array[Rect2]) -> ProcCityGenerator:
	var params := GoplacementxParams.new()
	params.resolution = 256
	params.iterations = 40
	params.sprites_enabled = false
	params.seed = 90210
	params.randomize_seed = false
	# A bright background keeps nearly every uncarved cell standing, so the
	# carved ones are unambiguous.
	params.background_brightness = 200

	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.params = params
	gen.mesh_size = MESH_SIZE
	gen.grid_vertices = GRID
	gen.height_scale = 20.0
	gen.persist_in_scene = false
	gen.keep_intermediate_png = false
	gen.generate_collision = true
	# Open ground is anything at or below the clip, as in a sunk skyline.
	gen.clip_below_height = 0.5
	gen.carve_rects = carves
	return gen


func _generate(gen: ProcCityGenerator) -> void:
	var outcome := {"done": false}
	gen.all_finished.connect(func() -> void:
		outcome["done"] = true)
	gen.generation_failed.connect(func(stage: String, message: String) -> void:
		push_error("generation failed at %s: %s" % [stage, message])
		failures += 1
		outcome["done"] = true)
	gen.generate_all()
	var deadline := Time.get_ticks_msec() + 30_000
	while not outcome["done"]:
		await process_frame
		if Time.get_ticks_msec() >= deadline:
			push_error("generation timed out")
			failures += 1
			return


func _cell_inside(data: Dictionary, i: int, j: int, rect: Rect2) -> bool:
	var size: Vector2 = data["cell_size"]
	var origin: Vector2 = data["origin"]
	var cell := Rect2(origin + Vector2(i, j) * size, size)
	return rect.encloses(cell)


func _run() -> void:
	var carved := _make_generator([PLAZA, AVENUE])
	var plain := _make_generator([])
	# Keep the reference city's collision out of the plaza ray below.
	plain.position = Vector3(1000.0, 0.0, 0.0)
	_check(carved.carve_rects.size() == 2, "carve_rects round-trips through the property")
	await _generate(carved)
	await _generate(plain)

	var cut: Dictionary = carved.get_cell_heights()
	var ref: Dictionary = plain.get_cell_heights()
	var cut_heights: PackedFloat32Array = cut.get("heights", PackedFloat32Array())
	var ref_heights: PackedFloat32Array = ref.get("heights", PackedFloat32Array())
	_check(not cut_heights.is_empty() and cut_heights.size() == ref_heights.size(),
			"carved and plain grids share a shape")

	var inside := 0
	var inside_flat := 0
	var outside := 0
	var outside_same := 0
	var columns := int(cut.get("columns", 0))
	for j in int(cut.get("rows", 0)):
		for i in columns:
			var index := j * columns + i
			var near_edge := false
			for rect in [PLAZA, AVENUE]:
				if _cell_inside(cut, i, j, rect.grow(0.01)):
					inside += 1
					if cut_heights[index] <= 0.0001:
						inside_flat += 1
				elif rect.grow(6.0).has_point(Vector2(cut["origin"]) + (Vector2(i, j) + Vector2(0.5, 0.5)) * Vector2(cut["cell_size"])):
					near_edge = true
			if near_edge or _cell_inside(cut, i, j, PLAZA.grow(0.01)) or _cell_inside(cut, i, j, AVENUE.grow(0.01)):
				continue
			outside += 1
			if is_equal_approx(cut_heights[index], ref_heights[index]):
				outside_same += 1
	_check(inside > 0 and inside_flat == inside, "every cell inside a carve is flat (%d/%d)" % [inside_flat, inside])
	_check(outside > 0 and outside_same == outside, "cells away from carves are untouched (%d/%d)" % [outside_same, outside])

	var plaza_centre := PLAZA.get_center()
	_check(not carved.is_point_inside_block(Vector3(plaza_centre.x, 0.0, plaza_centre.y)),
			"plaza centre reads as open ground")
	_check(carved.sample_city_height(Vector3(0.0, 0.0, 0.0)) <= 0.0001, "avenue samples at street level")
	var clear := carved.find_clear_point(Vector3(0.0, 0.0, 1.0), 4.0)
	_check(AVENUE.has_point(Vector2(clear.x, clear.z)), "clear-point search stays on the open avenue")

	# Collision must agree with the render: a downward ray over the plaza hits
	# nothing standing above the street.
	await physics_frame
	await physics_frame
	var space := carved.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(
			Vector3(plaza_centre.x, 50.0, plaza_centre.y), Vector3(plaza_centre.x, 0.5, plaza_centre.y))
	var hit := space.intersect_ray(query)
	_check(hit.is_empty(), "no collision rises above the carved plaza")

	carved.queue_free()
	plain.queue_free()
	await process_frame
	if failures == 0:
		print("carve_rects_contract: OK")
	quit(1 if failures > 0 else 0)
