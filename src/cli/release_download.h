#ifndef RELEASE_DOWNLOAD_H
#define RELEASE_DOWNLOAD_H

#include <godot_cpp/variant/string.hpp>

namespace godot {

// PlatformInfo is the running platform expressed the way the release assets
// name it, plus the addon subdirectory a bundled binary would ship in.
struct PlatformInfo {
	String os;		   // release asset OS token: windows / linux / darwin
	String arch;	   // release asset arch token: amd64 / arm64
	String exe_suffix; // ".exe" on Windows
	String bundle_dir; // addon subdirectory the binary ships in
};

PlatformInfo detect_platform();

// download_latest_release fetches the newest GitHub release of p_repo, extracts
// p_binary_name for this platform and installs it under p_cache_dir. Returns
// the installed path, or empty on failure with a pushed warning explaining why:
// this runs on a worker thread where a hard failure would take the editor with
// it, and every caller has a working fallback.
String download_latest_release(const String &p_cache_dir, const PlatformInfo &p_platform,
							   const String &p_binary_name, const String &p_repo, const String &p_display_name);

} // namespace godot

#endif // RELEASE_DOWNLOAD_H
