extends SceneTree
# Contract for the inspector's usability rules: a property that the current
# configuration cannot act on must come back read-only, so the editor greys it
# out instead of offering a knob that does nothing.
# Run: godot --headless --path demo --script tests/inspector_rules.gd

var fails := 0

func check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		fails += 1
		printerr("FAIL  ", label)

func is_read_only(object: Object, name: String) -> bool:
	for property in object.get_property_list():
		if property["name"] == name:
			return (property["usage"] & PROPERTY_USAGE_READ_ONLY) != 0
	printerr("       (no such property: %s)" % name)
	fails += 1
	return false

func expect(object: Object, name: String, read_only: bool, why: String) -> void:
	check(is_read_only(object, name) == read_only, "%s %s (%s)" % [
		name, "read-only" if read_only else "editable", why,
	])

func _init() -> void:
	var gen := ProcCityGenerator.new()
	gen.params = GoplacementxParams.new()

	gen.generation_mode = ProcCityGenerator.GEN_GPU_NATIVE
	expect(gen, "use_gpu_server", true, "native GPU reuses its own device")
	expect(gen, "binary_path_override", true, "no CLI is involved")
	expect(gen, "auto_download_binary", true, "no CLI is involved")
	expect(gen, "keep_intermediate_png", true, "native maps never touch disk")

	gen.generation_mode = ProcCityGenerator.GEN_GPU_LEGACY
	expect(gen, "use_gpu_server", false, "legacy GPU can keep a server alive")
	expect(gen, "binary_path_override", false, "legacy resolves a binary")

	gen.generation_mode = ProcCityGenerator.GEN_CPU_LEGACY
	expect(gen, "use_gpu_server", true, "the server is a GPU-only path")
	expect(gen, "output_dir", false, "legacy writes its maps somewhere")

	gen.build_mode = ProcCityGenerator.BUILD_ARRAY_MESH
	expect(gen, "geometry_chunks", false, "chunking is an ArrayMesh feature")
	expect(gen, "clip_below_height", false, "clipping is an ArrayMesh feature")
	expect(gen, "ao_strength", false, "ArrayMesh bakes vertex style")
	expect(gen, "hive_warp", true, "not building a hive")
	expect(gen, "gridmap_level_height", true, "not building a gridmap")

	gen.build_mode = ProcCityGenerator.BUILD_HEX
	expect(gen, "hive_warp", false, "HexHive uses the hive knobs")
	expect(gen, "ao_strength", false, "HexHive bakes vertex style too")
	expect(gen, "geometry_chunks", true, "chunking is ArrayMesh only")
	gen.hive_rim_boost = 0.0
	expect(gen, "hive_rim_falloff", true, "a zero boost has nothing to fall off")
	gen.hive_rim_boost = 4.0
	expect(gen, "hive_rim_falloff", false, "the rim is active")

	gen.build_mode = ProcCityGenerator.BUILD_GRIDMAP
	expect(gen, "gridmap_level_height", false, "GridMap uses the gridmap knobs")
	expect(gen, "gridmap_item_id", true, "no library to pick an item from")
	gen.gridmap_mesh_library = MeshLibrary.new()
	expect(gen, "gridmap_item_id", false, "a library is supplied")
	expect(gen, "ao_strength", true, "GridMap bakes no vertex style")

	gen.build_mode = ProcCityGenerator.BUILD_MULTIMESH
	expect(gen, "color_variation", true, "MultiMesh bakes no vertex style")

	gen.persist_in_scene = false
	expect(gen, "external_resource_dir", true, "nothing is persisted")
	gen.persist_in_scene = true
	expect(gen, "external_resource_dir", false, "the city is saved with the scene")

	gen.generate_collision = false
	expect(gen, "collision_layer", true, "no body is generated")
	gen.generate_collision = true
	expect(gen, "collision_layer", false, "the body needs a layer")

	gen.texture_mode = ProcCityGenerator.TEX_SINGLE
	expect(gen, "roughness", false, "no roughness map, the scalar rules")
	gen.texture_mode = ProcCityGenerator.TEX_SHARED
	expect(gen, "roughness", true, "a roughness map overrides the scalar")

	var params := GoplacementxParams.new()
	params.palette_preset = 0
	expect(params, "gradient_colors", false, "Custom edits the gradient")
	params.palette_preset = 3
	expect(params, "gradient_colors", true, "a preset supplies the gradient")

	params.randomize_seed = true
	expect(params, "seed", true, "the seed is drawn per run")
	params.randomize_seed = false
	expect(params, "seed", false, "the seed is pinned by hand")

	params.out_width = 0
	params.out_height = 0
	expect(params, "resolution", false, "no explicit output size")
	params.out_width = 1024
	params.out_height = 512
	expect(params, "resolution", true, "an explicit size overrides it")

	params.rect_enabled = false
	expect(params, "rect_scale", true, "the rect layer is off")
	expect(params, "rect_enabled", false, "the toggle itself stays editable")
	params.rect_enabled = true
	expect(params, "rect_scale", false, "the rect layer is on")

	params.sprites_enabled = false
	expect(params, "sprite_packs", true, "sprites are off")
	expect(params, "sprites_rotation_enabled", true, "sprites are off")
	params.sprites_enabled = true
	expect(params, "sprite_packs", false, "sprites are on")

	gen.free()

	print("---")
	if fails == 0:
		print("ALL TESTS PASSED")
	else:
		printerr(str(fails) + " TEST(S) FAILED")
	quit(1 if fails > 0 else 0)
