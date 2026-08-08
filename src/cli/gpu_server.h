#ifndef PROC_CITY_GPU_SERVER_H
#define PROC_CITY_GPU_SERVER_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <mutex>

#include "cli/goplacementx_params.h"

namespace godot {

class ProcCityGpuServer : public Object {
	GDCLASS(ProcCityGpuServer, Object)

	static ProcCityGpuServer *singleton;

	Ref<FileAccess> pipe;
	int32_t pid = -1;
	bool ready = false;
	String started_binary;
	mutable std::mutex io_mutex;

	void close_locked();
	bool read_handshake_locked();

protected:
	static void _bind_methods();

public:
	static ProcCityGpuServer *get_singleton() { return singleton; }

	bool ensure_started(const String &p_binary);
	Dictionary run_bundle(const String &p_config, const Array &p_emits, const Ref<GoplacementxParams> &p_params,
			int p_material_max_size, bool p_mipmaps);
	bool is_running() const;
	void shutdown();

	ProcCityGpuServer();
	~ProcCityGpuServer();
};

} // namespace godot

#endif // PROC_CITY_GPU_SERVER_H
