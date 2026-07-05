#ifndef GOPLACEMENTX_RUNNER_H
#define GOPLACEMENTX_RUNNER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "goplacementx_params.h"

namespace godot {

// GoplacementxRunner orchestrates the external goplacementx CLI. It performs no
// scene-tree access so it is safe to call from a worker thread.
class GoplacementxRunner : public RefCounted {
	GDCLASS(GoplacementxRunner, RefCounted)

protected:
	static void _bind_methods();

public:
	String find_binary(const String &p_override) const;
	String write_config(const String &p_dir, const Ref<GoplacementxParams> &p_params) const;
	int64_t resolve_seed(const Ref<GoplacementxParams> &p_params) const;
	Dictionary run_generate(const String &p_binary, const String &p_config, const String &p_mode,
			int64_t p_seed, const String &p_out_png, const Ref<GoplacementxParams> &p_params) const;
	// run_bundle renders several output maps in a single CLI invocation. Each
	// entry of p_emits is a Dictionary { "mode", "seed", "path" }; emits sharing a
	// seed reuse one (expensive) generation pass inside the CLI.
	Dictionary run_bundle(const String &p_binary, const String &p_config,
			const Array &p_emits, const Ref<GoplacementxParams> &p_params) const;

	GoplacementxRunner() {}
	~GoplacementxRunner() {}
};

} // namespace godot

#endif // GOPLACEMENTX_RUNNER_H
