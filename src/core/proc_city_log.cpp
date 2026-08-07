#include "core/proc_city_log.h"

#include "core/proc_city_generator.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void godot::log_pipeline_event(const String &p_message) {
	UtilityFunctions::print("[ProcCity] " + p_message);
}

String godot::format_byte_size(int64_t p_bytes) {
	if (p_bytes < 1024) {
		return String::num_int64(p_bytes) + " B";
	}
	if (p_bytes < 1024 * 1024) {
		return String::num((double)p_bytes / 1024.0, 1) + " KB";
	}
	return String::num((double)p_bytes / (1024.0 * 1024.0), 1) + " MB";
}

String godot::format_pixel_size(const Ref<Image> &p_image) {
	if (p_image.is_null() || p_image->is_empty()) {
		return "none";
	}
	return String::num_int64(p_image->get_width()) + "x" + String::num_int64(p_image->get_height());
}

String godot::generation_mode_name(int p_mode) {
	switch (p_mode) {
		case ProcCityGenerator::GEN_GPU_NATIVE:
			return "GPU native";
		case ProcCityGenerator::GEN_CPU_NATIVE:
			return "CPU native";
		case ProcCityGenerator::GEN_GPU_LEGACY:
			return "GPU legacy";
		case ProcCityGenerator::GEN_CPU_LEGACY:
			return "CPU legacy";
		default:
			return "unknown mode";
	}
}

StageTimer::StageTimer(const String &p_stage) :
		_stage(p_stage), _started_usec(Time::get_singleton()->get_ticks_usec()) {
}

double StageTimer::elapsed_seconds() const {
	return (double)(Time::get_singleton()->get_ticks_usec() - _started_usec) / 1000000.0;
}

void StageTimer::report() {
	report(String());
}

void StageTimer::report(const String &p_detail) {
	String message = _stage + " done in " + String::num(elapsed_seconds(), 2) + " s";
	if (!p_detail.is_empty()) {
		message += " (" + p_detail + ")";
	}
	log_pipeline_event(message);
}
