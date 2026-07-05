#include "proc_city_generator.h"

#include "goplacementx_runner.h"
#include "heightmap_mesher.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/csg_combiner3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/orm_material3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

static const char *GENERATED_NAME = "GeneratedCity";

// mix_seed derives an independent-but-deterministic seed from a base seed and a
// small index, so the per-channel maps differ while staying reproducible.
static uint64_t mix_seed(uint64_t p_base, uint64_t p_index) {
	return p_base ^ (p_index * 0x9E3779B97F4A7C15ULL);
}

// img_or_null fetches an Image from a result dictionary, returning a null Ref
// when the key is absent.
static Ref<Image> img_or_null(const Dictionary &p_result, const char *p_key) {
	if (p_result.has(p_key)) {
		return p_result[p_key];
	}
	return Ref<Image>();
}

static void remove_file(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}
	Ref<DirAccess> da = DirAccess::open(p_path.get_base_dir());
	if (da.is_valid()) {
		da->remove(p_path.get_file());
	}
}

ProcCityGenerator::ProcCityGenerator() {}

ProcCityGenerator::~ProcCityGenerator() {
	if (_worker.is_valid() && _worker->is_started()) {
		_worker->wait_to_finish();
	}
}

void ProcCityGenerator::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		if (_worker.is_valid() && _worker->is_started()) {
			_worker->wait_to_finish();
		}
	}
}

void ProcCityGenerator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_params", "params"), &ProcCityGenerator::set_params);
	ClassDB::bind_method(D_METHOD("get_params"), &ProcCityGenerator::get_params);
	ClassDB::bind_method(D_METHOD("set_mesh_size", "size"), &ProcCityGenerator::set_mesh_size);
	ClassDB::bind_method(D_METHOD("get_mesh_size"), &ProcCityGenerator::get_mesh_size);
	ClassDB::bind_method(D_METHOD("set_grid_vertices", "verts"), &ProcCityGenerator::set_grid_vertices);
	ClassDB::bind_method(D_METHOD("get_grid_vertices"), &ProcCityGenerator::get_grid_vertices);
	ClassDB::bind_method(D_METHOD("set_height_scale", "value"), &ProcCityGenerator::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &ProcCityGenerator::get_height_scale);
	ClassDB::bind_method(D_METHOD("set_base_height", "value"), &ProcCityGenerator::set_base_height);
	ClassDB::bind_method(D_METHOD("get_base_height"), &ProcCityGenerator::get_base_height);
	ClassDB::bind_method(D_METHOD("set_build_mode", "mode"), &ProcCityGenerator::set_build_mode);
	ClassDB::bind_method(D_METHOD("get_build_mode"), &ProcCityGenerator::get_build_mode);
	ClassDB::bind_method(D_METHOD("set_sample_filter", "filter"), &ProcCityGenerator::set_sample_filter);
	ClassDB::bind_method(D_METHOD("get_sample_filter"), &ProcCityGenerator::get_sample_filter);
	ClassDB::bind_method(D_METHOD("set_max_cells", "value"), &ProcCityGenerator::set_max_cells);
	ClassDB::bind_method(D_METHOD("get_max_cells"), &ProcCityGenerator::get_max_cells);
	ClassDB::bind_method(D_METHOD("set_hive_warp", "value"), &ProcCityGenerator::set_hive_warp);
	ClassDB::bind_method(D_METHOD("get_hive_warp"), &ProcCityGenerator::get_hive_warp);
	ClassDB::bind_method(D_METHOD("set_hive_jitter", "value"), &ProcCityGenerator::set_hive_jitter);
	ClassDB::bind_method(D_METHOD("get_hive_jitter"), &ProcCityGenerator::get_hive_jitter);
	ClassDB::bind_method(D_METHOD("set_hive_gap", "value"), &ProcCityGenerator::set_hive_gap);
	ClassDB::bind_method(D_METHOD("get_hive_gap"), &ProcCityGenerator::get_hive_gap);
	ClassDB::bind_method(D_METHOD("set_hive_flat_rect", "value"), &ProcCityGenerator::set_hive_flat_rect);
	ClassDB::bind_method(D_METHOD("get_hive_flat_rect"), &ProcCityGenerator::get_hive_flat_rect);
	ClassDB::bind_method(D_METHOD("set_hive_rim_boost", "value"), &ProcCityGenerator::set_hive_rim_boost);
	ClassDB::bind_method(D_METHOD("get_hive_rim_boost"), &ProcCityGenerator::get_hive_rim_boost);
	ClassDB::bind_method(D_METHOD("set_hive_rim_falloff", "value"), &ProcCityGenerator::set_hive_rim_falloff);
	ClassDB::bind_method(D_METHOD("get_hive_rim_falloff"), &ProcCityGenerator::get_hive_rim_falloff);
	ClassDB::bind_method(D_METHOD("set_material_mode", "mode"), &ProcCityGenerator::set_material_mode);
	ClassDB::bind_method(D_METHOD("get_material_mode"), &ProcCityGenerator::get_material_mode);
	ClassDB::bind_method(D_METHOD("set_texture_mode", "mode"), &ProcCityGenerator::set_texture_mode);
	ClassDB::bind_method(D_METHOD("get_texture_mode"), &ProcCityGenerator::get_texture_mode);
	ClassDB::bind_method(D_METHOD("set_normal_strength", "value"), &ProcCityGenerator::set_normal_strength);
	ClassDB::bind_method(D_METHOD("get_normal_strength"), &ProcCityGenerator::get_normal_strength);
	ClassDB::bind_method(D_METHOD("set_uv_scale", "value"), &ProcCityGenerator::set_uv_scale);
	ClassDB::bind_method(D_METHOD("get_uv_scale"), &ProcCityGenerator::get_uv_scale);
	ClassDB::bind_method(D_METHOD("set_roughness", "value"), &ProcCityGenerator::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &ProcCityGenerator::get_roughness);
	ClassDB::bind_method(D_METHOD("set_metallic", "value"), &ProcCityGenerator::set_metallic);
	ClassDB::bind_method(D_METHOD("get_metallic"), &ProcCityGenerator::get_metallic);
	ClassDB::bind_method(D_METHOD("set_texture_filter", "value"), &ProcCityGenerator::set_texture_filter);
	ClassDB::bind_method(D_METHOD("get_texture_filter"), &ProcCityGenerator::get_texture_filter);
	ClassDB::bind_method(D_METHOD("set_texture_repeat", "value"), &ProcCityGenerator::set_texture_repeat);
	ClassDB::bind_method(D_METHOD("get_texture_repeat"), &ProcCityGenerator::get_texture_repeat);
	ClassDB::bind_method(D_METHOD("set_keep_intermediate_png", "value"), &ProcCityGenerator::set_keep_intermediate_png);
	ClassDB::bind_method(D_METHOD("get_keep_intermediate_png"), &ProcCityGenerator::get_keep_intermediate_png);
	ClassDB::bind_method(D_METHOD("set_persist_in_scene", "value"), &ProcCityGenerator::set_persist_in_scene);
	ClassDB::bind_method(D_METHOD("get_persist_in_scene"), &ProcCityGenerator::get_persist_in_scene);
	ClassDB::bind_method(D_METHOD("set_binary_path_override", "value"), &ProcCityGenerator::set_binary_path_override);
	ClassDB::bind_method(D_METHOD("get_binary_path_override"), &ProcCityGenerator::get_binary_path_override);
	ClassDB::bind_method(D_METHOD("set_output_dir", "value"), &ProcCityGenerator::set_output_dir);
	ClassDB::bind_method(D_METHOD("get_output_dir"), &ProcCityGenerator::get_output_dir);

	ClassDB::bind_method(D_METHOD("generate_displacement"), &ProcCityGenerator::generate_displacement);
	ClassDB::bind_method(D_METHOD("build_geometry"), &ProcCityGenerator::build_geometry);
	ClassDB::bind_method(D_METHOD("generate_material"), &ProcCityGenerator::generate_material);
	ClassDB::bind_method(D_METHOD("generate_all"), &ProcCityGenerator::generate_all);
	ClassDB::bind_method(D_METHOD("randomize_palette"), &ProcCityGenerator::randomize_palette);
	ClassDB::bind_method(D_METHOD("clear_generated"), &ProcCityGenerator::clear_generated);
	ClassDB::bind_method(D_METHOD("is_busy"), &ProcCityGenerator::is_busy);

	ClassDB::bind_method(D_METHOD("_thread_body", "job"), &ProcCityGenerator::_thread_body);
	ClassDB::bind_method(D_METHOD("_apply_results", "result"), &ProcCityGenerator::_apply_results);
	ClassDB::bind_method(D_METHOD("_emit_failed", "stage", "message"), &ProcCityGenerator::_emit_failed);

	ClassDB::bind_method(D_METHOD("_btn_generate_displacement"), &ProcCityGenerator::_btn_generate_displacement);
	ClassDB::bind_method(D_METHOD("_btn_build_geometry"), &ProcCityGenerator::_btn_build_geometry);
	ClassDB::bind_method(D_METHOD("_btn_generate_material"), &ProcCityGenerator::_btn_generate_material);
	ClassDB::bind_method(D_METHOD("_btn_generate_all"), &ProcCityGenerator::_btn_generate_all);
	ClassDB::bind_method(D_METHOD("_btn_randomize_palette"), &ProcCityGenerator::_btn_randomize_palette);
	ClassDB::bind_method(D_METHOD("_btn_clear_generated"), &ProcCityGenerator::_btn_clear_generated);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "params", PROPERTY_HINT_RESOURCE_TYPE, "GoplacementxParams"), "set_params", "get_params");

	ADD_GROUP("Mesh", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "mesh_size", PROPERTY_HINT_NONE, "suffix:m"), "set_mesh_size", "get_mesh_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "grid_vertices", PROPERTY_HINT_RANGE, "2,256,1"), "set_grid_vertices", "get_grid_vertices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_scale", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_height_scale", "get_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_base_height", "get_base_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "build_mode", PROPERTY_HINT_ENUM, "ArrayMesh,MultiMesh,CSG,HexHive"), "set_build_mode", "get_build_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sample_filter", PROPERTY_HINT_ENUM, "Nearest,BoxAverage"), "set_sample_filter", "get_sample_filter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_cells", PROPERTY_HINT_RANGE, "1,1048576,1"), "set_max_cells", "get_max_cells");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "persist_in_scene"), "set_persist_in_scene", "get_persist_in_scene");

	ADD_GROUP("Hive", "hive_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_warp", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hive_warp", "get_hive_warp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_jitter", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hive_jitter", "get_hive_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_gap", PROPERTY_HINT_RANGE, "0,0.5,0.01"), "set_hive_gap", "get_hive_gap");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "hive_flat_rect"), "set_hive_flat_rect", "get_hive_flat_rect");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_rim_boost", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_hive_rim_boost", "get_hive_rim_boost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_rim_falloff", PROPERTY_HINT_RANGE, "1,200,0.5"), "set_hive_rim_falloff", "get_hive_rim_falloff");

	ADD_GROUP("Material", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Standard,ORM"), "set_material_mode", "get_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_mode", PROPERTY_HINT_ENUM, "Single,Shared,Channels"), "set_texture_mode", "get_texture_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_normal_strength", "get_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_metallic", "get_metallic");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_scale"), "set_uv_scale", "get_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_filter", PROPERTY_HINT_ENUM, "Nearest,Linear"), "set_texture_filter", "get_texture_filter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "texture_repeat"), "set_texture_repeat", "get_texture_repeat");

	ADD_GROUP("Tool", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "binary_path_override", PROPERTY_HINT_GLOBAL_FILE), "set_binary_path_override", "get_binary_path_override");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "output_dir", PROPERTY_HINT_GLOBAL_DIR), "set_output_dir", "get_output_dir");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "keep_intermediate_png"), "set_keep_intermediate_png", "get_keep_intermediate_png");

	ADD_GROUP("Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_displacement_action", PROPERTY_HINT_TOOL_BUTTON, "Generate Displacement", PROPERTY_USAGE_EDITOR), "", "_btn_generate_displacement");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "build_geometry_action", PROPERTY_HINT_TOOL_BUTTON, "Build Geometry", PROPERTY_USAGE_EDITOR), "", "_btn_build_geometry");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_material_action", PROPERTY_HINT_TOOL_BUTTON, "Generate Material", PROPERTY_USAGE_EDITOR), "", "_btn_generate_material");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_all_action", PROPERTY_HINT_TOOL_BUTTON, "Generate All", PROPERTY_USAGE_EDITOR), "", "_btn_generate_all");
	// Randomize Palette now lives on the params resource, directly under the
	// gradient colours (the "single palette"); see GoplacementxParams.
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "clear_generated_action", PROPERTY_HINT_TOOL_BUTTON, "Clear Generated", PROPERTY_USAGE_EDITOR), "", "_btn_clear_generated");

	ADD_SIGNAL(MethodInfo("generation_started", PropertyInfo(Variant::STRING, "stage")));
	ADD_SIGNAL(MethodInfo("generation_progress", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::FLOAT, "ratio")));
	ADD_SIGNAL(MethodInfo("generation_finished", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("generation_failed", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("all_finished"));

	BIND_ENUM_CONSTANT(BUILD_ARRAY_MESH);
	BIND_ENUM_CONSTANT(BUILD_MULTIMESH);
	BIND_ENUM_CONSTANT(BUILD_CSG);
	BIND_ENUM_CONSTANT(BUILD_HEX);
	BIND_ENUM_CONSTANT(MATERIAL_STANDARD);
	BIND_ENUM_CONSTANT(MATERIAL_ORM);
	BIND_ENUM_CONSTANT(TEX_SINGLE);
	BIND_ENUM_CONSTANT(TEX_SHARED);
	BIND_ENUM_CONSTANT(TEX_CHANNELS);
}

void ProcCityGenerator::set_params(const Ref<GoplacementxParams> &p_params) {
	params = p_params;
}

Ref<GoplacementxParams> ProcCityGenerator::get_params() const {
	return params;
}

Callable ProcCityGenerator::_btn_generate_displacement() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_displacement");
}
Callable ProcCityGenerator::_btn_build_geometry() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "build_geometry");
}
Callable ProcCityGenerator::_btn_generate_material() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_material");
}
Callable ProcCityGenerator::_btn_generate_all() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_all");
}
Callable ProcCityGenerator::_btn_randomize_palette() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "randomize_palette");
}
Callable ProcCityGenerator::_btn_clear_generated() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "clear_generated");
}

void ProcCityGenerator::_ensure_params() {
	if (params.is_null()) {
		params.instantiate();
	}
}

String ProcCityGenerator::_resolve_output_dir() const {
	if (!output_dir.is_empty()) {
		return ProjectSettings::get_singleton()->globalize_path(output_dir);
	}
	return OS::get_singleton()->get_cache_dir();
}

Node *ProcCityGenerator::_get_generated() const {
	return get_node_or_null(NodePath(GENERATED_NAME));
}

void ProcCityGenerator::_set_owner_recursive(Node *p_node, Node *p_owner) {
	if (p_node == nullptr || p_owner == nullptr) {
		return;
	}
	p_node->set_owner(p_owner);
	TypedArray<Node> kids = p_node->get_children();
	for (int i = 0; i < kids.size(); i++) {
		_set_owner_recursive(Object::cast_to<Node>(kids[i]), p_owner);
	}
}

bool ProcCityGenerator::_build_geometry_main() {
	if (_height_image.is_null()) {
		_emit_failed("geometry", "No displacement image; run Generate Displacement first.");
		return false;
	}

	const int cols = MAX(1, grid_vertices.x - 1);
	const int rows = MAX(1, grid_vertices.y - 1);
	const int64_t cell_count = (int64_t)cols * (int64_t)rows;
	if (cell_count > (int64_t)max_cells) {
		_emit_failed("geometry", String("Cell count ") + String::num_int64(cell_count) + String(" exceeds Max Cells ") + String::num_int64(max_cells) + String(". Lower Grid Vertices or raise Max Cells."));
		return false;
	}
	if (build_mode == BUILD_CSG && cell_count > 2048) {
		UtilityFunctions::push_warning(String("[ProcCity] Building ") + String::num_int64(cell_count) + String(" CSG boxes; CSG rebuilds are expensive and may stall the editor."));
	}

	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Node3D *container = nullptr;

	if (build_mode == BUILD_MULTIMESH) {
		Ref<MultiMesh> mm = mesher->build_multimesh(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter);
		if (mm.is_null()) {
			_emit_failed("geometry", "MultiMesh build failed.");
			return false;
		}
		MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
		mmi->set_multimesh(mm);
		container = mmi;
	} else if (build_mode == BUILD_CSG) {
		CSGCombiner3D *comb = memnew(CSGCombiner3D);
		mesher->build_csg(comb, _height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter);
		container = comb;
	} else if (build_mode == BUILD_HEX) {
		Ref<ArrayMesh> mesh = mesher->build_hex_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height,
				sample_filter, hive_warp, hive_jitter, hive_gap, _resolved_seed,
				hive_flat_rect, hive_rim_boost, hive_rim_falloff);
		if (mesh.is_null()) {
			_emit_failed("geometry", "Hex hive build failed.");
			return false;
		}
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(mesh);
		container = mi;
	} else {
		Ref<ArrayMesh> mesh = mesher->build_array_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter);
		if (mesh.is_null()) {
			_emit_failed("geometry", "ArrayMesh build failed.");
			return false;
		}
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(mesh);
		container = mi;
	}

	_install_geometry(container);
	return true;
}

void ProcCityGenerator::_install_geometry(Node3D *p_container) {
	Node *old = _get_generated();
	if (old != nullptr) {
		remove_child(old);
		old->queue_free();
	}

	p_container->set_name(GENERATED_NAME);
	add_child(p_container);

	if (persist_in_scene && Engine::get_singleton()->is_editor_hint() && get_tree() != nullptr) {
		Node *root = get_tree()->get_edited_scene_root();
		if (root != nullptr) {
			_set_owner_recursive(p_container, root);
		}
	}

	if (_material.is_valid()) {
		GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(p_container);
		if (gi != nullptr) {
			gi->set_material_override(_material);
		}
	}
}

void ProcCityGenerator::_apply_material_main() {
	// Resolve the albedo: a CPU-composed RGB image for the per-channel mode,
	// otherwise the palette-coloured map (Single/Shared).
	Ref<Image> albedo_img = _albedo_image;
	if (_material_texture_mode == TEX_CHANNELS) {
		albedo_img = _compose_rgb_albedo();
	}
	if (albedo_img.is_null()) {
		UtilityFunctions::push_warning("[ProcCity] No albedo image to build a material from.");
		return;
	}

	Ref<BaseMaterial3D> mat;
	if (material_mode == MATERIAL_ORM) {
		Ref<ORMMaterial3D> m;
		m.instantiate();
		mat = m;
	} else {
		Ref<StandardMaterial3D> m;
		m.instantiate();
		mat = m;
	}

	mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(albedo_img));

	// Normal mapping: only the colour modes (Single/Shared) emit a normal map.
	if (_normal_image.is_valid() && !_normal_image->is_empty()) {
		mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
		mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, ImageTexture::create_from_image(_normal_image));
		mat->set_normal_scale(normal_strength);
	}

	// Roughness: a grayscale texture in Shared (reusing the height field) and
	// Channels (its dedicated map); a scalar slider otherwise.
	Ref<Image> rough_img;
	if (_material_texture_mode == TEX_SHARED) {
		rough_img = _height_image;
	} else if (_material_texture_mode == TEX_CHANNELS) {
		rough_img = _rough_image;
	}
	if (rough_img.is_valid() && !rough_img->is_empty()) {
		mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, ImageTexture::create_from_image(rough_img));
		mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
		mat->set_roughness(1.0);
	} else {
		mat->set_roughness(roughness);
	}

	mat->set_metallic(metallic);
	mat->set_uv1_scale(Vector3(uv_scale.x, uv_scale.y, 1.0));
	mat->set_texture_filter(texture_filter == 0 ? BaseMaterial3D::TEXTURE_FILTER_NEAREST : BaseMaterial3D::TEXTURE_FILTER_LINEAR);
	mat->set_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT, texture_repeat);
	_material = mat;

	Node *container = _get_generated();
	GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(container);
	if (gi != nullptr) {
		gi->set_material_override(_material);
	}
}

// _compose_rgb_albedo packs the red channel of the R/G/B grayscale maps into a
// single RGB8 image, reproducing the Combine Color node from the design graph.
Ref<Image> ProcCityGenerator::_compose_rgb_albedo() {
	if (_r_image.is_null() || _g_image.is_null() || _b_image.is_null()) {
		return Ref<Image>();
	}
	// Normalise to RGBA8 so each source has a fixed 4-byte stride.
	_r_image->convert(Image::FORMAT_RGBA8);
	_g_image->convert(Image::FORMAT_RGBA8);
	_b_image->convert(Image::FORMAT_RGBA8);

	const int w = _r_image->get_width();
	const int h = _r_image->get_height();
	if (w <= 0 || h <= 0 ||
			_g_image->get_width() != w || _g_image->get_height() != h ||
			_b_image->get_width() != w || _b_image->get_height() != h) {
		UtilityFunctions::push_warning("[ProcCity] R/G/B channel maps have mismatched sizes; cannot compose albedo.");
		return Ref<Image>();
	}

	const PackedByteArray rd = _r_image->get_data();
	const PackedByteArray gd = _g_image->get_data();
	const PackedByteArray bd = _b_image->get_data();
	const uint8_t *rp = rd.ptr();
	const uint8_t *gp = gd.ptr();
	const uint8_t *bp = bd.ptr();

	const int64_t n = (int64_t)w * (int64_t)h;
	PackedByteArray out;
	out.resize(n * 3);
	uint8_t *op = out.ptrw();
	for (int64_t i = 0; i < n; i++) {
		op[i * 3 + 0] = rp[i * 4];
		op[i * 3 + 1] = gp[i * 4];
		op[i * 3 + 2] = bp[i * 4];
	}
	return Image::create_from_data(w, h, false, Image::FORMAT_RGB8, out);
}

void ProcCityGenerator::_start_pipeline(int p_stages, bool p_fresh_seed) {
	_ensure_params();

	if (_busy) {
		_emit_failed("busy", "A generation is already in progress.");
		return;
	}

	if ((p_stages & STAGE_MATERIAL) && !(p_stages & STAGE_HEIGHT) && _height_image.is_null()) {
		_emit_failed("material", "Run Generate Displacement first.");
		return;
	}

	// Oversize grids must fail before a worker spins up (the worker builds
	// geometry for the mesh modes; see _thread_body).
	if (p_stages & STAGE_GEOMETRY) {
		const int cols = MAX(1, grid_vertices.x - 1);
		const int rows = MAX(1, grid_vertices.y - 1);
		const int64_t cell_count = (int64_t)cols * (int64_t)rows;
		if (cell_count > (int64_t)max_cells) {
			_emit_failed("geometry", String("Cell count ") + String::num_int64(cell_count) + String(" exceeds Max Cells ") + String::num_int64(max_cells) + String(". Lower Grid Vertices or raise Max Cells."));
			return;
		}
	}

	Ref<GoplacementxRunner> runner;
	runner.instantiate();
	if (p_fresh_seed) {
		_resolved_seed = runner->resolve_seed(params);
	}

	String label = "all";
	if ((p_stages & STAGE_HEIGHT) && !(p_stages & STAGE_MATERIAL)) {
		label = "displacement";
	} else if (!(p_stages & STAGE_HEIGHT) && (p_stages & STAGE_MATERIAL)) {
		label = "material";
	}
	emit_signal("generation_started", label);

	Dictionary job;
	job["params"] = params;
	job["stages"] = p_stages;
	job["seed"] = _resolved_seed;
	job["texture_mode"] = texture_mode;
	job["dir"] = _resolve_output_dir();
	job["binary_override"] = binary_path_override;
	// Geometry inputs are snapshotted here: the worker must never read member
	// fields the main thread could mutate mid-run.
	job["build_mode"] = build_mode;
	job["mesh_size"] = mesh_size;
	job["grid_vertices"] = grid_vertices;
	job["height_scale"] = height_scale;
	job["base_height"] = base_height;
	job["sample_filter"] = sample_filter;
	job["hive_warp"] = hive_warp;
	job["hive_jitter"] = hive_jitter;
	job["hive_gap"] = hive_gap;
	job["hive_flat_rect"] = hive_flat_rect;
	job["hive_rim_boost"] = hive_rim_boost;
	job["hive_rim_falloff"] = hive_rim_falloff;

	_busy = true;
	_worker.instantiate();
	_worker->start(Callable(this, "_thread_body").bind(job));
}

void ProcCityGenerator::_thread_body(Dictionary p_job) {
	Ref<GoplacementxParams> p = p_job["params"];
	const int stages = p_job["stages"];
	const int64_t seed = p_job["seed"];
	const int tex_mode = p_job["texture_mode"];
	const String dir = p_job["dir"];
	const String binary_override = p_job["binary_override"];

	Ref<GoplacementxRunner> runner;
	runner.instantiate();

	const String binary = runner->find_binary(binary_override);
	if (binary.is_empty()) {
		call_deferred("_emit_failed", "setup", "goplacementx CLI not found. Build it (build.ps1 cli) and place it under addons/procedural_city/goplacementx/<platform>/, or set Binary Path Override.");
		return;
	}

	const String config = runner->write_config(dir, p);
	if (config.is_empty()) {
		call_deferred("_emit_failed", "setup", "Could not write config JSON into " + dir);
		return;
	}

	const uint64_t stamp = Time::get_singleton()->get_ticks_usec();
	const String base = dir.path_join("proc_city_" + String::num_uint64(stamp));
	const uint64_t base_seed = (uint64_t)seed;

	Dictionary result;
	result["stages"] = stages;
	result["seed"] = seed;
	result["texture_mode"] = tex_mode;
	result["dir"] = dir;
	result["config_path"] = config;

	// Assemble the emit list for a single bundled CLI invocation. Path keys are
	// stored on the result so _apply_results / _cleanup_temp can find them.
	Array emits;
	auto add_emit = [&](const String &p_mode, uint64_t p_seed, const String &p_path) {
		Dictionary e;
		e["mode"] = p_mode;
		e["seed"] = (int64_t)p_seed;
		e["path"] = p_path;
		emits.push_back(e);
	};

	if (stages & STAGE_HEIGHT) {
		const String out = base + String("_height.png");
		add_emit("grayscale", base_seed, out);
		result["height_path"] = out;
	}

	if (stages & STAGE_MATERIAL) {
		if (tex_mode == TEX_CHANNELS) {
			const String r_out = base + String("_r.png");
			const String g_out = base + String("_g.png");
			const String b_out = base + String("_b.png");
			const String rough_out = base + String("_rough.png");
			add_emit("grayscale", mix_seed(base_seed, 1), r_out);
			add_emit("grayscale", mix_seed(base_seed, 2), g_out);
			add_emit("grayscale", mix_seed(base_seed, 3), b_out);
			add_emit("grayscale", mix_seed(base_seed, 4), rough_out);
			result["r_path"] = r_out;
			result["g_path"] = g_out;
			result["b_path"] = b_out;
			result["rough_path"] = rough_out;
		} else { // TEX_SINGLE or TEX_SHARED: colour albedo + normal
			const String aout = base + String("_albedo.png");
			const String nout = base + String("_normal.png");
			add_emit("color", base_seed, aout);
			add_emit("normal", base_seed, nout);
			result["albedo_path"] = aout;
			result["normal_path"] = nout;
		}
	}

	const Dictionary r = runner->run_bundle(binary, config, emits, p);
	if ((int)r["code"] != 0) {
		String m = String(r.get("output", ""));
		if (m.strip_edges().is_empty()) {
			m = "goplacementx exited with code " + String::num_int64((int)r.get("code", -1));
		}
		call_deferred("_emit_failed", (stages & STAGE_MATERIAL) ? "material" : "displacement", m);
		return;
	}

	// Load each produced PNG keyed by the *_path entries recorded above.
	struct MapLoad {
		const char *path_key;
		const char *image_key;
		const char *fail_stage;
	};
	static const MapLoad loads[] = {
		{ "height_path", "height_image", "displacement" },
		{ "albedo_path", "albedo_image", "material" },
		{ "normal_path", "normal_image", "material" },
		{ "r_path", "r_image", "material" },
		{ "g_path", "g_image", "material" },
		{ "b_path", "b_image", "material" },
		{ "rough_path", "rough_image", "material" },
	};
	for (const MapLoad &ml : loads) {
		if (!result.has(ml.path_key)) {
			continue;
		}
		const String path = result[ml.path_key];
		if (!FileAccess::file_exists(path)) {
			call_deferred("_emit_failed", ml.fail_stage, "goplacementx did not produce " + path);
			return;
		}
		Ref<Image> img = Image::load_from_file(path);
		if (img.is_null()) {
			call_deferred("_emit_failed", ml.fail_stage, "Failed to load " + path);
			return;
		}
		result[ml.image_key] = img;
	}

	// Mesh geometry is built here, off the main thread: the mesher only
	// touches worker-owned Image/ArrayMesh resources (surface creation goes
	// through the RenderingServer's thread-safe command queue), and the Ref
	// crosses back inside the result Dictionary via call_deferred. CSG and
	// MultiMesh stay on the main thread (node-tree surgery / conservative).
	if ((stages & STAGE_GEOMETRY) && result.has("height_image")) {
		const int mode = p_job["build_mode"];
		if (mode == BUILD_HEX || mode == BUILD_ARRAY_MESH) {
			Ref<Image> height_img = result["height_image"];
			Ref<HeightmapMesher> mesher;
			mesher.instantiate();
			Ref<ArrayMesh> mesh;
			if (mode == BUILD_HEX) {
				mesh = mesher->build_hex_mesh(height_img, p_job["mesh_size"], p_job["grid_vertices"],
						p_job["height_scale"], p_job["base_height"], p_job["sample_filter"],
						p_job["hive_warp"], p_job["hive_jitter"], p_job["hive_gap"], seed,
						p_job["hive_flat_rect"], p_job["hive_rim_boost"], p_job["hive_rim_falloff"]);
			} else {
				mesh = mesher->build_array_mesh(height_img, p_job["mesh_size"], p_job["grid_vertices"],
						p_job["height_scale"], p_job["base_height"], p_job["sample_filter"]);
			}
			if (mesh.is_null()) {
				call_deferred("_emit_failed", "geometry", "Worker mesh build failed.");
				return;
			}
			result["geometry_mesh"] = mesh;
		}
	}

	call_deferred("_apply_results", result);
}

void ProcCityGenerator::_apply_results(Dictionary p_result) {
	const int stages = p_result["stages"];

	if (p_result.has("height_image")) {
		_height_image = p_result["height_image"];
		_last_height_path = String(p_result.get("height_path", ""));
	}
	_resolved_seed = (int64_t)p_result["seed"];

	if (stages & STAGE_HEIGHT) {
		emit_signal("generation_finished", "displacement", _last_height_path);
		emit_signal("generation_progress", "all", 0.5);
	}

	bool geometry_ok = true;
	if (stages & STAGE_GEOMETRY) {
		if (p_result.has("geometry_mesh")) {
			// Worker already built the mesh; the main thread only wraps it.
			Ref<ArrayMesh> mesh = p_result["geometry_mesh"];
			MeshInstance3D *mi = memnew(MeshInstance3D);
			mi->set_mesh(mesh);
			_install_geometry(mi);
		} else {
			geometry_ok = _build_geometry_main();
		}
		if (geometry_ok) {
			emit_signal("generation_finished", "geometry", String());
		}
	}

	if (stages & STAGE_MATERIAL) {
		_material_texture_mode = (int)p_result.get("texture_mode", texture_mode);
		_albedo_image = img_or_null(p_result, "albedo_image");
		_normal_image = img_or_null(p_result, "normal_image");
		_r_image = img_or_null(p_result, "r_image");
		_g_image = img_or_null(p_result, "g_image");
		_b_image = img_or_null(p_result, "b_image");
		_rough_image = img_or_null(p_result, "rough_image");
		_last_albedo_path = String(p_result.get("albedo_path", p_result.get("r_path", "")));
		_last_normal_path = String(p_result.get("normal_path", ""));
	}

	if ((stages & STAGE_APPLY_MATERIAL) && geometry_ok) {
		_apply_material_main();
		emit_signal("generation_finished", "material", _last_albedo_path);
	}

	_cleanup_temp(p_result);

	if (_worker.is_valid()) {
		_worker->wait_to_finish();
		_worker.unref();
	}
	_busy = false;

	emit_signal("generation_progress", "all", 1.0);
	emit_signal("all_finished");
}

void ProcCityGenerator::_cleanup_temp(const Dictionary &p_result) {
	if (keep_intermediate_png) {
		return;
	}
	remove_file(String(p_result.get("height_path", "")));
	remove_file(String(p_result.get("albedo_path", "")));
	remove_file(String(p_result.get("normal_path", "")));
	remove_file(String(p_result.get("r_path", "")));
	remove_file(String(p_result.get("g_path", "")));
	remove_file(String(p_result.get("b_path", "")));
	remove_file(String(p_result.get("rough_path", "")));
	remove_file(String(p_result.get("config_path", "")));
}

void ProcCityGenerator::_emit_failed(String p_stage, String p_message) {
	if (_worker.is_valid()) {
		if (_worker->is_started()) {
			_worker->wait_to_finish();
		}
		_worker.unref();
	}
	_busy = false;
	UtilityFunctions::push_error(String("[ProcCity] ") + p_stage + String(": ") + p_message);
	emit_signal("generation_failed", p_stage, p_message);
}

void ProcCityGenerator::generate_displacement() {
	_start_pipeline(STAGE_HEIGHT, true);
}

void ProcCityGenerator::build_geometry() {
	if (_busy) {
		_emit_failed("geometry", "A generation is already in progress.");
		return;
	}
	emit_signal("generation_started", "geometry");
	if (_build_geometry_main()) {
		emit_signal("generation_finished", "geometry", String());
		emit_signal("all_finished");
	}
}

void ProcCityGenerator::generate_material() {
	_start_pipeline(STAGE_MATERIAL | STAGE_APPLY_MATERIAL, false);
}

void ProcCityGenerator::generate_all() {
	_start_pipeline(STAGE_HEIGHT | STAGE_GEOMETRY | STAGE_MATERIAL | STAGE_APPLY_MATERIAL, true);
}

void ProcCityGenerator::randomize_palette() {
	_ensure_params();
	params->generate_random_palette(4, 0);
	if (!_busy && _height_image.is_valid()) {
		generate_material();
	}
}

void ProcCityGenerator::clear_generated() {
	Node *old = _get_generated();
	if (old != nullptr) {
		remove_child(old);
		old->queue_free();
	}
	_material = Ref<Material>();
}
