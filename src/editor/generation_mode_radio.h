#ifndef PROC_CITY_GENERATION_MODE_RADIO_H
#define PROC_CITY_GENERATION_MODE_RADIO_H

#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <vector>

namespace godot {

// Renders generation_mode as a radio group instead of a dropdown: the four
// backends are one exclusive choice, and seeing all of them at once is the
// point - a dropdown hides which alternatives exist.
class ProcCityModeRadio : public EditorProperty {
	GDCLASS(ProcCityModeRadio, EditorProperty)

	std::vector<CheckBox *> choices;
	bool suppress_signal = false;

protected:
	static void _bind_methods();

public:
	void build_choices(const PackedStringArray &p_labels);
	void _update_property() override;
	void _on_choice_pressed(int p_index);
};

class ProcCityInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(ProcCityInspectorPlugin, EditorInspectorPlugin)

protected:
	static void _bind_methods() {}

public:
	bool _can_handle(Object *p_object) const override;
	bool _parse_property(Object *p_object, Variant::Type p_type, const String &p_name, PropertyHint p_hint,
			const String &p_hint_string, BitField<PropertyUsageFlags> p_usage, bool p_wide) override;
};

class ProcCityEditorPlugin : public EditorPlugin {
	GDCLASS(ProcCityEditorPlugin, EditorPlugin)

	Ref<ProcCityInspectorPlugin> inspector_plugin;

protected:
	static void _bind_methods() {}

public:
	void _enter_tree() override;
	void _exit_tree() override;
};

} // namespace godot

#endif // PROC_CITY_GENERATION_MODE_RADIO_H
