#ifndef RELEASE_DOWNLOAD_H
#define RELEASE_DOWNLOAD_H

#include <godot_cpp/variant/string.hpp>

namespace godot {

struct PlatformInfo {
	String os;
	String arch;
	String exe_suffix;
	String bundle_dir;
};

PlatformInfo detect_platform();

String download_latest_release(const String &p_cache_dir, const PlatformInfo &p_platform,
		const String &p_binary_name, const String &p_repo, const String &p_display_name);

} // namespace godot

#endif // RELEASE_DOWNLOAD_H
