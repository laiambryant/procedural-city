#ifndef PROC_CITY_LOG_H
#define PROC_CITY_LOG_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace godot {

void log_pipeline_event(const String &p_message);

String format_byte_size(int64_t p_bytes);
String format_pixel_size(const Ref<Image> &p_image);
String generation_mode_name(int p_mode);

// Reports how long a stage took the moment it finishes, so a slow run names its
// own bottleneck instead of leaving the whole pipeline as one opaque wait.
class StageTimer {
public:
	explicit StageTimer(const String &p_stage);

	double elapsed_seconds() const;
	void report();
	void report(const String &p_detail);

private:
	String _stage;
	uint64_t _started_usec;
};

} // namespace godot

#endif // PROC_CITY_LOG_H
