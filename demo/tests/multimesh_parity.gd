# Parity oracle for the buffer-based multimesh path: rebuilds the same tiny
# grid with per-instance set_instance_transform and diffs the two buffers.
# Run: godot --headless --path demo -s res://tests/multimesh_parity.gd
extends SceneTree

const IMG_SIZE := 64
const GRID := Vector2i(4, 4)
const MESH_SIZE := Vector2(10, 10)
const HEIGHT_SCALE := 2.0
const BASE_HEIGHT := 0.3


func _init() -> void:
	_main()


func _make_height_image() -> Image:
	var bytes := PackedByteArray()
	bytes.resize(IMG_SIZE * IMG_SIZE)
	var v := 123456789
	for i in IMG_SIZE * IMG_SIZE:
		v = (v * 1103515245 + 12345) & 0x7FFFFFFF
		bytes[i] = (v >> 16) & 0xFF
	return Image.create_from_data(IMG_SIZE, IMG_SIZE, false, Image.FORMAT_L8, bytes)


func _main() -> void:
	var img := _make_height_image()
	var mesher := HeightmapMesher.new()
	var mm: MultiMesh = mesher.build_multimesh(img, MESH_SIZE, GRID, HEIGHT_SCALE, BASE_HEIGHT,
			HeightmapMesher.FILTER_BOX_AVERAGE)

	var cols := GRID.x - 1
	var rows := GRID.y - 1
	var cw := MESH_SIZE.x / cols
	var cd := MESH_SIZE.y / rows
	var ox := -MESH_SIZE.x * 0.5
	var oz := -MESH_SIZE.y * 0.5

	var buf := mm.buffer
	if buf.size() != cols * rows * 12:
		print("bad buffer size: %d" % buf.size())
		quit(1)
		return
	var mismatches := 0
	var idx := 0
	for j in rows:
		for i in cols:
			var v := mesher.sample_height(img, i, j, cols, rows, HeightmapMesher.FILTER_BOX_AVERAGE)
			var h := maxf(BASE_HEIGHT + v * HEIGHT_SCALE, 0.0001)
			var cx := ox + (i + 0.5) * cw
			var cz := oz + (j + 0.5) * cd
			var expected := [cw, 0.0, 0.0, cx, 0.0, h, 0.0, h * 0.5, 0.0, 0.0, cd, cz]
			for slot in 12:
				var got := buf[idx * 12 + slot]
				var want: float = expected[slot]
				if absf(got - want) > 1e-5 * maxf(1.0, absf(want)):
					if mismatches < 12:
						print("inst %d slot %d: cpp=%.9f ref=%.9f" % [idx, slot, got, want])
					mismatches += 1
			idx += 1
	print("parity: %d mismatches over %d instances" % [mismatches, idx])
	quit(0 if mismatches == 0 else 1)
