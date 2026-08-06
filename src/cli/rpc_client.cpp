#include "cli/rpc_client.h"

#ifdef PROC_CITY_HAVE_GRPC

#include "cli/gdxraw_loader.h"
#include "cli/goplacementx_params_proto.h"
#include "material/city_material_builder.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <grpcpp/grpcpp.h>

#include "displacement.grpc.pb.h"

#include <unordered_map>

using namespace godot;

namespace {
constexpr int MAX_MESSAGE_BYTES = 8 * 1024 * 1024;
}

struct godot::RpcStubs {
	std::unique_ptr<displacement::v1::Displacement::Stub> stub;
};

static PackedStringArray serve_grpc_args() {
	PackedStringArray args;
	args.push_back("serve-grpc");
	return args;
}

void ProcCityRpcClient::close_locked() {
	stubs.reset();
	channel.reset();
	if (pipe.is_valid()) {
		pipe->close();
		pipe.unref();
	}
	if (pid >= 0 && OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	pid = -1;
	started_binary = String();
}

bool ProcCityRpcClient::handshake_locked(int &r_port) {
	const String line = pipe->get_line();
	if (line.is_empty()) {
		return false;
	}
	const Variant parsed = JSON::parse_string(line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary handshake = parsed;
	if (!handshake.has("grpc_port")) {
		return false;
	}
	r_port = (int)handshake["grpc_port"];
	return r_port > 0;
}

bool ProcCityRpcClient::dial_locked(int p_port) {
	grpc::ChannelArguments args;
	args.SetMaxReceiveMessageSize(MAX_MESSAGE_BYTES);
	args.SetMaxSendMessageSize(MAX_MESSAGE_BYTES);
	auto grpc_channel = grpc::CreateCustomChannel(
			"127.0.0.1:" + std::to_string(p_port), grpc::InsecureChannelCredentials(), args);
	if (!grpc_channel) {
		return false;
	}
	auto new_stubs = std::make_unique<RpcStubs>();
	new_stubs->stub = displacement::v1::Displacement::NewStub(grpc_channel);
	if (!new_stubs->stub) {
		return false;
	}
	channel = std::static_pointer_cast<void>(grpc_channel);
	stubs = std::move(new_stubs);
	return true;
}

ProcCityRpcClient::ProcCityRpcClient(CliKind p_kind) :
		kind(p_kind) {}

ProcCityRpcClient::~ProcCityRpcClient() {
	close_locked();
}

bool ProcCityRpcClient::ensure_started(const String &p_binary) {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (stubs && pipe.is_valid() && started_binary == p_binary &&
			(pid < 0 || OS::get_singleton()->is_process_running(pid))) {
		return true;
	}
	close_locked();

	const Dictionary proc = OS::get_singleton()->execute_with_pipe(p_binary, serve_grpc_args());
	if (proc.is_empty()) {
		return false;
	}
	pipe = proc.get("stdio", Ref<FileAccess>());
	pid = (int32_t)(int64_t)proc.get("pid", -1);
	if (pipe.is_null()) {
		close_locked();
		return false;
	}

	int port = 0;
	if (!handshake_locked(port) || !dial_locked(port)) {
		close_locked();
		return false;
	}
	started_binary = p_binary;
	return true;
}

bool ProcCityRpcClient::is_running() const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!stubs || pid < 0) {
		return false;
	}
	return OS::get_singleton()->is_process_running(pid);
}

void ProcCityRpcClient::shutdown() {
	std::lock_guard<std::mutex> guard(io_mutex);
	close_locked();
}

static Dictionary reply_error(const String &p_message) {
	Dictionary out;
	out["code"] = 1;
	out["output"] = p_message;
	return out;
}

Dictionary ProcCityRpcClient::run_generate(const String &p_mode, int64_t p_seed, const String &p_out_key, const Ref<GoplacementxParams> &p_params) const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!stubs) {
		return reply_error("rpc client not connected");
	}

	displacement::v1::GenerateRequest req;
	*req.mutable_params() = params_to_proto(p_params);
	*req.mutable_options() = render_options_from(p_params);
	req.set_mode(std::string(p_mode.utf8().get_data()));
	req.set_seed((uint64_t)p_seed);

	grpc::ClientContext ctx;
	displacement::v1::GenerateReply reply;
	const grpc::Status status = stubs->stub->Generate(&ctx, req, &reply);
	if (!status.ok()) {
		return reply_error(String(status.error_message().c_str()));
	}

	Ref<Image> image = decode_gdxraw_bytes(
			(const uint8_t *)reply.gdxraw().data(), (int64_t)reply.gdxraw().size());
	if (image.is_null()) {
		return reply_error("gRPC server sent an undecodable map");
	}

	Dictionary out;
	out["code"] = 0;
	out["output"] = String();
	out["path"] = p_out_key;
	out["image"] = image;
	return out;
}

Dictionary ProcCityRpcClient::run_bundle(const Array &p_emits, const Ref<GoplacementxParams> &p_params,
		int p_material_max_size, bool p_mipmaps) const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!stubs) {
		return reply_error("rpc client not connected");
	}

	displacement::v1::BundleRequest req;
	*req.mutable_params() = params_to_proto(p_params);
	*req.mutable_options() = render_options_from(p_params);
	struct EmitPreparation {
		bool material = false;
		bool normal = false;
		bool compose_channel = false;
	};
	std::unordered_map<std::string, EmitPreparation> preparation_by_id;
	for (int i = 0; i < p_emits.size(); i++) {
		const Dictionary e = p_emits[i];
		const String path = e.get("path", "");
		if (path.is_empty()) {
			continue;
		}
		displacement::v1::Emit *emit = req.add_emits();
		emit->set_id(std::string(path.utf8().get_data()));
		emit->set_mode(std::string(String(e.get("mode", "grayscale")).utf8().get_data()));
		emit->set_seed((uint64_t)(int64_t)e.get("seed", 0));
		preparation_by_id[std::string(path.utf8().get_data())] = {
			(bool)e.get("material", false),
			(bool)e.get("normal", false),
			(bool)e.get("compose_channel", false),
		};
	}

	grpc::ClientContext ctx;
	std::unique_ptr<grpc::ClientReader<displacement::v1::MapChunk>> reader(stubs->stub->Bundle(&ctx, req));

	Dictionary images;
	std::string current_id;
	std::unique_ptr<GdxrawStreamDecoder> decoder;
	String decode_error;
	displacement::v1::MapChunk chunk;
	while (reader->Read(&chunk)) {
		if (!decoder) {
			current_id = chunk.id();
			decoder = std::make_unique<GdxrawStreamDecoder>();
		} else if (chunk.id() != current_id) {
			decode_error = "gRPC server changed map id before the terminal chunk";
			ctx.TryCancel();
			break;
		}
		if (!decoder->append((const uint8_t *)chunk.data().data(), (int64_t)chunk.data().size())) {
			decode_error = "gRPC server sent a malformed GDXR map for " + String(current_id.c_str());
			ctx.TryCancel();
			break;
		}
		if (chunk.last()) {
			Ref<Image> image = decoder->finish();
			if (image.is_null()) {
				decode_error = "gRPC server sent a truncated GDXR map for " + String(current_id.c_str());
				ctx.TryCancel();
				break;
			}
			const auto prep = preparation_by_id.find(current_id);
			if (prep == preparation_by_id.end()) {
				decode_error = "gRPC server returned an unexpected map id " + String(current_id.c_str());
				ctx.TryCancel();
				break;
			}
			if (prep->second.material) {
				prepare_material_image(image, p_material_max_size,
						p_mipmaps && !prep->second.compose_channel, prep->second.normal);
			}
			images[String(current_id.c_str())] = image;
			decoder.reset();
			current_id.clear();
		}
	}
	const grpc::Status status = reader->Finish();
	if (!decode_error.is_empty()) {
		return reply_error(decode_error);
	}
	if (!status.ok()) {
		return reply_error(String(status.error_message().c_str()));
	}
	if (decoder) {
		return reply_error("gRPC server ended before the terminal map chunk");
	}

	Dictionary out;
	out["code"] = 0;
	out["output"] = String();
	out["images"] = images;
	return out;
}

Dictionary ProcCityRpcClient::randomize(int64_t p_seed) const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!stubs) {
		return reply_error("rpc client not connected");
	}

	displacement::v1::RandomizeRequest req;
	req.set_seed((uint64_t)p_seed);
	grpc::ClientContext ctx;
	displacement::v1::RandomizeReply reply;
	const grpc::Status status = stubs->stub->Randomize(&ctx, req, &reply);
	if (!status.ok()) {
		return reply_error(String(status.error_message().c_str()));
	}

	Dictionary out;
	out["code"] = 0;
	out["output"] = String();
	out["params"] = proto_params_to_dict(reply.params());
	return out;
}

String ProcCityRpcClient::version() const {
	std::lock_guard<std::mutex> guard(io_mutex);
	if (!stubs) {
		return String();
	}
	displacement::v1::VersionRequest req;
	grpc::ClientContext ctx;
	displacement::v1::VersionReply reply;
	const grpc::Status status = stubs->stub->Version(&ctx, req, &reply);
	if (!status.ok()) {
		return String();
	}
	return String(reply.version().c_str());
}

static ProcCityRpcClient *g_clients[2] = { nullptr, nullptr };
static std::mutex g_clients_mutex;

ProcCityRpcClient *godot::proc_city_rpc_client_for(CliKind p_kind) {
	std::lock_guard<std::mutex> guard(g_clients_mutex);
	const int index = (int)p_kind;
	if (!g_clients[index]) {
		g_clients[index] = new ProcCityRpcClient(p_kind);
	}
	return g_clients[index];
}

void godot::proc_city_rpc_clients_shutdown() {
	std::lock_guard<std::mutex> guard(g_clients_mutex);
	for (ProcCityRpcClient *&client : g_clients) {
		if (client) {
			client->shutdown();
			delete client;
			client = nullptr;
		}
	}
}

#else // !PROC_CITY_HAVE_GRPC

using namespace godot;

ProcCityRpcClient::ProcCityRpcClient(CliKind p_kind) :
		kind(p_kind) {}
ProcCityRpcClient::~ProcCityRpcClient() {}
void ProcCityRpcClient::close_locked() {}
bool ProcCityRpcClient::handshake_locked(int &) { return false; }
bool ProcCityRpcClient::dial_locked(int) { return false; }
bool ProcCityRpcClient::ensure_started(const String &) { return false; }
bool ProcCityRpcClient::is_running() const { return false; }
void ProcCityRpcClient::shutdown() {}
Dictionary ProcCityRpcClient::run_generate(const String &, int64_t, const String &, const Ref<GoplacementxParams> &) const {
	Dictionary out;
	out["code"] = 1;
	out["output"] = "gRPC support not built into this binary";
	return out;
}
Dictionary ProcCityRpcClient::run_bundle(const Array &, const Ref<GoplacementxParams> &, int, bool) const {
	Dictionary out;
	out["code"] = 1;
	out["output"] = "gRPC support not built into this binary";
	return out;
}
Dictionary ProcCityRpcClient::randomize(int64_t) const {
	Dictionary out;
	out["code"] = 1;
	out["output"] = "gRPC support not built into this binary";
	return out;
}
String ProcCityRpcClient::version() const { return String(); }

static ProcCityRpcClient *g_clients[2] = { nullptr, nullptr };

ProcCityRpcClient *godot::proc_city_rpc_client_for(CliKind p_kind) {
	const int index = (int)p_kind;
	if (!g_clients[index]) {
		g_clients[index] = new ProcCityRpcClient(p_kind);
	}
	return g_clients[index];
}

void godot::proc_city_rpc_clients_shutdown() {
	for (ProcCityRpcClient *&client : g_clients) {
		if (client) {
			delete client;
			client = nullptr;
		}
	}
}

#endif // PROC_CITY_HAVE_GRPC
