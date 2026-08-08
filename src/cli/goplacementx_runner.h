#ifndef GOPLACEMENTX_RUNNER_H
#define GOPLACEMENTX_RUNNER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "cli/goplacementx_params.h"

namespace godot {

class GoplacementxRunner : public RefCounted {
	GDCLASS(GoplacementxRunner, RefCounted)

public:
	enum CliKind {
		CLI_GODISPLACEMENTX = 0,
		CLI_GPUDISPLACEMENTX = 1,
	};

private:
	int cli_kind = CLI_GODISPLACEMENTX;

protected:
	static void _bind_methods();

public:
	void set_cli_kind(int p_kind) { cli_kind = p_kind; }
	int get_cli_kind() const { return cli_kind; }
	bool supports_gdxraw() const { return true; }
	String find_binary(const String &p_override) const;
	String ensure_binary(const String &p_override, bool p_allow_download) const;
	String write_config(const String &p_dir, const Ref<GoplacementxParams> &p_params) const;
	int64_t resolve_seed(const Ref<GoplacementxParams> &p_params) const;
	Dictionary run_generate(const String &p_binary, const String &p_config, const String &p_mode,
			int64_t p_seed, const String &p_out_png, const Ref<GoplacementxParams> &p_params) const;
	Dictionary run_bundle(const String &p_binary, const String &p_config,
			const Array &p_emits, const Ref<GoplacementxParams> &p_params) const;

	GoplacementxRunner() {}
	~GoplacementxRunner() {}
};

} // namespace godot

VARIANT_ENUM_CAST(godot::GoplacementxRunner::CliKind);

#endif // GOPLACEMENTX_RUNNER_H
