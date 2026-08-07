#include "meshing/packed_surface.h"

using namespace godot;

PackedSurface::PackedSurface(int64_t p_vertex_count, int64_t p_index_count, bool p_use_color) :
		use_color(p_use_color) {
	vertices.resize(p_vertex_count);
	normals.resize(p_vertex_count);
	tangents.resize(p_vertex_count * 4);
	uvs.resize(p_vertex_count);
	indices.resize(p_index_count);
	vp = vertices.ptrw();
	np = normals.ptrw();
	tp = tangents.ptrw();
	uvp = uvs.ptrw();
	ip = indices.ptrw();
	if (use_color) {
		colors.resize(p_vertex_count);
		cp = colors.ptrw();
	}
}

int32_t PackedSurface::Writer::push_vertex(const Vector3 &p_pos, const Vector2 &p_uv, const Color &p_col,
		const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w) {
	surface.vp[vi] = p_pos;
	surface.np[vi] = p_normal;
	surface.uvp[vi] = p_uv;
	surface.tp[vi * 4 + 0] = p_tangent.x;
	surface.tp[vi * 4 + 1] = p_tangent.y;
	surface.tp[vi * 4 + 2] = p_tangent.z;
	surface.tp[vi * 4 + 3] = p_tangent_w;
	if (surface.use_color) {
		surface.cp[vi] = p_col;
	}
	return (int32_t)vi++;
}

void PackedSurface::Writer::push_triangle(int32_t p_a, int32_t p_b, int32_t p_c) {
	surface.ip[ii + 0] = p_a;
	surface.ip[ii + 1] = p_b;
	surface.ip[ii + 2] = p_c;
	ii += 3;
}

void PackedSurface::Writer::quad(const Vector3 p_pos[4], const Vector2 p_uv[4], const Color p_col[4],
		const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w) {
	const int32_t a = push_vertex(p_pos[0], p_uv[0], p_col[0], p_normal, p_tangent, p_tangent_w);
	const int32_t b = push_vertex(p_pos[1], p_uv[1], p_col[1], p_normal, p_tangent, p_tangent_w);
	const int32_t c = push_vertex(p_pos[2], p_uv[2], p_col[2], p_normal, p_tangent, p_tangent_w);
	const int32_t d = push_vertex(p_pos[3], p_uv[3], p_col[3], p_normal, p_tangent, p_tangent_w);
	push_triangle(a, b, c);
	push_triangle(a, c, d);
}

void PackedSurface::Writer::fan(const Vector3 *p_pos, const Vector2 *p_uv, int p_corners, const Color &p_col,
		const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w) {
	const int32_t base = push_vertex(p_pos[0], p_uv[0], p_col, p_normal, p_tangent, p_tangent_w);
	for (int k = 1; k < p_corners; k++) {
		push_vertex(p_pos[k], p_uv[k], p_col, p_normal, p_tangent, p_tangent_w);
	}
	for (int k = 1; k < p_corners - 1; k++) {
		push_triangle(base, base + k, base + k + 1);
	}
}

Ref<ArrayMesh> PackedSurface::commit() {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TANGENT] = tangents;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	if (use_color) {
		arrays[Mesh::ARRAY_COLOR] = colors;
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}
