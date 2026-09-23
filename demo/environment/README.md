# Showcase environment

The demo's WorldEnvironment combines a volumetric-cloud sky, sky
ambient/reflections, screen-space reflections, depth fog toward the horizon,
restrained glow and ambient occlusion. The city stands in an 8 km water plane
(`mirror_floor.gdshader`): a near-mirror with slow, distance-calmed ripples
and an antialiased neon grid that fades out around the city.
All fixed geometry and lighting are authored in `../scenes/main.tscn`.

Each city has a mood in `../scenes/demo.gd`: besides the albedo ramp, it sets
the sky zenith and horizon, cloud tint and coverage, star brightness, fog,
key and fill light colours, and the grid colour. The first ramp is
godisplacementx's default gradient (`#00ffff`, `#9500ff`, `#ffe500`).

## Clouds and provenance

`cloud_sky.gdshader`, the two 64³ RGBA noise volumes, and their bake script are
adapted from **The Beehive**, `LEVEL_0` (GPL-3.0), from the same project author.
The original material was `common/materials/sky/candyfloss_sky.gdshader`.
The shader's density model follows Andrew Schneider's Perlin-Worley cloud
method, and the lighting follows
[clayjohn's volumetric-cloud demo v2](https://github.com/clayjohn/godot-volumetric-cloud-demo-v2).
The upstream license notice is preserved in [CLOUD_LICENSE.md](CLOUD_LICENSE.md).
The atmosphere shader mentioned in that upstream notice is not used here.

The noise volumes are generated with Godot FastNoiseLite, not downloaded art.
The water shader and the star field are procedural. There is no runtime reference
to another project, global shader setting or external texture download.

The cloud march runs at half resolution with 64 view steps and four cone-light
steps. Its cubemap pass uses an analytic cloud tint, avoiding six extra cloud
marches for reflections. `Sky` uses the 256-pixel radiance size required by
Godot's realtime sky mode. The fill light illuminates geometry only.

For slower GPUs, reduce `march_steps` to 32 and `light_steps` to 2 in
`cloud_sky.tres`. Validate the resulting appearance with a real renderer.
The showcase is intended for **Forward+**.

## Reproducible captures

`demo.gd` drives `showcase_time` from the same clock as the city and camera.
Clouds therefore freeze at the requested capture time instead of advancing
with wall-clock shader time. The first capture warms up eight rendered frames.

From the repository root, with a working display and Vulkan renderer:

```bash
godot --path demo --script res://scenes/capture.gd -- \
  --capture-dir=/tmp/procedural-city-preview --preview-time=2.7
```

Use 7.7, 12.7 and 17.7 for the other three cities. The image is always 1280×720,
regardless of the window manager's window size.

## Rebuilding noise (optional)

The shipped volumes are ready to use. To deliberately regenerate them:

```bash
godot --path demo res://environment/bake_cloud_noise.tscn
```

Run this **windowed**, never with `--headless`: ImageTexture3D needs a real
RenderingServer to serialize its texels. The bake refuses the dummy renderer.
Do not rebake the volumes just to import or package the demo.

## Validation

Validated on Godot 4.7.2, Linux x86-64, Vulkan Forward+ on Intel Iris Xe:
all three city views and the full 15-second capture timeline; a fresh demo
copy imported and rendered outside the checkout; and the noise baker's GPU
save/reload check. The clean import, bake and render logs contained no engine
errors or warnings. Other platforms and renderers were not tested here.
