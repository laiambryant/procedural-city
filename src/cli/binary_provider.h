#ifndef BINARY_PROVIDER_H
#define BINARY_PROVIDER_H

#include <godot_cpp/variant/string.hpp>

namespace godot {

enum class CliKind {
	GODISPLACEMENTX = 0,
	GPUDISPLACEMENTX = 1,
};

String resolve_cli_binary(CliKind p_kind, const String &p_override, bool p_allow_download);
String resolve_cli_binary(const String &p_override, bool p_allow_download);

} // namespace godot

#endif // BINARY_PROVIDER_H
