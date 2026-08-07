#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>

#include "cli/goplacementx_params.h"
#include "cli/goplacementx_runner.h"
#include "cli/gpu_server.h"
#include "cli/rpc_client.h"
#include "core/proc_city_generator.h"
#include "editor/generation_mode_radio.h"
#include "hive/hive_gen_core.h"
#include "meshing/heightmap_mesher.h"

using namespace godot;

static void register_editor_surface() {
	GDREGISTER_INTERNAL_CLASS(ProcCityModeRadio);
	GDREGISTER_INTERNAL_CLASS(ProcCityInspectorPlugin);
	GDREGISTER_INTERNAL_CLASS(ProcCityEditorPlugin);
	EditorPlugins::add_by_type<ProcCityEditorPlugin>();
}

void initialize_procedural_city_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		register_editor_surface();
		return;
	}
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GoplacementxParams);
	GDREGISTER_CLASS(GoplacementxRunner);
	GDREGISTER_CLASS(HeightmapMesher);
	GDREGISTER_CLASS(HiveGenCore);
	GDREGISTER_CLASS(ProcCityGenerator);
	GDREGISTER_CLASS(ProcCityGpuServer);
	Engine::get_singleton()->register_singleton("ProcCityGpuServer", memnew(ProcCityGpuServer));
}

void uninitialize_procedural_city_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (ProcCityGpuServer *server = ProcCityGpuServer::get_singleton()) {
		server->shutdown();
		Engine::get_singleton()->unregister_singleton("ProcCityGpuServer");
		memdelete(server);
	}
	proc_city_rpc_clients_shutdown();
}

extern "C" {
GDExtensionBool GDE_EXPORT procedural_city_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_procedural_city_module);
	init_obj.register_terminator(uninitialize_procedural_city_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
