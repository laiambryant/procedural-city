#include "cli/http_get.h"

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

// DNS plus TLS handshake against GitHub; past this the network is not coming
// back within a session the user would wait out.
static constexpr uint64_t CONNECT_TIMEOUT_MS = 15000;
// A whole release archive over a slow link. Deliberately generous: the download
// runs on a worker thread, so a long wait costs nothing but patience.
static constexpr uint64_t TRANSFER_TIMEOUT_MS = 180000;
// HTTPClient has no blocking wait, so the connect and body loops sleep between
// polls. Connecting is slow enough to poll lazily; the body loop runs tighter so
// throughput is not capped by the sleep.
static constexpr int CONNECT_POLL_INTERVAL_MS = 10;
static constexpr int BODY_POLL_INTERVAL_MS = 5;

// HTTP status range that carries a Location header.
static constexpr int HTTP_REDIRECT_MIN = 300;
static constexpr int HTTP_REDIRECT_MAX = 400;
static constexpr int HTTP_OK = 200;

static bool split_url(const String &p_url, String &r_host, String &r_path) {
	if (!p_url.begins_with("https://")) {
		return false;
	}
	const String rest = p_url.trim_prefix("https://");
	const int slash = rest.find("/");
	r_host = slash < 0 ? rest : rest.substr(0, slash);
	r_path = slash < 0 ? String("/") : rest.substr(slash);
	return !r_host.is_empty();
}

static bool poll_until(const Ref<HTTPClient> &p_client, HTTPClient::Status p_leave, uint64_t p_timeout_ms) {
	const uint64_t start = Time::get_singleton()->get_ticks_msec();
	while (p_client->get_status() == p_leave) {
		if (Time::get_singleton()->get_ticks_msec() - start > p_timeout_ms) {
			return false;
		}
		p_client->poll();
		OS::get_singleton()->delay_msec(CONNECT_POLL_INTERVAL_MS);
	}
	return true;
}

static String header_value(const Dictionary &p_headers, const String &p_key) {
	const Array keys = p_headers.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String k = keys[i];
		if (k.nocasecmp_to(p_key) == 0) {
			return p_headers[keys[i]];
		}
	}
	return String();
}

static bool connect_and_request(const Ref<HTTPClient> &p_client, const String &p_host, const String &p_path,
								const String &p_accept, HttpResponse &r_res) {
	if (p_client->connect_to_host("https://" + p_host, -1, TLSOptions::client()) != OK) {
		r_res.error = "Could not start connection to " + p_host;
		return false;
	}
	if (!poll_until(p_client, HTTPClient::STATUS_RESOLVING, CONNECT_TIMEOUT_MS) ||
		!poll_until(p_client, HTTPClient::STATUS_CONNECTING, CONNECT_TIMEOUT_MS) ||
		p_client->get_status() != HTTPClient::STATUS_CONNECTED) {
		r_res.error = "Connection to " + p_host + " failed (status " + String::num_int64(p_client->get_status()) + ").";
		return false;
	}

	PackedStringArray headers;
	headers.push_back("User-Agent: procedural-city-gdextension");
	headers.push_back("Accept: " + p_accept);
	if (p_client->request(HTTPClient::METHOD_GET, p_path, headers) != OK) {
		r_res.error = "Request to " + p_host + p_path + " failed to start.";
		return false;
	}
	if (!poll_until(p_client, HTTPClient::STATUS_REQUESTING, TRANSFER_TIMEOUT_MS) || !p_client->has_response()) {
		r_res.error = "No response from " + p_host + ".";
		return false;
	}
	return true;
}

// read_body drains the response in chunks, stopping if the transfer stalls past
// the timeout or grows past the size ceiling.
static bool read_body(const Ref<HTTPClient> &p_client, const String &p_host, HttpResponse &r_res) {
	const uint64_t start = Time::get_singleton()->get_ticks_msec();
	while (p_client->get_status() == HTTPClient::STATUS_BODY) {
		if (Time::get_singleton()->get_ticks_msec() - start > TRANSFER_TIMEOUT_MS) {
			r_res.error = "Download from " + p_host + " timed out.";
			return false;
		}
		p_client->poll();
		const PackedByteArray chunk = p_client->read_response_body_chunk();
		if (chunk.is_empty()) {
			OS::get_singleton()->delay_msec(BODY_POLL_INTERVAL_MS);
			continue;
		}
		r_res.body.append_array(chunk);
		if ((int64_t)r_res.body.size() > MAX_DOWNLOAD_BYTES) {
			r_res.error = "Download from " + p_host + " exceeded the size limit.";
			return false;
		}
	}
	return true;
}

HttpResponse godot::http_get(const String &p_url, const String &p_accept, int p_redirects_left) {
	HttpResponse res;
	String host;
	String path;
	if (!split_url(p_url, host, path)) {
		res.error = "Unsupported URL: " + p_url;
		return res;
	}

	Ref<HTTPClient> client;
	client.instantiate();
	if (!connect_and_request(client, host, path, p_accept, res)) {
		return res;
	}

	res.code = client->get_response_code();
	if (res.code >= HTTP_REDIRECT_MIN && res.code < HTTP_REDIRECT_MAX) {
		const String location = header_value(client->get_response_headers_as_dictionary(), "Location");
		client->close();
		if (location.is_empty() || p_redirects_left <= 0) {
			res.error = "Redirect from " + host + " could not be followed.";
			return res;
		}
		return http_get(location, p_accept, p_redirects_left - 1);
	}

	if (!read_body(client, host, res)) {
		return res;
	}
	client->close();

	if (res.code != HTTP_OK) {
		res.error = host + path + " answered HTTP " + String::num_int64(res.code) + ".";
	}
	return res;
}
