extends Node3D

## Runtime showcase: generates one city after another, each with its own seed
## and palette, raising the finished blocks out of the ground while the camera
## orbits. In the editor, drive the same node with the inspector buttons.

const ORBIT_PERIOD := 34.0
const RISE_SECONDS := 1.15
const RISE_STAGGER := 0.055
const HOLD_SECONDS := 1.4

var palettes: Array[PackedColorArray] = [
	PackedColorArray([
		Color("0a0d1c"), Color("2a2350"), Color("b5455f"), Color("ffc46b"), Color("fff2d0"),
	]),
	PackedColorArray([
		Color("07131a"), Color("0f4a52"), Color("2fb8a0"), Color("d6f5a3"), Color("fbffe3"),
	]),
	PackedColorArray([
		Color("120a18"), Color("46145c"), Color("d1345b"), Color("ff8c42"), Color("ffe9c9"),
	]),
]

@onready var generator: ProcCityGenerator = $ProcCityGenerator
@onready var camera_rig: Node3D = $CameraRig
@onready var readout: Label = $Hud/Readout

var _pass := 0
var _started_usec := 0

func _ready() -> void:
	generator.generation_failed.connect(_on_generation_failed)
	generator.all_finished.connect(_on_all_finished)
	_start_next_pass()

func _process(delta: float) -> void:
	camera_rig.rotate_y(TAU / ORBIT_PERIOD * delta)

func _start_next_pass() -> void:
	_pass += 1
	_apply_palette(palettes[(_pass - 1) % palettes.size()])
	_show("generating…")
	_started_usec = Time.get_ticks_usec()
	generator.generate_all()

func _apply_palette(colors: PackedColorArray) -> void:
	generator.params.gradient_colors = colors
	generator.params.palette_preset = 0

func _on_all_finished() -> void:
	var elapsed_ms := (Time.get_ticks_usec() - _started_usec) / 1000.0
	_show("%d×%d cells · %d px map · %.0f ms" % [
		generator.grid_vertices.x - 1,
		generator.grid_vertices.y - 1,
		generator.params.resolution,
		elapsed_ms,
	])
	_raise_city()
	await get_tree().create_timer(RISE_SECONDS + HOLD_SECONDS).timeout
	_start_next_pass()

func _on_generation_failed(stage: String, message: String) -> void:
	push_error("[demo] %s failed: %s" % [stage, message])
	_show("%s failed: %s" % [stage, message])

func _raise_city() -> void:
	var tiles := _city_tiles()
	if tiles.is_empty():
		return
	var rise := create_tween().set_parallel(true)
	for i in tiles.size():
		tiles[i].scale.y = 0.001
		rise.tween_property(tiles[i], "scale:y", 1.0, RISE_SECONDS) \
			.set_delay(i * RISE_STAGGER) \
			.set_trans(Tween.TRANS_CUBIC) \
			.set_ease(Tween.EASE_OUT)

func _city_tiles() -> Array[Node3D]:
	var tiles: Array[Node3D] = []
	var city := generator.get_node_or_null("GeneratedCity") as Node3D
	if city == null:
		return tiles
	if city is VisualInstance3D:
		tiles.append(city)
		return tiles
	for child in city.get_children():
		if child is Node3D:
			tiles.append(child)
	return tiles

func _show(text: String) -> void:
	readout.text = "Procedural City · city %d · %s" % [_pass, text]
