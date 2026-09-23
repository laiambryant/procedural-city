extends Node3D

## Runtime showcase: builds four cities up front, each with its own seed,
## palette and sky, then plays them back on a fixed 20 s timeline while the
## camera orbits and the finished blocks rise out of the water. The timeline is a
## pure function of one clock, so `scripts/record_showcase.py` can pose it
## frame by frame and get the same GIF every run.

const ORBIT_PERIOD := 34.0
const RISE_SECONDS := 1.15
const RISE_STAGGER := 0.055
const CYCLE_SECONDS := 5.0
const SEEDS := [424242, 73129, 917203, 581337]

## One look per city: the albedo ramp the generator maps heights through, plus
## the sky, fog, light and water-grid colours that frame it. The first ramp is
## godisplacementx's default gradient.
var moods: Array[Dictionary] = [
	{
		name = "DisplacementX",
		palette = PackedColorArray([Color("00ffff"), Color("9500ff"), Color("ffe500")]),
		zenith = Color("080722"), horizon = Color("d93f86"), fog = Color("8a2c78"),
		cloud_lit = Color("e089c4"), cloud_mid = Color("6a2c80"), cloud_deep = Color("170d33"),
		sun = Color("ff9f8a"), fill = Color("5ad6ff"), grid = Color("00ffff"), stars = 2.5,
		clouds = 0.2,
	},
	{
		name = "Vaporwave",
		palette = PackedColorArray([
			Color("2d0b59"), Color("b967ff"), Color("ff71ce"), Color("01cdfe"), Color("fffb96"),
		]),
		zenith = Color("2b1f78"), horizon = Color("ffa7c4"), fog = Color("e48bb8"),
		cloud_lit = Color("fff0f6"), cloud_mid = Color("ffb3d4"), cloud_deep = Color("7a5ab8"),
		sun = Color("ffd2c0"), fill = Color("8fb8ff"), grid = Color("ff71ce"), stars = 0.4,
		clouds = 0.24,
	},
	{
		name = "Aurora",
		palette = PackedColorArray([
			Color("0b2545"), Color("13708a"), Color("05bfdb"), Color("00ffca"), Color("eafff7"),
		]),
		zenith = Color("020916"), horizon = Color("1f7d86"), fog = Color("15505e"),
		cloud_lit = Color("86d6cc"), cloud_mid = Color("2a6f78"), cloud_deep = Color("071a28"),
		sun = Color("b8f0ff"), fill = Color("39ffc0"), grid = Color("00ffca"), stars = 3.0,
		clouds = 0.18,
	},
	{
		name = "Solar Flare",
		palette = PackedColorArray([
			Color("240b36"), Color("7a1d4a"), Color("e8412c"), Color("ff9a1f"), Color("ffe9a8"),
		]),
		zenith = Color("23265e"), horizon = Color("ff8c42"), fog = Color("d86a3a"),
		cloud_lit = Color("fff0d6"), cloud_mid = Color("ffa45c"), cloud_deep = Color("6a3050"),
		sun = Color("ffb06a"), fill = Color("7a8cff"), grid = Color("ff6a00"), stars = 0.0,
		clouds = 0.24,
	},
]

@onready var generator: ProcCityGenerator = $ProcCityGenerator
@onready var camera_rig: Node3D = $CameraRig
@onready var readout: Label = $Hud/Readout
@onready var environment: Environment = $WorldEnvironment.environment
@onready var cloud_sky: ShaderMaterial = environment.sky.sky_material
@onready var water: ShaderMaterial = $Water.get_surface_override_material(0)
@onready var sun: DirectionalLight3D = $DirectionalLight3D
@onready var fill: DirectionalLight3D = $FillLight

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
	generator.params.gradient_colors = moods[index].palette
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
	_captions.append("Procedural City · %s · %d×%d cells · %d px map · %.0f ms" % [
		moods[index].name,
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
	cloud_sky.set_shader_parameter("showcase_time", time)
	water.set_shader_parameter("showcase_time", time)
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
		_apply_mood(moods[index])
	var tiles: Array = _tiles[index]
	for i in tiles.size():
		var risen := clampf((local - i * RISE_STAGGER) / RISE_SECONDS, 0.0, 1.0)
		tiles[i].scale.y = maxf(ease(risen, 0.35), 0.001)
	camera_rig.rotation.y = TAU / ORBIT_PERIOD * time

func _apply_mood(mood: Dictionary) -> void:
	cloud_sky.set_shader_parameter("sky_zenith", mood.zenith)
	cloud_sky.set_shader_parameter("sky_horizon", mood.horizon)
	cloud_sky.set_shader_parameter("ground_horizon", mood.horizon)
	cloud_sky.set_shader_parameter("ground_bottom", mood.zenith)
	cloud_sky.set_shader_parameter("floss_lit", mood.cloud_lit)
	cloud_sky.set_shader_parameter("floss_mid", mood.cloud_mid)
	cloud_sky.set_shader_parameter("floss_deep", mood.cloud_deep)
	cloud_sky.set_shader_parameter("ground_bounce", mood.cloud_mid)
	cloud_sky.set_shader_parameter("cubemap_cloud_tint", mood.cloud_mid)
	cloud_sky.set_shader_parameter("sun_disc_color", mood.sun)
	cloud_sky.set_shader_parameter("star_energy", mood.stars)
	cloud_sky.set_shader_parameter("coverage", mood.clouds)
	environment.fog_light_color = mood.horizon.lerp(mood.fog, 0.5)
	sun.light_color = mood.sun
	fill.light_color = mood.fill
	water.set_shader_parameter("grid_color", mood.grid)

func _capture() -> void:
	var total := 1 if _preview_time >= 0.0 else int(CYCLE_SECONDS * _cities.size() * _capture_fps)
	for frame in total:
		var time := _preview_time
		if _preview_time < 0.0:
			time = fmod(2.0 + float(frame) / _capture_fps, CYCLE_SECONDS * _cities.size())
		_pose(time)
		# Settle sky radiance and screen-space effects before the first still.
		if frame == 0:
			for _warmup in 8:
				await RenderingServer.frame_post_draw
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
