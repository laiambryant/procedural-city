#ifndef DETERMINISTIC_NOISE_H
#define DETERMINISTIC_NOISE_H

#include <godot_cpp/core/math.hpp>

#include <cstdint>

namespace godot {

// Seeded value noise with no engine dependency, so results are identical on
// every platform and every run with the same seed.

// hash_u32 is Chris Wellons's "lowbias32" xorshift-multiply hash; the shift
// and multiplier constants are the published, search-optimized values.
inline uint32_t hash_u32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// Odd primes decorrelate the two lattice axes before hashing.
inline constexpr uint32_t HASH_PRIME_X = 0x8DA6B343u;
inline constexpr uint32_t HASH_PRIME_Y = 0xD8163841u;
// hash01 keeps the low 24 bits — a float's full mantissa — so the result is
// uniform in [0, 1) with no rounding bias.
inline constexpr uint32_t HASH01_MANTISSA_MASK = 0x00FFFFFFu;
inline constexpr float HASH01_MANTISSA_SCALE = 1.0f / 16777216.0f; // 2^-24

inline float hash01(int p_x, int p_y, uint32_t p_seed) {
	const uint32_t h = hash_u32((uint32_t)p_x * HASH_PRIME_X ^ (uint32_t)p_y * HASH_PRIME_Y ^ p_seed);
	return (float)(h & HASH01_MANTISSA_MASK) * HASH01_MANTISSA_SCALE;
}

inline float value_noise(float p_x, float p_y, uint32_t p_seed) {
	const int x0 = (int)Math::floor(p_x);
	const int y0 = (int)Math::floor(p_y);
	const float fx = p_x - (float)x0;
	const float fy = p_y - (float)y0;
	const float ux = fx * fx * (3.0f - 2.0f * fx);
	const float uy = fy * fy * (3.0f - 2.0f * fy);
	const float a = hash01(x0, y0, p_seed);
	const float b = hash01(x0 + 1, y0, p_seed);
	const float c = hash01(x0, y0 + 1, p_seed);
	const float d = hash01(x0 + 1, y0 + 1, p_seed);
	return Math::lerp(Math::lerp(a, b, ux), Math::lerp(c, d, ux), uy);
}

} // namespace godot

#endif // DETERMINISTIC_NOISE_H
