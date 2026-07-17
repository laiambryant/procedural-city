#ifndef BINARY_PROVIDER_H
#define BINARY_PROVIDER_H

#include <godot_cpp/variant/string.hpp>

namespace godot {

enum class CliKind {
	GODISPLACEMENTX = 0,
	GPUDISPLACEMENTX = 1,
};

// Resolves the CLI executable the generator shells out to. Search order: the
// explicit override, binaries bundled with the addon, a previously downloaded
// copy, and finally — when p_allow_download is set — the latest GitHub release
// of the CLI's repository, downloaded and cached under user://.
// Returns an absolute path, or an empty String when nothing could be resolved.
// Blocking; safe to call from a worker thread (no scene-tree access).
String resolve_cli_binary(CliKind p_kind, const String &p_override, bool p_allow_download);
String resolve_cli_binary(const String &p_override, bool p_allow_download);

} // namespace godot

#endif // BINARY_PROVIDER_H
