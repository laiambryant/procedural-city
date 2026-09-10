extends Node3D

## Runtime showcase: builds three cities up front, each with its own seed and
## palette, then plays them back on a fixed 15 s timeline while the camera
## orbits and the finished blocks rise out of the street. The timeline is a
## pure function of one clock, so `scripts/record_showcase.py` can pose it
## frame by frame and get the same GIF every run.

const ORBIT_PERIOD := 34.0
const RISE_SECONDS := 1.15
const RISE_STAGGER := 0.055
const CYCLE_SECONDS := 5.0
const SEEDS := [424242, 73129, 917203]

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

var _cities: Array[Node3D] = []
var _tiles: Array[Array] = []
var _captions: PackedStringArray = []
var _time := 0.0
var _active := -1
var _capture_dir := ""
var _capture_fps := 16
var _preview_time := -1.0

func _ready() -> void:
	# Godot starts _process as soon as the method exists. Left running, the
	# timeline would advance through the build and then overwrite every pose
	# the capture loop sets, one frame after it sets it.
	set_process(false)
	_read_cmdline()
	_fit_readout_to_viewport()
	generator.generation_failed.connect(_on_generation_failed)
	for index in SEEDS.size():
		await _build_city(index)
	generator.release_source_images()
	if _capture_dir.is_empty():
		set_process(true)
	else:
		DirAccess.make_dir_recursive_absolute(_capture_dir)
		await _capture()

func _read_cmdline() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture-dir="):
			_capture_dir = arg.trim_prefix("--capture-dir=")
		elif arg.begins_with("--capture-fps="):
			_capture_fps = maxi(1, int(arg.trim_prefix("--capture-fps=")))
		elif arg.begins_with("--preview-time="):
			_preview_time = float(arg.trim_prefix("--preview-time="))

## The readout is authored against a 720p viewport; scale it so it stays
## readable when the scene is rendered (or recorded) at a larger size.
func _fit_readout_to_viewport() -> void:
	var scale := get_viewport().get_visible_rect().size.y / 720.0
	readout.add_theme_font_size_override("font_size", roundi(17 * scale))
	readout.add_theme_constant_override("outline_size", roundi(6 * scale))
	readout.offset_left = 28 * scale
	readout.offset_right = 620 * scale
	readout.offset_top = -58 * scale
	readout.offset_bottom = -24 * scale

## One generation pass, reparented off the generator so the next pass cannot
## overwrite it. Cities are hidden until the timeline calls for them.
func _build_city(index: int) -> void:
	readout.text = "Procedural City · building city %d of %d…" % [index + 1, SEEDS.size()]
	generator.params.gradient_colors = palettes[index]
	generator.params.seed = SEEDS[index]
	var started_usec := Time.get_ticks_usec()
	generator.generate_all()
	await generator.all_finished
	var elapsed_ms := (Time.get_ticks_usec() - started_usec) / 1000.0
	var city := generator.get_node_or_null("GeneratedCity") as Node3D
	if city == null:
		push_error("[demo] city %d produced no geometry" % (index + 1))
		return
	generator.remove_child(city)
	generator.add_sibling(city)
	city.position = generator.position
	city.visible = false
	_cities.append(city)
	_tiles.append(_city_tiles(city))
	_captions.append("Procedural City · city %d · %d×%d cells · %d px map · %.0f ms" % [
		index + 1,
		generator.grid_vertices.x - 1,
		generator.grid_vertices.y - 1,
		generator.params.resolution,
		elapsed_ms,
	])

func _process(delta: float) -> void:
	_time = fmod(_time + delta, CYCLE_SECONDS * SEEDS.size())
	_pose(_time)

## The whole showcase as a function of time: which city is up, how far it has
## risen, and where the camera is. No tweens and no accumulated state, so a
## captured frame and a played frame at the same time are identical.
func _pose(time: float) -> void:
	if _cities.is_empty():
		return
	var index := int(time / CYCLE_SECONDS) % _cities.size()
	var local := fmod(time, CYCLE_SECONDS)
	if index != _active:
		if _active >= 0:
			_cities[_active].visible = false
		_active = index
		_cities[index].visible = true
		readout.text = _captions[index]
	var tiles: Array = _tiles[index]
	for i in tiles.size():
		var risen := clampf((local - i * RISE_STAGGER) / RISE_SECONDS, 0.0, 1.0)
		tiles[i].scale.y = maxf(ease(risen, 0.35), 0.001)
	camera_rig.rotation.y = TAU / ORBIT_PERIOD * time

func _capture() -> void:
	var total := 1 if _preview_time >= 0.0 else int(CYCLE_SECONDS * _cities.size() * _capture_fps)
	for frame in total:
		var time := _preview_time
		if _preview_time < 0.0:
			time = fmod(2.0 + float(frame) / _capture_fps, CYCLE_SECONDS * _cities.size())
		_pose(time)
		await RenderingServer.frame_post_draw
		var path := _capture_dir.path_join("frame_%04d.png" % frame)
		if get_viewport().get_texture().get_image().save_png(path) != OK:
			push_error("[demo] capture failed: " + path)
			get_tree().quit(1)
			return
		if frame % _capture_fps == 0:
			print("[demo] captured %d / %d" % [frame, total])
	print("[demo] capture complete: %d frames" % total)
	get_tree().quit()

## Chunked geometry arrives as a container of meshes; a single mesh arrives on
## its own. Either way the rise animates the pieces, staggered.
func _city_tiles(city: Node3D) -> Array:
	if city is VisualInstance3D:
		return [city]
	var tiles: Array = []
	for child in city.get_children():
		if child is Node3D:
			tiles.append(child)
	return tiles

func _on_generation_failed(stage: String, message: String) -> void:
	push_error("[demo] %s failed: %s" % [stage, message])
	readout.text = "%s failed: %s" % [stage, message]
	if not _capture_dir.is_empty():
		get_tree().quit(1)
