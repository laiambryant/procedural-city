shader_type sky;
render_mode use_half_res_pass;

// Adapted from The Beehive LEVEL_0 candyfloss_sky.gdshader (GPL-3.0).
// Density: Schneider's Perlin-Worley method. Lighting follows clayjohn's
// volumetric-cloud-demo-v2: energy-conserving integration, cone light march,
// stacked Henyey-Greenstein lobes, and multiple scattering.
// See README.md and CLOUD_LICENSE.md for credits and the upstream notice.
// Driven by the showcase timeline so recording speed cannot change the sky.
uniform float showcase_time = 0.0;

group_uniforms Sky;
uniform vec3 sky_zenith : source_color = vec3(0.60, 0.55, 0.76);
uniform vec3 sky_horizon : source_color = vec3(0.96, 0.72, 0.68);
uniform vec3 ground_horizon : source_color = vec3(0.94, 0.72, 0.66);
uniform vec3 ground_bottom : source_color = vec3(0.50, 0.44, 0.50);
uniform float sky_curve : hint_range(0.01, 1.0) = 0.12;

group_uniforms Sun;
uniform float sun_size : hint_range(0.0, 0.2) = 0.028;
uniform float sun_blur : hint_range(0.0, 1.0) = 0.35;
uniform vec3 sun_disc_color : source_color = vec3(1.0, 0.93, 0.84);
uniform float sun_disc_energy : hint_range(0.0, 40.0) = 12.0;

group_uniforms Clouds;
uniform sampler3D base_noise : filter_linear, repeat_enable;
uniform sampler3D detail_noise : filter_linear, repeat_enable;
uniform float slab_base = 620.0;
uniform float slab_thickness = 520.0;
uniform float max_march_length = 5200.0;
uniform float base_scale = 0.00019;
uniform float detail_scale = 0.0021;
uniform vec3 detail_stretch = vec3(1.0, 2.5, 1.0);
uniform float coverage : hint_range(0.0, 1.0) = 0.30;
uniform float density : hint_range(0.001, 0.2) = 0.024;
uniform float erosion : hint_range(0.0, 1.0) = 0.42;
uniform float base_softness : hint_range(0.01, 0.6) = 0.26;
uniform float top_softness : hint_range(0.01, 0.6) = 0.34;
uniform vec2 wind = vec2(1.0, 0.35);
uniform float wind_speed = 5.5;
uniform float churn = 0.9;

group_uniforms Quality;
uniform int light_steps : hint_range(2, 6) = 6;
uniform int march_steps : hint_range(8, 64) = 32;
uniform float light_cone_spread : hint_range(0.0, 1.0) = 0.32;

group_uniforms Floss;
uniform vec3 floss_lit : source_color = vec3(1.00, 0.94, 0.91);
uniform vec3 floss_mid : source_color = vec3(1.00, 0.72, 0.78);
uniform vec3 floss_deep : source_color = vec3(0.88, 0.55, 0.66);
uniform float floss_tint : hint_range(0.0, 1.0) = 0.85;
uniform float floss_ramp_top : hint_range(0.1, 8.0) = 2.2;

group_uniforms Lighting;
uniform float sun_energy : hint_range(0.0, 10.0) = 3.4;
uniform float scatter_albedo : hint_range(0.0, 1.0) = 0.99;
uniform float ambient_sky_energy : hint_range(0.0, 4.0) = 0.55;
uniform float ambient_ground_energy : hint_range(0.0, 4.0) = 0.42;
uniform vec3 ground_bounce : source_color = vec3(0.96, 0.74, 0.68);
uniform float phase_forward : hint_range(0.0, 0.99) = 0.8;
uniform float phase_back : hint_range(-0.99, 0.0) = -0.15;
uniform float phase_boost : hint_range(0.5, 4.0) = 1.6;
uniform float powder_amount : hint_range(0.0, 1.0) = 0.5;

group_uniforms Horizon;
uniform float horizon_fade_start : hint_range(0.0, 0.5) = 0.035;
uniform float horizon_fade_end : hint_range(0.0, 1.0) = 0.24;
uniform float zenith_clear_start : hint_range(0.0, 1.0) = 0.55;
uniform float zenith_clear_end : hint_range(0.0, 1.5) = 1.0;

group_uniforms Cubemap;
uniform vec3 cubemap_cloud_tint : source_color = vec3(1.0, 0.80, 0.82);
uniform float cubemap_cloud_amount : hint_range(0.0, 2.0) = 0.75;

const float LUMA_R = 0.2126;
const float LUMA_G = 0.7152;
const float LUMA_B = 0.0722;
const float EPS = 1e-6;

float luma(vec3 c) {
	return dot(c, vec3(LUMA_R, LUMA_G, LUMA_B));
}

float remap(float v, float lo_in, float hi_in, float lo_out, float hi_out) {
	return lo_out + (((v - lo_in) / (hi_in - lo_in)) * (hi_out - lo_out));
}

float hash13(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float henyey_greenstein(float cos_theta, float g) {
	const float k = 0.0795774715459;
	return k * (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cos_theta, 1.5);
}

vec3 floss_ramp(float t) {
	return t < 0.5
		? mix(floss_deep, floss_mid, t * 2.0)
		: mix(floss_mid, floss_lit, (t - 0.5) * 2.0);
}

vec3 sky_gradient(vec3 dir) {
	if (dir.y >= 0.0) {
		return mix(sky_horizon, sky_zenith, pow(dir.y, sky_curve));
	}
	return mix(ground_horizon, ground_bottom, pow(-dir.y, 0.5));
}

float height_fraction(float y) {
	return clamp((y - slab_base) / slab_thickness, 0.0, 1.0);
}

float cloud_density(vec3 p, float hf) {
	vec3 wp = p;
	wp.xz += wind * (showcase_time * wind_speed);

	vec4 n = textureLod(base_noise, wp * base_scale, 0.0);
	float fbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
	float base = remap(n.r, -(1.0 - fbm), 1.0, 0.0, 1.0);

	float grad = smoothstep(0.0, base_softness, hf)
		* (1.0 - smoothstep(1.0 - top_softness, 1.0, hf));

	base = remap(base * grad, 1.0 - coverage, 1.0, 0.0, 1.0);
	base *= coverage;
	if (base <= 0.0) {
		return 0.0;
	}

	vec3 dp = wp * detail_scale * detail_stretch;
	dp.y -= showcase_time * churn;
	vec3 hn = textureLod(detail_noise, dp, 0.0).rgb;
	float hfbm = hn.r * 0.625 + hn.g * 0.25 + hn.b * 0.125;
	hfbm = mix(hfbm, 1.0 - hfbm, clamp(hf * 4.0, 0.0, 1.0));
	base = remap(base, hfbm * erosion * hf, 1.0, 0.0, 1.0);

	return clamp(base, 0.0, 1.0);
}

vec4 march_clouds(vec3 ro, vec3 rd, float t_start, float t_len, vec3 ldir) {
	const vec3 CONE[6] = {
		vec3( 0.38051305,  0.92453449, -0.02111345),
		vec3(-0.50625799, -0.03590792, -0.86163418),
		vec3(-0.32509218, -0.94557439,  0.01428793),
		vec3( 0.09026238, -0.27376545,  0.95755165),
		vec3( 0.28128598,  0.42443639, -0.86065785),
		vec3(-0.16852403,  0.14748697,  0.97460106)
	};

	float ss = t_len / float(march_steps);
	vec3 p = ro + rd * (t_start + hash13(rd * 631.0) * ss);

	float lss = slab_thickness / 48.0;

	float cos_theta = dot(ldir, rd);
	float phase = max(max(
		henyey_greenstein(cos_theta, phase_forward),
		henyey_greenstein(cos_theta, 0.4 - 1.4 * ldir.y)),
		henyey_greenstein(cos_theta, phase_back)) * phase_boost;

	vec3 sun_col = floss_lit * LIGHT0_COLOR * LIGHT0_ENERGY * sun_energy;
	vec3 amb_sky = mix(sky_zenith, floss_mid, 0.5) * ambient_sky_energy;
	vec3 amb_ground = ground_bounce * floss_deep * ambient_ground_energy;

	float transmittance = 1.0;
	float alpha = 0.0;
	vec3 scattered = vec3(0.0);

	for (int i = 0; i < march_steps; i++) {
		p += rd * ss;
		float hf = height_fraction(p.y);
		float d = cloud_density(p, hf);
		if (d <= 0.0) {
			continue;
		}

		float dt = exp(-density * d * ss);

		float cd = 0.0;
		vec3 lp = p;
		float light_stride = lss * 6.0 / float(light_steps);
		for (int j = 0; j < light_steps; j++) {
			int cone_index = min(int(float(j) * 6.0 / float(light_steps)), 5);
			lp += (ldir + CONE[cone_index] * float(cone_index) * light_cone_spread) * light_stride;
			cd += cloud_density(lp, height_fraction(lp.y));
		}
		vec3 fp = p + ldir * 18.0 * lss;
		float distant_density = cloud_density(fp, height_fraction(fp.y));

		float optical = density * (cd * light_stride + distant_density * lss) * 3.0;

		float ms = 0.0;
		float amp = 1.0;
		float ext = 1.0;
		for (int o = 0; o < 3; o++) {
			ms += amp * exp(-optical * ext);
			amp *= 0.5;
			ext *= 0.5;
		}

		float powder = 1.0 - exp(-optical * 2.0);
		float sun_reach = 2.0 * ms * mix(1.0, powder, powder_amount);

		vec3 ambient = mix(amb_ground, amb_sky, smoothstep(0.0, 1.0, hf));
		vec3 radiance = (ambient + sun_reach * sun_col * phase) * scatter_albedo * d;

		alpha += (1.0 - dt) * (1.0 - alpha);
		scattered += transmittance * (radiance - radiance * dt) / max(EPS, d);
		transmittance *= dt;

		if (transmittance < 0.01) {
			break;
		}
	}

	float lum = luma(scattered);
	if (lum > EPS) {
		vec3 ramp = floss_ramp(clamp(lum / floss_ramp_top, 0.0, 1.0));
		ramp /= max(luma(ramp), EPS);
		scattered = mix(scattered, ramp * lum, floss_tint);
	}

	return vec4(scattered, clamp(alpha, 0.0, 1.0));
}

void sky() {
	vec3 background = sky_gradient(EYEDIR);

	if (LIGHT0_ENABLED) {
		float d = distance(EYEDIR, LIGHT0_DIRECTION);
		float disc = 1.0 - smoothstep(sun_size * (1.0 - sun_blur),
			sun_size * (1.0 + sun_blur) + EPS, d);
		float glow = pow(clamp(1.0 - d * 0.5, 0.0, 1.0), 24.0) * 0.06;
		background += sun_disc_color * sun_disc_energy * max(disc, glow)
			* smoothstep(-0.05, 0.05, EYEDIR.y);
	}

	if (AT_CUBEMAP_PASS) {
		float band = smoothstep(horizon_fade_start, horizon_fade_end, EYEDIR.y)
			* (1.0 - smoothstep(zenith_clear_start, zenith_clear_end, EYEDIR.y));
		COLOR = mix(background, cubemap_cloud_tint,
			clamp(band * coverage * cubemap_cloud_amount, 0.0, 1.0));

	} else if (AT_HALF_RES_PASS) {
		vec3 cloud_rgb = vec3(0.0);
		float cloud_a = 0.0;

		vec3 ro = POSITION;
		float t_top = (slab_base + slab_thickness - ro.y) / max(EYEDIR.y, EPS);
		if (EYEDIR.y > horizon_fade_start && t_top > 0.0) {
			float t_start = max((slab_base - ro.y) / EYEDIR.y, 0.0);
			float t_len = min(t_top - t_start, max_march_length);
			vec4 cloud = march_clouds(ro, EYEDIR, t_start, t_len, LIGHT0_DIRECTION);
			float fade = smoothstep(horizon_fade_start, horizon_fade_end, EYEDIR.y)
				* (1.0 - smoothstep(zenith_clear_start, zenith_clear_end, EYEDIR.y));
			cloud_rgb = cloud.rgb * fade;
			cloud_a = cloud.a * fade;
		}

		COLOR = cloud_rgb;
		ALPHA = cloud_a;

	} else {
		// March output is premultiplied by opacity; do not multiply it twice.
		COLOR = background * (1.0 - HALF_RES_COLOR.a) + HALF_RES_COLOR.rgb;
	}
}
