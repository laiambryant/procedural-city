#ifndef HTTP_GET_H
#define HTTP_GET_H

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

inline constexpr int64_t MAX_DOWNLOAD_BYTES = 256ll * 1024 * 1024;

struct HttpResponse {
	int code = 0;
	PackedByteArray body;
	String error;

	bool ok() const { return error.is_empty() && code == 200; }
};

HttpResponse http_get(const String &p_url, const String &p_accept, int p_redirects_left);

} // namespace godot

#endif // HTTP_GET_H
