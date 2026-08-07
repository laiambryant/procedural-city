#include "editor/generation_mode_radio.h"

#include "core/proc_city_generator.h"

#include <godot_cpp/classes/button_group.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void ProcCityModeRadio::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_choice_pressed", "index"), &ProcCityModeRadio::_on_choice_pressed);
}

void ProcCityModeRadio::build_choices(const PackedStringArray &p_labels) {
	HBoxContainer *row = memnew(HBoxContainer);
	Ref<ButtonGroup> group;
	group.instantiate();

	for (int i = 0; i < p_labels.size(); i++) {
		CheckBox *choice = memnew(CheckBox);
		choice->set_text(p_labels[i]);
		choice->set_button_group(group);
		choice->connect("pressed", Callable(this, "_on_choice_pressed").bind(i));
		row->add_child(choice);
		choices.push_back(choice);
	}

	add_child(row);
	set_bottom_editor(row);
}

void ProcCityModeRadio::_update_property() {
	const int selected = (int)get_edited_object()->get(get_edited_property());
	suppress_signal = true;
	for (size_t i = 0; i < choices.size(); i++) {
		choices[i]->set_pressed((int)i == selected);
	}
	suppress_signal = false;
}

void ProcCityModeRadio::_on_choice_pressed(int p_index) {
	if (suppress_signal) {
		return;
	}
	emit_changed(get_edited_property(), p_index);
}

bool ProcCityInspectorPlugin::_can_handle(Object *p_object) const {
	return Object::cast_to<ProcCityGenerator>(p_object) != nullptr;
}

bool ProcCityInspectorPlugin::_parse_property(Object *, Variant::Type, const String &p_name, PropertyHint,
		const String &p_hint_string, BitField<PropertyUsageFlags>, bool) {
	if (p_name != "generation_mode") {
		return false;
	}
	ProcCityModeRadio *radio = memnew(ProcCityModeRadio);
	radio->build_choices(p_hint_string.split(","));
	add_property_editor(p_name, radio);
	return true;
}

void ProcCityEditorPlugin::_enter_tree() {
	inspector_plugin.instantiate();
	add_inspector_plugin(inspector_plugin);
}

void ProcCityEditorPlugin::_exit_tree() {
	if (inspector_plugin.is_valid()) {
		remove_inspector_plugin(inspector_plugin);
		inspector_plugin.unref();
	}
}
