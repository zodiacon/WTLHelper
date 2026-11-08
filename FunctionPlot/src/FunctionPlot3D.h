#pragma once

// The function plot's 3D model, window-free: the plotted box (the plane's ranges on three axes, drawn as the cube
// [-1, 1]^3), the orbiting camera and its matrices, and the meshes the renderer draws: surfaces z = F(x, y) and
// (x(u, v), y(u, v), z(u, v)), curves as tubes, points as spheres, the box's edges and its ticks. Matrices are for row
// vectors (p' = p M), with z up, right-handed, and Direct3D's clip space (depth 0 to 1).

#include "FunctionPlotModel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct FplVec3 {
	float x = 0, y = 0, z = 0;
};

// A point in the plane's (space's) coordinates.
struct FplPoint3 {
	double x = 0, y = 0, z = 0;
};

struct FplMatrix4 {
	float m[4][4] = {};
	static FplMatrix4 Identity();
	FplMatrix4 operator*(const FplMatrix4& b) const;
	// (p, 1) M: x, y, z and w
	void Transform(FplVec3 p, float out[4]) const;
};

struct FplBox3 {
	double xMin = -10, xMax = 10, yMin = -10, yMax = 10, zMin = -10, zMax = 10;

	bool Valid() const;
	// To the cube: [-1, 1] on each axis inside the box; clamped to +-4 so floats stay sane (the renderer clips at 1).
	FplVec3 ToBox(double x, double y, double z) const;
	FplVec3 ToBox(FplPoint3 p) const { return ToBox(p.x, p.y, p.z); }
	FplPoint3 ToSpace(FplVec3 b) const;
	void Zoom(double factor);				// about the center, every axis; > 1 zooms in
	void Pan(double dx, double dy);			// the x and y ranges move by this much (space units)
};

// Looking at the cube's center from a distance, yaw radians around z from the +x axis and pitch above the xy plane.
struct FplCamera {
	float yaw = -0.9f;
	float pitch = 0.45f;
	float distance = 6.0f;
	float fov = 0.62f;						// vertical, radians

	FplVec3 Eye() const;
	FplMatrix4 View() const;
	FplMatrix4 Projection(float aspect) const;
	FplMatrix4 ViewProjection(float aspect) const { return View() * Projection(aspect); }
	void Orbit(float dYaw, float dPitch);	// pitch kept short of straight up or down
	// A cube point on a width by height screen (DIPs, y down); false behind the camera.
	bool Project(FplVec3 p, float width, float height, FplPoint& screen) const;
	// The ray from the eye through a screen point (dir unit length).
	void Ray(FplPoint screen, float width, float height, FplVec3& origin, FplVec3& dir) const;
};

struct FplVertex3 {
	FplVec3 pos;							// in the cube
	FplVec3 normal;
	float value = 0;						// 0 to 1: its height in the box, for a heat coloring
};

struct FplMesh {
	std::vector<FplVertex3> vertices;
	std::vector<uint32_t> indices;			// triangles (a line list for FplBoxEdges)
	bool Empty() const { return indices.empty(); }
};

// z = F(x, y) over the box's x and y on an n by n grid. Cells with an undefined corner are left out, and so are those
// across a jump (an edge whose gap doesn't close as it's halved: floor, tan), not the merely steep.
FplMesh FplSurfaceMesh(const std::function<double(double, double)>& F, const FplBox3& box, int n);

// (x, y, z)(u, v) over [u0, u1] x [v0, v1] on an n by n grid, the same way.
FplMesh FplParametricMesh(const std::function<FplPoint3(double, double)>& f, const FplBox3& box, double u0, double u1,
	double v0, double v1, int n);

// F(x, y, z) = 0 over the box on an n^3 grid of cells, each split into the 6 tetrahedra around its diagonal (so
// neighbors' faces match and the surface has no cracks): the crossings found along the grid's edges (shared by the
// tetrahedra around an edge, halved toward the zero), normals from F's gradient. A crossing whose values don't shrink
// toward zero (an asymptote) is dropped with its triangles.
FplMesh FplImplicitMesh(const std::function<double(double, double, double)>& F, const FplBox3& box, int n);

// A space curve at samples + 1 values of t, in the cube, broken where it's undefined or jumps.
std::vector<std::vector<FplVec3>> FplSampleCurve3(const std::function<FplPoint3(double)>& f, const FplBox3& box,
	double t0, double t1, int samples);

// Tubes of the radius (cube units) around polylines, sides around.
FplMesh FplTubes(const std::vector<std::vector<FplVec3>>& lines, float radius, int sides = 8);

// A sphere (for a point).
FplMesh FplSphere(FplVec3 center, float radius, int slices = 12);

// The cube's 12 edges, as a line list.
FplMesh FplBoxEdges();

// The numbers along three of the box's edges, the ones toward the camera, and the axes' names past their ends.
struct FplTick3 {
	FplVec3 at;								// in the cube
	std::wstring text;
	bool name = false;						// "x", "y", "z"
};
std::vector<FplTick3> FplBoxTicks(const FplBox3& box, const FplCamera& camera);

// The nearest of the mesh's triangles the ray meets inside the cube (what's outside it isn't drawn): its distance
// along the ray. false if none.
bool FplIntersect(const FplMesh& mesh, FplVec3 origin, FplVec3 dir, float& t);

// Appends b's vertices and triangles to a.
void FplAppend(FplMesh& a, const FplMesh& b);
