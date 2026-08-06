#ifndef PROC_CITY_RPC_CLIENT_H
#define PROC_CITY_RPC_CLIENT_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <mutex>

#include "cli/binary_provider.h"
#include "cli/goplacementx_params.h"

namespace godot {

struct RpcStubs;

// ProcCityRpcClient keeps one persistent gRPC channel per CliKind alive for
// the whole editor/game session: `<binary> serve-grpc` is spawned once, hands
// back its loopback port on stdout, and every later generate/bundle/
// randomize/version call reuses that channel - no config file on disk, no
// per-call process spawn. Falls back cleanly (ensure_started returns false)
// when grpc++ isn't available for this build (e.g. non-Windows, or vcpkg not
// installed) so callers always have a CLI-subprocess path to drop back to.
//
// One instance per CliKind is process-wide (see proc_city_rpc_client_for);
// callers from worker threads share it behind io_mutex, same single-in-flight
// contract as ProcCityGpuServer.
class ProcCityRpcClient {
	CliKind kind;
	Ref<FileAccess> pipe;
	int32_t pid = -1;
	String started_binary;
	std::shared_ptr<void> channel;
	std::unique_ptr<RpcStubs> stubs;
	mutable std::mutex io_mutex;

	void close_locked();
	bool handshake_locked(int &r_port);
	bool dial_locked(int p_port);

public:
	explicit ProcCityRpcClient(CliKind p_kind);
	~ProcCityRpcClient();

	// ensure_started spawns/reuses the serve-grpc process for p_binary and
	// dials it; returns false (never throws) on any failure so the caller can
	// fall back to the one-shot CLI. Safe to call from a worker thread.
	bool ensure_started(const String &p_binary);
	bool is_running() const;
	void shutdown();

	// run_generate/run_bundle/randomize return the same Dictionary shape the
	// CLI-subprocess path already uses: {"code", "output", ...}; run_bundle
	// additionally fills "images" (Dictionary keyed by each emit's "path"
	// entry, same key load_result_images already expects from the GPU pipe
	// server).
	Dictionary run_generate(const String &p_mode, int64_t p_seed, const String &p_out_key, const Ref<GoplacementxParams> &p_params) const;
	Dictionary run_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
			int p_material_max_size, bool p_mipmaps) const;
	Dictionary randomize(int64_t p_seed) const;
	String version() const;
};

// One client per CliKind, created on first use and torn down at module
// unload (see register_types.cpp).
ProcCityRpcClient *proc_city_rpc_client_for(CliKind p_kind);
void proc_city_rpc_clients_shutdown();

} // namespace godot

#endif // PROC_CITY_RPC_CLIENT_H
