shader_type spatial;

varying vec2 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xz;
}

void fragment() {
	// Antialiased eight-metre paving slabs and a restrained brass perimeter.
	vec2 cell = world_position / 8.0;
	vec2 edge = abs(fract(cell - 0.5) - 0.5) * 8.0;
	vec2 width = max(fwidth(world_position), vec2(0.015));
	vec2 seam = 1.0 - smoothstep(vec2(0.025), vec2(0.025) + width, edge);
	float joint = max(seam.x, seam.y);
	float border_distance = abs(max(abs(world_position.x), abs(world_position.y)) - 74.0);
	float border = 1.0 - smoothstep(0.09, 0.09 + max(width.x, width.y), border_distance);
	vec3 stone = mix(vec3(0.19, 0.22, 0.25), vec3(0.10, 0.12, 0.15), joint * 0.45);
	ALBEDO = mix(stone, vec3(0.63, 0.40, 0.17), border);
	METALLIC = mix(0.12, 0.65, border);
	ROUGHNESS = mix(0.48, 0.32, border);
}
