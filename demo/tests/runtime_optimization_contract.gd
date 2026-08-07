# Runtime-only performance contracts: material maps may be smaller than the
# topology-driving height map, mipmaps are authored off-thread, and the compact
# cell-height cache survives releasing full source Images.
# Run: godot --headless --path demo --script res://tests/runtime_optimization_contract.gd
extends SceneTree

const SOURCE_SIZE := 128
const MATERIAL_SIZE := 32

var failures := 0


func _init() -> void:
	_run.call_deferred()


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok: " + label)
	else:
		failures += 1
		push_error("check failed: " + label)


func _run() -> void:
	var params := GoplacementxParams.new()
	params.resolution = SOURCE_SIZE
	params.iterations = 20
	params.sprites_enabled = false
	params.seed = 424242
	params.randomize_seed = false

	var gen := ProcCityGenerator.new()
	root.add_child(gen)
	gen.params = params
	gen.grid_vertices = Vector2i(9, 9)
	gen.persist_in_scene = false
	gen.keep_intermediate_png = false
	gen.texture_mode = ProcCityGenerator.TEX_SHARED
	gen.material_max_size = MATERIAL_SIZE
	gen.texture_filter = ProcCityGenerator.TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC

	var outcome := {"done": false}
	gen.all_finished.connect(func() -> void:
		outcome["done"] = true)
	gen.generation_failed.connect(func(stage: String, message: String) -> void:
		push_error("generation failed at %s: %s" % [stage, message])
		failures += 1
		outcome["done"] = true)
	gen.generate_all()
	# The completed material must use the same filter snapshot that decided its
	# worker-side mip policy, even if the inspector value changes mid-flight.
	gen.texture_filter = ProcCityGenerator.TEXTURE_FILTER_LINEAR
	var deadline := Time.get_ticks_msec() + 30_000
	while not outcome["done"]:
		await process_frame
		if Time.get_ticks_msec() >= deadline:
			push_error("generation timed out")
			failures += 1
			break

	var height := gen.get_height_image()
	_check(height != null and height.get_width() == SOURCE_SIZE,
			"height map preserves topology resolution")
	var city := gen.get_node_or_null("GeneratedCity") as MeshInstance3D
	var material := city.material_override as BaseMaterial3D if city != null else null
	_check(material != null, "runtime material installed")
	if material != null:
		var albedo := material.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) as ImageTexture
		var normal := material.get_texture(BaseMaterial3D.TEXTURE_NORMAL) as ImageTexture
		var roughness := material.get_texture(BaseMaterial3D.TEXTURE_ROUGHNESS) as ImageTexture
		_check(albedo != null and albedo.get_width() == MATERIAL_SIZE,
				"albedo upload obeys material_max_size")
		_check(normal != null and normal.get_width() == MATERIAL_SIZE,
				"normal upload obeys material_max_size")
		_check(albedo != null and albedo.get_image().has_mipmaps(),
				"albedo upload carries mipmaps")
		_check(normal != null and normal.get_image().has_mipmaps(),
				"normal upload carries renormalized mipmaps")
		_check(roughness != null and roughness.get_width() == MATERIAL_SIZE,
				"shared height roughness copy obeys material_max_size")
		_check(roughness != null and roughness.get_image().has_mipmaps(),
				"shared height roughness copy carries mipmaps")
		_check(material.texture_filter == BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC,
				"material uses mipmapped anisotropic filtering")

	# Channel maps are resized individually but do not waste memory on mip chains
	# that composition discards; the worker-produced RGB albedo owns the mips.
	outcome["done"] = false
	gen.texture_mode = ProcCityGenerator.TEX_CHANNELS
	gen.texture_filter = ProcCityGenerator.TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC
	gen.generate_material()
	deadline = Time.get_ticks_msec() + 30_000
	while not outcome["done"]:
		await process_frame
		if Time.get_ticks_msec() >= deadline:
			push_error("channel material generation timed out")
			failures += 1
			break
	material = city.material_override as BaseMaterial3D if city != null else null
	if material != null:
		var channel_albedo := material.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) as ImageTexture
		var channel_roughness := material.get_texture(BaseMaterial3D.TEXTURE_ROUGHNESS) as ImageTexture
		_check(channel_albedo != null and channel_albedo.get_width() == MATERIAL_SIZE,
				"worker-composed channel albedo obeys material_max_size")
		_check(channel_albedo != null and channel_albedo.get_image().has_mipmaps(),
				"worker-composed channel albedo carries mipmaps")
		_check(channel_roughness != null and channel_roughness.get_image().has_mipmaps(),
				"channel roughness carries mipmaps")

	var cells := gen.get_cell_heights()
	gen.release_source_images()
	_check(gen.get_height_image() == null, "source height released")
	_check(gen.get_cell_heights() == cells, "cell-height cache remains available")

	gen.queue_free()
	await process_frame
	if failures == 0:
		print("runtime_optimization_contract: OK")
	quit(1 if failures > 0 else 0)
