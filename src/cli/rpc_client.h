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

	bool ensure_started(const String &p_binary);
	bool is_running() const;
	void shutdown();

	Dictionary run_generate(const String &p_mode, int64_t p_seed, const String &p_out_key, const Ref<GoplacementxParams> &p_params) const;
	Dictionary run_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
			int p_material_max_size, bool p_mipmaps) const;
	Dictionary randomize(int64_t p_seed) const;
	String version() const;
};

ProcCityRpcClient *proc_city_rpc_client_for(CliKind p_kind);
void proc_city_rpc_clients_shutdown();

} // namespace godot

#endif // PROC_CITY_RPC_CLIENT_H
