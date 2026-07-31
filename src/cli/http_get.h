#ifndef HTTP_GET_H
#define HTTP_GET_H

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// Release archives are tens of megabytes; this ceiling is generous enough for
// any of them and small enough that a runaway response cannot exhaust memory.
// It doubles as the bound on gzip expansion when a .tar.gz is decompressed.
inline constexpr int64_t MAX_DOWNLOAD_BYTES = 256ll * 1024 * 1024;

struct HttpResponse {
	int code = 0;
	PackedByteArray body;
	String error;

	bool ok() const { return error.is_empty() && code == 200; }
};

// http_get performs a blocking HTTPS GET, following up to p_redirects_left
// redirects by hand (HTTPClient does not). Only https:// URLs are accepted:
// release and API traffic is https-only, and a plain-http fallback would be a
// downgrade attack waiting to happen. Every failure comes back in
// HttpResponse::error rather than as a signal or an exception, because the
// caller is a worker thread with nowhere to raise.
HttpResponse http_get(const String &p_url, const String &p_accept, int p_redirects_left);

} // namespace godot

#endif // HTTP_GET_H
