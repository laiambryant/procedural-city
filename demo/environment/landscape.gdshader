shader_type spatial;
render_mode cull_back;

// A flat foreground that becomes low, eroded hills beyond the city.
// Everything is in metres; the origin remains flat for the entire orbit.
varying vec3 landscape_position;

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

float height_at(vec2 p) {
	float hills = noise(p * 0.0018 + vec2(12.7, 3.1)) * 0.65
		+ noise(p * 0.0041 + vec2(5.2, 8.3)) * 0.25
		+ noise(p * 0.009) * 0.1;
	return smoothstep(500.0, 1400.0, length(p)) * hills * hills * 180.0;
}

void vertex() {
	VERTEX.y += height_at(VERTEX.xz);
	float dx = height_at(VERTEX.xz + vec2(2, 0)) - height_at(VERTEX.xz - vec2(2, 0));
	float dz = height_at(VERTEX.xz + vec2(0, 2)) - height_at(VERTEX.xz - vec2(0, 2));
	NORMAL = normalize(vec3(-dx, 4.0, -dz));
	landscape_position = VERTEX;
}

void fragment() {
	vec2 p = landscape_position.xz;
	float broad = noise(p * 0.036) * 0.65 + noise(p * 0.17) * 0.35;
	vec3 stone = mix(vec3(0.23, 0.245, 0.26), vec3(0.39, 0.36, 0.33), broad);
	ALBEDO = stone;
	ROUGHNESS = 0.86;
}
