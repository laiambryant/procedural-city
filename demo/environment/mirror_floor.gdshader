shader_type spatial;

// Still, dark water the city rises out of: a near-mirror with slow ripples
// and a faint neon survey grid that fades out before it can alias.
// Everything is in metres. Driven by the showcase timeline, not TIME.
uniform float showcase_time = 0.0;
uniform vec3 water_color : source_color = vec3(0.012, 0.014, 0.024);
uniform vec3 grid_color : source_color = vec3(0.0, 1.0, 1.0);
uniform float grid_energy : hint_range(0.0, 16.0) = 0.75;
uniform float grid_spacing = 10.0;
uniform float grid_fade_start = 70.0;
uniform float grid_fade_end = 300.0;
uniform float ripple_strength : hint_range(0.0, 1.0) = 0.1;
uniform float ripple_fade_end = 700.0;

varying vec3 world_position;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
	vec2 cell = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(cell), hash(cell + vec2(1, 0)), f.x),
		mix(hash(cell + vec2(0, 1)), hash(cell + vec2(1, 1)), f.x), f.y);
}

float ripples(vec2 p) {
	return noise(p * 0.045 + vec2(showcase_time * 0.05, 0.0)) * 0.6
		+ noise(p * 0.12 - vec2(0.0, showcase_time * 0.08)) * 0.4;
}

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 p = world_position.xz;
	float h = ripples(p);
	vec2 slope = vec2(ripples(p + vec2(0.5, 0.0)) - h, ripples(p + vec2(0.0, 0.5)) - h);
	// Distant ripples are sub-pixel and would only sparkle; calm them to a mirror.
	float calm = 1.0 - smoothstep(60.0, ripple_fade_end, length(world_position - CAMERA_POSITION_WORLD));
	NORMAL_MAP = normalize(vec3(-slope * ripple_strength * 8.0 * calm, 1.0)) * 0.5 + 0.5;

	// Antialiased lines; once a cell shrinks below a few pixels the grid
	// fades instead of turning into moiré.
	vec2 cell = p / grid_spacing;
	vec2 width = fwidth(cell);
	vec2 line = 1.0 - smoothstep(vec2(0.0), width, abs(fract(cell - 0.5) - 0.5));
	float grid = max(line.x, line.y) * (1.0 - smoothstep(0.03, 0.3, max(width.x, width.y)));
	grid *= 1.0 - smoothstep(grid_fade_start, grid_fade_end, length(p));

	ALBEDO = water_color;
	METALLIC = 0.0;
	SPECULAR = 0.9;
	ROUGHNESS = 0.035;
	EMISSION = grid_color * grid_energy * grid;
}
