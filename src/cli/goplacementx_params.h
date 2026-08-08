#ifndef GOPLACEMENTX_PARAMS_H
#define GOPLACEMENTX_PARAMS_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

inline constexpr int GPX_DEFAULT_RESOLUTION = 2048;
inline constexpr int GPX_RANDOM_PALETTE_STOPS = 4;
inline constexpr int GPX_FRESH_SEED = 0;
inline constexpr int GPX_PALETTE_CUSTOM = 0;
inline constexpr int GPX_MIN_PALETTE_STOPS = 2;

#define GPX_FIELD(m_type, m_name, m_default) \
private:                                     \
	m_type m_name = m_default;               \
                                             \
public:                                      \
	void set_##m_name(m_type p_value) {      \
		m_name = p_value;                    \
		emit_changed();                      \
	}                                        \
	m_type get_##m_name() const {            \
		return m_name;                       \
	}

#define GPX_GATING_FIELD(m_type, m_name, m_default) \
private:                                            \
	m_type m_name = m_default;                      \
                                                    \
public:                                             \
	void set_##m_name(m_type p_value) {             \
		m_name = p_value;                           \
		notify_property_list_changed();             \
		emit_changed();                             \
	}                                               \
	m_type get_##m_name() const {                   \
		return m_name;                              \
	}

class GoplacementxParams : public Resource {
	GDCLASS(GoplacementxParams, Resource)

	GPX_FIELD(int, resolution, GPX_DEFAULT_RESOLUTION)
	GPX_GATING_FIELD(int, out_width, 0)
	GPX_GATING_FIELD(int, out_height, 0)
	GPX_FIELD(int64_t, seed, 0)
	GPX_GATING_FIELD(bool, randomize_seed, true)
	GPX_FIELD(bool, invert, false)
	GPX_GATING_FIELD(int, palette_preset, 0)
	GPX_FIELD(PackedColorArray, gradient_colors, PackedColorArray())

	GPX_FIELD(int, iterations, 100)
	GPX_FIELD(int, background_brightness, 32)

	GPX_GATING_FIELD(bool, rect_enabled, true)
	GPX_FIELD(Vector2i, rect_brightness, Vector2i(0, 255))
	GPX_FIELD(Vector2i, rect_alpha, Vector2i(50, 100))
	GPX_FIELD(int, rect_scale, 100)

	GPX_GATING_FIELD(bool, grid_enabled, true)
	GPX_FIELD(Vector2i, grid_brightness, Vector2i(0, 255))
	GPX_FIELD(Vector2i, grid_alpha, Vector2i(80, 100))
	GPX_FIELD(int, grid_scale, 100)
	GPX_FIELD(Vector2i, grid_amount, Vector2i(2, 5))
	GPX_FIELD(int, grid_gap, 100)

	GPX_GATING_FIELD(bool, cols_enabled, true)
	GPX_FIELD(Vector2i, cols_brightness, Vector2i(0, 255))
	GPX_FIELD(Vector2i, cols_alpha, Vector2i(80, 100))
	GPX_FIELD(int, cols_scale, 100)
	GPX_FIELD(Vector2i, cols_amount, Vector2i(2, 5))
	GPX_FIELD(int, cols_gap, 100)

	GPX_GATING_FIELD(bool, rows_enabled, true)
	GPX_FIELD(Vector2i, rows_brightness, Vector2i(0, 255))
	GPX_FIELD(Vector2i, rows_alpha, Vector2i(80, 100))
	GPX_FIELD(int, rows_scale, 100)
	GPX_FIELD(Vector2i, rows_amount, Vector2i(2, 5))
	GPX_FIELD(int, rows_gap, 100)

	GPX_GATING_FIELD(bool, lines_enabled, true)
	GPX_FIELD(Vector2i, lines_brightness, Vector2i(0, 255))
	GPX_FIELD(Vector2i, lines_alpha, Vector2i(80, 100))
	GPX_FIELD(Vector2i, lines_width, Vector2i(5, 10))

	GPX_GATING_FIELD(bool, sprites_enabled, false)
	GPX_FIELD(int, sprite_packs, 1)
	GPX_FIELD(bool, sprites_rotation_enabled, true)
	GPX_FIELD(bool, seamless, false)
	GPX_FIELD(int, composition_modes, 1 << 14)

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	Dictionary to_dict() const;
	String to_json() const;
	void from_dict(const Dictionary &p_dict);

	bool has_explicit_output_size() const { return out_width > 0 && out_height > 0; }
	bool property_is_usable(const String &p_name) const;

	String gradient_to_string() const;
	PackedColorArray get_effective_gradient() const;
	void generate_random_palette(int p_stops, int p_seed);
	void randomize_palette();
	Callable _btn_randomize_palette() const;
	PackedStringArray composition_modes_list() const;
	PackedStringArray sprite_packs_list() const;

	GoplacementxParams() {}
	~GoplacementxParams() {}
};

} // namespace godot

#endif // GOPLACEMENTX_PARAMS_H
