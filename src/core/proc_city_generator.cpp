#include "core/proc_city_generator.h"

#include "material/city_material_builder.h"
#include "meshing/height_sampling.h"
#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/csg_combiner3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static const char *GENERATED_NAME = "GeneratedCity";

// Godot rebuilds the whole CSG boolean union on every change, so box counts
// past this stall the editor noticeably; warn before building.
static constexpr int64_t CSG_BOX_WARNING_THRESHOLD = 2048;

static MeshInstance3D *wrap_mesh_in_instance(const Ref<ArrayMesh> &p_mesh) {
	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(p_mesh);
	return mi;
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

void ProcCityGenerator::set_params(const Ref<GoplacementxParams> &p_params) {
	params = p_params;
}

Ref<GoplacementxParams> ProcCityGenerator::get_params() const {
	return params;
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

String ProcCityGenerator::_cell_budget_error() const {
	const int cols = MAX(1, grid_vertices.x - 1);
	const int rows = MAX(1, grid_vertices.y - 1);
	const int64_t cell_count = (int64_t)cols * (int64_t)rows;
	if (cell_count <= (int64_t)max_cells) {
		return String();
	}
	return String("Cell count ") + String::num_int64(cell_count) + String(" exceeds Max Cells ") + String::num_int64(max_cells) + String(". Lower Grid Vertices or raise Max Cells.");
}

bool ProcCityGenerator::_build_geometry_main() {
	if (_height_image.is_null()) {
		_emit_failed("geometry", "No displacement image; run Generate Displacement first.");
		return false;
	}
	const String budget_error = _cell_budget_error();
	if (!budget_error.is_empty()) {
		_emit_failed("geometry", budget_error);
		return false;
	}

	Node3D *container = _make_geometry_container();
	if (container == nullptr) {
		return false;
	}
	_install_geometry(container);
	return true;
}

Node3D *ProcCityGenerator::_make_geometry_container() {
	switch (build_mode) {
		case BUILD_MULTIMESH:
			return _make_multimesh_node();
		case BUILD_CSG:
			return _make_csg_node();
		case BUILD_HEX:
			return _make_hex_node();
		default:
			return _make_blocks_node();
	}
}

Node3D *ProcCityGenerator::_make_multimesh_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<MultiMesh> mm = mesher->build_multimesh(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter, height_power, block_inset);
	if (mm.is_null()) {
		_emit_failed("geometry", "MultiMesh build failed.");
		return nullptr;
	}
	MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
	mmi->set_multimesh(mm);
	return mmi;
}

Node3D *ProcCityGenerator::_make_csg_node() {
	const int cols = MAX(1, grid_vertices.x - 1);
	const int rows = MAX(1, grid_vertices.y - 1);
	const int64_t cell_count = (int64_t)cols * (int64_t)rows;
	if (cell_count > CSG_BOX_WARNING_THRESHOLD) {
		UtilityFunctions::push_warning(String("[ProcCity] Building ") + String::num_int64(cell_count) + String(" CSG boxes; CSG rebuilds are expensive and may stall the editor."));
	}
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	CSGCombiner3D *comb = memnew(CSGCombiner3D);
	mesher->build_csg(comb, _height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter, height_power, block_inset);
	return comb;
}

Node3D *ProcCityGenerator::_make_hex_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> mesh = mesher->build_hex_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height,
												 sample_filter, hive_warp, hive_jitter, hive_gap, _resolved_seed,
												 hive_flat_rect, hive_rim_boost, hive_rim_falloff,
												 height_power, ao_strength, color_variation, hive_floor);
	if (mesh.is_null()) {
		_emit_failed("geometry", "Hex hive build failed.");
		return nullptr;
	}
	return wrap_mesh_in_instance(mesh);
}

Node3D *ProcCityGenerator::_make_blocks_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> mesh = mesher->build_array_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter,
												   height_power, block_inset, _resolved_seed, ao_strength, color_variation);
	if (mesh.is_null()) {
		_emit_failed("geometry", "ArrayMesh build failed.");
		return nullptr;
	}
	return wrap_mesh_in_instance(mesh);
}

bool ProcCityGenerator::_apply_result_geometry(const Dictionary &p_result) {
	if (p_result.has("geometry_mesh")) {
		Ref<ArrayMesh> mesh = p_result["geometry_mesh"];
		_install_geometry(wrap_mesh_in_instance(mesh));
		return true;
	}
	return _build_geometry_main();
}

void ProcCityGenerator::_install_geometry(Node3D *p_container) {
	Node *old = _get_generated();
	if (old != nullptr) {
		remove_child(old);
		old->queue_free();
	}

	p_container->set_name(GENERATED_NAME);
	add_child(p_container);

	if (generate_collision) {
		_attach_collision(p_container);
	}

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

static void add_trimesh_body(Node3D *p_parent, const Ref<Mesh> &p_mesh, int p_collision_layer) {
	if (p_mesh.is_null()) {
		return;
	}
	Ref<ConcavePolygonShape3D> shape = p_mesh->create_trimesh_shape();
	if (shape.is_null()) {
		return;
	}
	StaticBody3D *body = memnew(StaticBody3D);
	body->set_name("CollisionBody");
	body->set_collision_layer(p_collision_layer);
	body->set_collision_mask(0);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("CollisionShape");
	cs->set_shape(shape);
	body->add_child(cs);
	p_parent->add_child(body);
}

void ProcCityGenerator::_attach_trimesh_body(MeshInstance3D *p_mesh_instance) {
	add_trimesh_body(p_mesh_instance, p_mesh_instance->get_mesh(), collision_layer);
}

// MultiMesh instances have no merged mesh; build the equivalent extruded-blocks
// mesh purely as a collision source.
void ProcCityGenerator::_attach_multimesh_body(Node3D *p_container) {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> source = mesher->build_array_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height,
													 sample_filter, height_power, block_inset);
	add_trimesh_body(p_container, source, collision_layer);
}

void ProcCityGenerator::_attach_collision(Node3D *p_container) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_container);
	if (mi != nullptr) {
		_attach_trimesh_body(mi);
		return;
	}
	if (Object::cast_to<MultiMeshInstance3D>(p_container) != nullptr) {
		_attach_multimesh_body(p_container);
		return;
	}
	TypedArray<Node> kids = p_container->get_children();
	for (int i = 0; i < kids.size(); i++) {
		Node3D *child = Object::cast_to<Node3D>(kids[i]);
		if (child != nullptr && Object::cast_to<StaticBody3D>(child) == nullptr) {
			_attach_collision(child);
		}
	}
}

Ref<Image> ProcCityGenerator::_resolve_albedo_image() {
	if (_material_texture_mode == TEX_CHANNELS) {
		return compose_rgb_albedo(_r_image, _g_image, _b_image);
	}
	return _albedo_image;
}

Ref<Image> ProcCityGenerator::_resolve_roughness_image() const {
	if (_material_texture_mode == TEX_SHARED) {
		return _height_image;
	}
	if (_material_texture_mode == TEX_CHANNELS) {
		return _rough_image;
	}
	return Ref<Image>();
}

void ProcCityGenerator::_apply_material_main() {
	CityMaterialSpec spec;
	spec.orm = material_mode == MATERIAL_ORM;
	spec.albedo = _resolve_albedo_image();
	spec.normal_map = _normal_image;
	spec.roughness_map = _resolve_roughness_image();
	spec.normal_strength = normal_strength;
	spec.roughness = roughness;
	spec.metallic = metallic;
	spec.uv_scale = uv_scale;
	spec.filter_nearest = texture_filter == 0;
	spec.repeat = texture_repeat;

	Ref<Material> mat = build_city_material(spec);
	if (mat.is_null()) {
		UtilityFunctions::push_warning("[ProcCity] No albedo image to build a material from.");
		return;
	}
	_material = mat;
	_apply_material_to_generated();
}

void ProcCityGenerator::_apply_material_to_generated() {
	GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(_get_generated());
	if (gi != nullptr) {
		gi->set_material_override(_material);
	}
}

Dictionary ProcCityGenerator::get_cell_heights() const {
	Dictionary result;
	if (_height_image.is_null()) {
		return result;
	}
	const CellGrid grid = make_cell_grid(mesh_size, grid_vertices);
	std::vector<float> heights;
	if (!resolve_cell_heights(_height_image, grid, height_scale, base_height, sample_filter, heights)) {
		return result;
	}
	PackedFloat32Array packed;
	packed.resize((int64_t)heights.size());
	memcpy(packed.ptrw(), heights.data(), heights.size() * sizeof(float));
	result["columns"] = grid.cols;
	result["rows"] = grid.rows;
	result["cell_size"] = Vector2(grid.cw, grid.cd);
	result["origin"] = Vector2(grid.ox, grid.oz);
	result["heights"] = packed;
	return result;
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
	params->randomize_palette();
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
