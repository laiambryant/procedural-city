extends SceneTree

## Hosts the showcase in a fixed-size SubViewport so a frame is always captured
## at CAPTURE_SIZE. The OS window is whatever the window manager decides to
## give us — a tiling compositor resizes it — and rendering straight into it
## makes the recorded frames depend on the desktop.

const CAPTURE_SIZE := Vector2i(1280, 720)

func _initialize() -> void:
	var viewport := SubViewport.new()
	viewport.size = CAPTURE_SIZE
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_4X
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var preview := TextureRect.new()
	preview.texture = viewport.get_texture()
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	preview.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_child(preview)
	viewport.add_child(load("res://scenes/main.tscn").instantiate())
