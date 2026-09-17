extends Node
## Bakes the two 3D noise volumes the candy-floss sky marches through.
##   godot --path . res://environment/bake_cloud_noise.tscn      # WINDOWED. Not --headless.
##
## MUST BE BAKED WINDOWED — ImageTexture3D stores its data on the GPU, and
## it fooled the first version of this file, whose header confidently said the
## opposite. The GENERATION really is pure CPU work (Noise.get_seamless_image_3d
## on the main thread — NoiseTexture3D, the node-facing wrapper, is the one that
## threads and needs `await tex.changed`, and we never use it). But the STORAGE
## is not: ImageTexture3D keeps its texels on the RenderingServer, so both
## `get_data()` and the property getter ResourceSaver serialises through return
## an empty array under the dummy driver. Headless therefore writes a
## syntactically valid .res containing no image data, and the sky renders clear.
## The round-trip check at the bottom of _save is what catches it; the guard in
## _ready is what stops it happening in the first place.
##
## Two volumes, four octaves each, packed one octave per channel. That packing
## is the whole point: the raymarch evaluates density ~90 times per pixel, and
## four separate textures would be four fetches every time. Packed, it is one
## `textureLod` for the entire FBM (A. Schneider, "The Real-Time Volumetric
## Cloudscapes of Horizon: Zero Dawn", SIGGRAPH 2015).
##
##   cloud_base_noise.res    R  Perlin dilated by low-frequency Worley
##                           G  Worley  x8       (billow)
##                           B  Worley  x16
##                           A  Worley  x24
##
##   cloud_detail_noise.res  R  Worley  x12      (edge erosion)
##                           G  Worley  x24
##                           B  Worley  x40
##                           A  unused, 1.0
##
## Worley is INVERTED on every channel. Straight cellular distance is dark at
## the cell centres and bright at the seams, which erodes clouds into a net;
## inverted, each cell is a rounded lobe and the field reads as billows. That is
## the difference between a cauliflower silhouette and a sponge.
##
## Everything is generated SEAMLESS. The shader tiles these volumes across the
## sky, so a non-tiling volume puts a visible grid seam over the street.

const OUT_DIR := "res://environment"
const BASE_PATH := OUT_DIR + "/cloud_base_noise.res"
const DETAIL_PATH := OUT_DIR + "/cloud_detail_noise.res"

## 64^3 RGBA8 is 1 MiB per volume. 128^3 is eight times that for detail no one
## can see through a half-res march at dusk, and seamless generation is O(n^3)
## with a blend skirt on top — it turns a 3 second bake into most of a minute.
const SIZE := 64
## Cells across the volume, per channel. The base supplies broad billows;
## the higher-frequency detail channels erode their edges.
const BASE_CELLS := [8, 16, 24]
const DETAIL_CELLS := [12, 24, 40]
const PERLIN_CELLS := 4
const BASE_WORLEY_CELLS := 6

## get_seamless_image_3d blends a skirt of the volume back onto itself to make
## it tile. The default 0.1 leaves a soft band across the wrap on cellular
## noise, where a blended cell reads as a smeared one; 0.2 costs generation
## time and hides it.
const SKIRT := 0.2

var _failed := false


func _ready() -> void:
	## Refuse rather than write an empty volume. See the header: headless does
	## not fail here, it succeeds quietly and ships a clear sky.
	if DisplayServer.get_name() == "headless":
		push_error("build_cloud_noise must run WINDOWED — ImageTexture3D "
			+ "serialises through the RenderingServer and the dummy driver "
			+ "returns no texels. Drop --headless and run it again.")
		get_tree().quit(1)
		return

	var started := Time.get_ticks_msec()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	_bake_base()
	if _failed:
		get_tree().quit(1)
		return
	_bake_detail()
	if _failed:
		get_tree().quit(1)
		return

	print("cloud noise baked in %d ms" % (Time.get_ticks_msec() - started))
	get_tree().quit(0)


## R is the SHAPE channel and it is not plain Perlin. Perlin alone gives smoky,
## wispy clouds — the connective-tissue look — because its extrema are smooth
## saddles. Dilating it by a low-frequency Worley field pulls the mass into
## rounded lobes while keeping Perlin's large-scale variation, which is what
## makes a cumulus read as stacked scoops. GBA are rising Worley octaves that
## the shader folds into an FBM to erode R with.
func _bake_base() -> void:
	var perlin := _noise_perlin(PERLIN_CELLS, 1201)
	var worley_low := _worley_slices(BASE_WORLEY_CELLS, 1301)
	var r := _combine_perlin_worley(_slices(perlin), worley_low)
	var g := _worley_slices(BASE_CELLS[0], 1401)
	var b := _worley_slices(BASE_CELLS[1], 1501)
	var a := _worley_slices(BASE_CELLS[2], 1601)
	_save(_pack(r, g, b, a), BASE_PATH)


## Pure erosion. This volume is only ever subtracted from the base shape at the
## cloud boundary, so it carries no low frequencies at all — any large-scale
## structure in here would punch holes through the middle of a cloud instead of
## fraying its edge.
func _bake_detail() -> void:
	var r := _worley_slices(DETAIL_CELLS[0], 1701)
	var g := _worley_slices(DETAIL_CELLS[1], 1801)
	var b := _worley_slices(DETAIL_CELLS[2], 1901)
	_save(_pack(r, g, b, []), DETAIL_PATH)


func _noise_perlin(cells: int, seed_value: int) -> FastNoiseLite:
	var n := FastNoiseLite.new()
	n.noise_type = FastNoiseLite.TYPE_PERLIN
	n.seed = seed_value
	n.frequency = float(cells) / float(SIZE)
	n.fractal_type = FastNoiseLite.FRACTAL_FBM
	n.fractal_octaves = 3
	return n


func _noise_worley(cells: int, seed_value: int) -> FastNoiseLite:
	var n := FastNoiseLite.new()
	n.noise_type = FastNoiseLite.TYPE_CELLULAR
	n.seed = seed_value
	n.frequency = float(cells) / float(SIZE)
	n.cellular_distance_function = FastNoiseLite.DISTANCE_EUCLIDEAN
	n.cellular_return_type = FastNoiseLite.RETURN_DISTANCE
	## One octave. Fractal cellular stacks seams on seams and the lobes stop
	## reading as lobes; the FBM we want happens in the shader, across channels.
	n.fractal_type = FastNoiseLite.FRACTAL_NONE
	return n


## `invert = true` is what turns cellular DISTANCE into billows — see the header.
func _worley_slices(cells: int, seed_value: int) -> Array:
	return _slices(_noise_worley(cells, seed_value), true)


func _slices(noise: FastNoiseLite, invert: bool = false) -> Array:
	return noise.get_seamless_image_3d(SIZE, SIZE, SIZE, invert, SKIRT, true)


## perlin_worley = remap(perlin, worley - 1, 1, 0, 1), i.e. the Worley field
## raises the floor Perlin is measured against, so Perlin's mass survives only
## where a Worley lobe agrees with it.
func _combine_perlin_worley(perlin: Array, worley: Array) -> Array:
	var out: Array = []
	for z in SIZE:
		var p_img: Image = perlin[z]
		var w_img: Image = worley[z]
		var dst := Image.create_empty(SIZE, SIZE, false, Image.FORMAT_L8)
		for y in SIZE:
			for x in SIZE:
				var p := p_img.get_pixel(x, y).r
				var w := w_img.get_pixel(x, y).r
				var v := clampf(remapf(p, w - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0)
				dst.set_pixel(x, y, Color(v, v, v))
		out.append(dst)
	return out


func remapf(value: float, from_low: float, from_high: float, to_low: float,
		to_high: float) -> float:
	var span := from_high - from_low
	if is_zero_approx(span):
		return to_low
	return to_low + ((value - from_low) / span) * (to_high - to_low)


## An empty channel array writes 1.0, not 0.0: the shader multiplies unused
## channels through in places, and a zero there silently deletes the cloud.
func _pack(r: Array, g: Array, b: Array, a: Array) -> Array:
	var out: Array = []
	for z in SIZE:
		var dst := Image.create_empty(SIZE, SIZE, false, Image.FORMAT_RGBA8)
		for y in SIZE:
			for x in SIZE:
				dst.set_pixel(x, y, Color(
					_sample(r, z, x, y),
					_sample(g, z, x, y),
					_sample(b, z, x, y),
					_sample(a, z, x, y)))
		out.append(dst)
	return out


func _sample(slices: Array, z: int, x: int, y: int) -> float:
	if slices.is_empty():
		return 1.0
	return (slices[z] as Image).get_pixel(x, y).r


## No mipmaps, deliberately. The march samples inside `if (density > 0.0)`, and
## a texture read under non-uniform control flow has undefined derivatives —
## so every fetch in the shader is a textureLod at level 0 and there is no mip
## chain for it to choose from anyway. Building one would cost 30% more VRAM to
## be never sampled.
func _save(slices: Array, path: String) -> void:
	var tex := ImageTexture3D.new()
	var err := tex.create(Image.FORMAT_RGBA8, SIZE, SIZE, SIZE, false, slices)
	if err != OK:
		push_error("ImageTexture3D.create failed for %s: %d" % [path, err])
		_failed = true
		return
	err = ResourceSaver.save(tex, path)
	if err != OK:
		push_error("saving %s failed: %d" % [path, err])
		_failed = true
		return
	## Round-trip immediately. A texture that saved but did not serialise its
	## data reloads as the right size full of nothing, and the sky would just
	## render clear — the exact failure this whole file exists to avoid.
	var back := ResourceLoader.load(path, "ImageTexture3D", ResourceLoader.CACHE_MODE_IGNORE) as ImageTexture3D
	if back == null or back.get_data().size() != SIZE:
		push_error("%s reloaded empty" % path)
		_failed = true
		return
	print("  %s  %d^3 RGBA8" % [path, SIZE])
