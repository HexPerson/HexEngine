#pragma once

// Ray / triangle maths shared by PhysUtils::RayCast.
//
// Header-only and NOT HEX_API on purpose: HexEngine.Tests compiles it
// directly (no Core link), so keep it free of engine types and of the
// DirectXTK SimpleMath static constants (Vector3::Zero, ::Up, ...), which
// live in the DirectXTK library rather than in the header.
//
// Conventions (measured on real .hmesh files): the engine is right-handed and
// a triangle (a, b, c) faces along (b - a) x (c - a). World matrices are
// row-vector SimpleMath matrices: p_world = p_object * World.

#if __has_include(<SimpleMath.h>)
	#include <SimpleMath.h>
#else
	#include <directxtk/SimpleMath.h>
#endif

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cfloat>
#include <cstring>
#include <vector>
#include <algorithm>

namespace HexEngine
{
	namespace RayMath
	{
		using Vec3 = DirectX::SimpleMath::Vector3;
		using Mat4 = DirectX::SimpleMath::Matrix;

		/** @brief NaN/inf test on the bits - Core builds with /fp:fast, where
		 *  std::isfinite and x - x == 0 style checks may be optimised away. */
		inline bool IsFinite(float f)
		{
			uint32_t u;
			std::memcpy(&u, &f, sizeof(u));
			return (u & 0x7F800000u) != 0x7F800000u;
		}

		/** @brief Double-sided Moller-Trumbore. On a hit, t is the ray
		 *  parameter (o + d * t, t >= 0) and (u, v) are the barycentric weights
		 *  of p1 and p2 (p0 has 1 - u - v). The direction need NOT be unit
		 *  length - t is then in units of |d|. Degenerate triangles and rays
		 *  parallel to the plane never hit. */
		inline bool IntersectTriangle(const Vec3& o, const Vec3& d,
			const Vec3& p0, const Vec3& p1, const Vec3& p2,
			float& t, float& u, float& v)
		{
			const Vec3 e1 = p1 - p0;
			const Vec3 e2 = p2 - p0;
			const Vec3 pv = d.Cross(e2);
			const float det = e1.Dot(pv);
			if (std::fabs(det) < 1e-20f)
				return false;
			const float invDet = 1.0f / det;
			const Vec3 tv = o - p0;
			u = tv.Dot(pv) * invDet;
			if (u < 0.0f || u > 1.0f)
				return false;
			const Vec3 qv = tv.Cross(e1);
			v = d.Dot(qv) * invDet;
			if (v < 0.0f || u + v > 1.0f)
				return false;
			t = e2.Dot(qv) * invDet;
			return t >= 0.0f && IsFinite(t);
		}

		/** @brief Object-space front-face normal (b - a) x (c - a), unnormalised. */
		inline Vec3 FaceNormal(const Vec3& p0, const Vec3& p1, const Vec3& p2)
		{
			return (p1 - p0).Cross(p2 - p0);
		}

		/** @brief Normal -> world space via the inverse-transpose of the world
		 *  matrix (pass the INVERSE world matrix). Correct under rotation and
		 *  non-uniform scale; under a mirroring (negative-determinant) matrix it
		 *  keeps the object's outward side outward. Returns a unit vector, or
		 *  (0,0,0) for a degenerate input. */
		inline Vec3 NormalToWorld(const Vec3& objectNormal, const Mat4& invWorld)
		{
			Vec3 n = Vec3::TransformNormal(objectNormal, invWorld.Transpose());
			const float len = n.Length();
			if (!(len > 1e-30f) || !IsFinite(len))
				return Vec3(0.0f, 0.0f, 0.0f);
			return n / len;
		}

		/** @brief A world ray expressed in an entity's object space. With the
		 *  UN-normalised local direction the map is affine, so the ray parameter
		 *  t is identical in both spaces: a world-unit-direction ray reports
		 *  world distances straight from the object-space test. */
		struct ObjectRay
		{
			Vec3 origin;
			Vec3 direction;
		};

		inline ObjectRay ToObjectSpace(const Vec3& worldOrigin, const Vec3& worldDir, const Mat4& invWorld)
		{
			return ObjectRay{ Vec3::Transform(worldOrigin, invWorld), Vec3::TransformNormal(worldDir, invWorld) };
		}

		/** @brief True when the world matrix can be inverted (no zero scale). */
		inline bool InvertWorld(const Mat4& world, Mat4& outInverse)
		{
			const float det = world.Determinant();
			if (!(std::fabs(det) > 1e-24f) || !IsFinite(det))
				return false;
			world.Invert(outInverse);
			return true;
		}

		/** @brief The ray hit the back of a surface whose front faces along n. */
		inline bool IsBackFace(const Vec3& worldNormal, const Vec3& worldDir)
		{
			return worldNormal.Dot(worldDir) > 0.0f;
		}

		/** @brief Surface steeper than the limit? minNormalY <= -1 disables it. */
		inline bool RejectsSlope(const Vec3& worldNormal, float minNormalY)
		{
			return minNormalY > -1.0f && worldNormal.y < minNormalY;
		}

		struct TriangleHit
		{
			bool hit = false;
			float t = 0.0f;
			uint32_t triangle = 0;
			float u = 0.0f; // barycentric weight of the triangle's 2nd vertex
			float v = 0.0f; // barycentric weight of the 3rd vertex
		};

		/** @brief NEAREST triangle hit (not the first one found) over an indexed
		 *  triangle list, in whatever space the ray and positions share.
		 *
		 *  @param triangleCount  number of triangles (indices / 3)
		 *  @param index(i)       vertex index i of the list (uint32_t)
		 *  @param vertexCount    number of vertices (out-of-range indices skip the triangle)
		 *  @param position(vi)   object-space position of vertex vi (Vec3)
		 *  @param maxT           only hits with t < maxT count
		 *  @param accept(tri, t, p0, p1, p2)  optional per-candidate veto (e.g.
		 *         a slope limit); it is only called for a hit nearer than the
		 *         best so far, so a rejected surface is passed straight through
		 *         and anything behind it can still win. */
		template<typename IndexFn, typename PositionFn, typename AcceptFn>
		TriangleHit NearestTriangle(const ObjectRay& ray, size_t triangleCount, IndexFn&& index,
			size_t vertexCount, PositionFn&& position, float maxT, AcceptFn&& accept)
		{
			TriangleHit best;
			float bestT = maxT;
			for (size_t tri = 0; tri < triangleCount; ++tri)
			{
				const uint32_t i0 = index(tri * 3 + 0);
				const uint32_t i1 = index(tri * 3 + 1);
				const uint32_t i2 = index(tri * 3 + 2);
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
					continue;
				const Vec3 p0 = position(i0), p1 = position(i1), p2 = position(i2);
				float t, u, v;
				if (!IntersectTriangle(ray.origin, ray.direction, p0, p1, p2, t, u, v))
					continue;
				if (!(t < bestT))
					continue;
				if (!accept(static_cast<uint32_t>(tri), t, p0, p1, p2))
					continue;
				bestT = t;
				best.hit = true;
				best.t = t;
				best.triangle = static_cast<uint32_t>(tri);
				best.u = u;
				best.v = v;
			}
			return best;
		}

		template<typename IndexFn, typename PositionFn>
		TriangleHit NearestTriangle(const ObjectRay& ray, size_t triangleCount, IndexFn&& index,
			size_t vertexCount, PositionFn&& position, float maxT)
		{
			return NearestTriangle(ray, triangleCount, index, vertexCount, position, maxT,
				[](uint32_t, float, const Vec3&, const Vec3&, const Vec3&) { return true; });
		}

		/** @brief Bounding volume hierarchy over one mesh's triangles (object
		 *  space), for NearestTriangle-equivalent queries in ~log(n) instead of
		 *  n. Binned-SAH build on the longest centroid axis, <= kMaxLeaf
		 *  triangles per leaf. Immutable after Build(): share it freely between
		 *  threads. Triangle positions are copied in leaf order (36 B/tri) so a
		 *  query never touches the mesh, and stays cache-friendly in Debug.
		 *  Memory is roughly 56 bytes per triangle in total. */
		class TriangleBVH
		{
		public:
			static constexpr uint32_t kMaxLeaf = 4;

			template<typename IndexFn, typename PositionFn>
			void Build(size_t triangleCount, IndexFn&& index, size_t vertexCount, PositionFn&& position)
			{
				_nodes.clear();
				_triIds.clear();
				_triVerts.clear();

				// Runs on the first ray into a big mesh, frequently in Debug
				// builds: flat arrays, raw pointers, explicit compares (no
				// std::min/max calls) in every per-triangle loop.

				// 1. Gather valid triangles (in-range, finite): vertices, AABB, centroid.
				std::vector<uint32_t> ids(triangleCount);
				std::vector<float> verts(triangleCount * 9);   // original-order positions
				std::vector<float> boxes(triangleCount * 6);   // min xyz, max xyz
				std::vector<float> cents(triangleCount * 3);
				uint32_t count = 0;
				{
					uint32_t* I = ids.data();
					float* Vw = verts.data();
					float* Bw = boxes.data();
					float* Cw = cents.data();
					for (size_t tri = 0; tri < triangleCount; ++tri)
					{
						const uint32_t i0 = index(tri * 3 + 0), i1 = index(tri * 3 + 1), i2 = index(tri * 3 + 2);
						if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
							continue;
						const Vec3 a = position(i0), b = position(i1), c = position(i2);
						const float p[9] = { a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z };
						bool finite = true;
						for (int k = 0; k < 9; ++k)
							finite = finite && IsFinite(p[k]);
						if (!finite)
							continue;
						float* v = Vw + size_t(count) * 9;
						for (int k = 0; k < 9; ++k)
							v[k] = p[k];
						float* bx = Bw + size_t(count) * 6;
						float* ce = Cw + size_t(count) * 3;
						for (int ax = 0; ax < 3; ++ax)
						{
							float lo = p[ax], hi = p[ax];
							if (p[ax + 3] < lo) lo = p[ax + 3];
							if (p[ax + 3] > hi) hi = p[ax + 3];
							if (p[ax + 6] < lo) lo = p[ax + 6];
							if (p[ax + 6] > hi) hi = p[ax + 6];
							bx[ax] = lo;
							bx[ax + 3] = hi;
							ce[ax] = (lo + hi) * 0.5f;
						}
						I[count] = static_cast<uint32_t>(tri);
						++count;
					}
				}
				if (count == 0)
					return;

				// order[] is permuted in place; nodes reference ranges of it.
				std::vector<uint32_t> order(count);
				std::vector<uint8_t> binOf(count);
				uint32_t* O = order.data();
				uint8_t* BI = binOf.data();
				for (uint32_t i = 0; i < count; ++i)
					O[i] = i;
				const float* B = boxes.data();
				const float* C = cents.data();

				_nodes.reserve((count / kMaxLeaf + 1) * 2);
				_nodes.push_back(Node{});
				_nodes[0].leftFirst = 0;
				_nodes[0].count = count;

				struct Pending { uint32_t node; uint32_t depth; };
				std::vector<Pending> work;
				work.reserve(128);
				work.push_back({ 0u, 0u });

				constexpr int kBins = 16;
				struct Bin { float mn[3]; float mx[3]; uint32_t n; };
				auto area = [](const float* mn, const float* mx)
				{
					const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
					return (dx < 0.0f) ? 0.0f : (dx * dy + dy * dz + dz * dx);
				};

				while (!work.empty())
				{
					const Pending job = work.back();
					work.pop_back();
					const uint32_t first = _nodes[job.node].leftFirst, n = _nodes[job.node].count;

					// 2. Node bounds (from triangle boxes) and centroid bounds.
					float bmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, bmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
					float cmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, cmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
					for (uint32_t i = first; i < first + n; ++i)
					{
						const float* bx = B + size_t(O[i]) * 6;
						const float* ce = C + size_t(O[i]) * 3;
						for (int ax = 0; ax < 3; ++ax)
						{
							if (bx[ax] < bmin[ax]) bmin[ax] = bx[ax];
							if (bx[ax + 3] > bmax[ax]) bmax[ax] = bx[ax + 3];
							if (ce[ax] < cmin[ax]) cmin[ax] = ce[ax];
							if (ce[ax] > cmax[ax]) cmax[ax] = ce[ax];
						}
					}
					{
						Node& node = _nodes[job.node];
						for (int ax = 0; ax < 3; ++ax)
						{
							// Pad by a hair so float rounding in the slab test can't
							// cull a hit the triangle test would accept (edge grazes,
							// zero-thickness nodes of flat terrain).
							const float m = std::fabs(bmin[ax]) > std::fabs(bmax[ax]) ? std::fabs(bmin[ax]) : std::fabs(bmax[ax]);
							const float ext = bmax[ax] - bmin[ax];
							const float pad = 1e-5f * (m > ext ? m : ext) + 1e-6f;
							node.bmin[ax] = bmin[ax] - pad;
							node.bmax[ax] = bmax[ax] + pad;
						}
					}

					if (n <= kMaxLeaf)
						continue; // leaf

					int axis = 0;
					if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) axis = 1;
					if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) axis = 2;
					const float extent = cmax[axis] - cmin[axis];

					uint32_t mid = first;
					if (extent > 0.0f && job.depth < kMaxSahDepth)
					{
						// 3. Binned SAH along the longest centroid axis.
						Bin bins[kBins];
						for (int b = 0; b < kBins; ++b)
						{
							bins[b].n = 0;
							for (int ax = 0; ax < 3; ++ax) { bins[b].mn[ax] = FLT_MAX; bins[b].mx[ax] = -FLT_MAX; }
						}
						const float scale = kBins / extent;
						const float cmin0 = cmin[axis];
						for (uint32_t i = first; i < first + n; ++i)
						{
							const uint32_t t = O[i];
							int b = static_cast<int>((C[size_t(t) * 3 + axis] - cmin0) * scale);
							b = b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
							BI[t] = static_cast<uint8_t>(b);
							Bin& bin = bins[b];
							++bin.n;
							const float* bx = B + size_t(t) * 6;
							for (int ax = 0; ax < 3; ++ax)
							{
								if (bx[ax] < bin.mn[ax]) bin.mn[ax] = bx[ax];
								if (bx[ax + 3] > bin.mx[ax]) bin.mx[ax] = bx[ax + 3];
							}
						}
						// Sweep: cost of splitting after bin s = A(left)*N(left) + A(right)*N(right).
						float leftArea[kBins - 1];
						uint32_t leftN[kBins - 1];
						{
							float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
							uint32_t acc = 0;
							for (int s = 0; s < kBins - 1; ++s)
							{
								acc += bins[s].n;
								for (int ax = 0; ax < 3; ++ax)
								{
									if (bins[s].mn[ax] < mn[ax]) mn[ax] = bins[s].mn[ax];
									if (bins[s].mx[ax] > mx[ax]) mx[ax] = bins[s].mx[ax];
								}
								leftArea[s] = area(mn, mx);
								leftN[s] = acc;
							}
						}
						float bestCost = FLT_MAX;
						int bestSplit = -1;
						{
							float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
							uint32_t acc = 0;
							for (int s = kBins - 1; s >= 1; --s)
							{
								acc += bins[s].n;
								for (int ax = 0; ax < 3; ++ax)
								{
									if (bins[s].mn[ax] < mn[ax]) mn[ax] = bins[s].mn[ax];
									if (bins[s].mx[ax] > mx[ax]) mx[ax] = bins[s].mx[ax];
								}
								const uint32_t ln = leftN[s - 1];
								if (ln == 0 || acc == 0)
									continue;
								const float cost = leftArea[s - 1] * ln + area(mn, mx) * acc;
								if (cost < bestCost) { bestCost = cost; bestSplit = s - 1; }
							}
						}
						if (bestSplit >= 0)
						{
							// Small node whose best split is no better than a leaf: stop.
							if (n <= kMaxLeaf * 4 && bestCost >= area(bmin, bmax) * n)
								continue;
							uint32_t* lo = O + first;
							uint32_t* hi = O + first + n;
							while (lo < hi)
							{
								if (BI[*lo] <= bestSplit)
									++lo;
								else
								{
									--hi;
									const uint32_t tmp = *lo; *lo = *hi; *hi = tmp;
								}
							}
							mid = static_cast<uint32_t>(lo - O);
						}
					}
					if (mid == first || mid == first + n)
					{
						// Degenerate (coincident centroids) or too deep: balanced
						// median split by index along the axis - always progresses.
						mid = first + n / 2;
						std::nth_element(O + first, O + mid, O + first + n, [&](uint32_t a, uint32_t b)
						{
							return C[size_t(a) * 3 + axis] < C[size_t(b) * 3 + axis];
						});
					}

					const uint32_t left = static_cast<uint32_t>(_nodes.size());
					_nodes.push_back(Node{});
					_nodes.push_back(Node{});
					_nodes[job.node].leftFirst = left;
					_nodes[job.node].count = 0;
					_nodes[left].leftFirst = first;
					_nodes[left].count = mid - first;
					_nodes[left + 1].leftFirst = mid;
					_nodes[left + 1].count = first + n - mid;
					work.push_back({ left + 1, job.depth + 1 });
					work.push_back({ left, job.depth + 1 });
				}

				// 4. Copy triangles into leaf order: leaves index _triIds/_triVerts directly.
				_triIds.resize(count);
				_triVerts.resize(size_t(count) * 9);
				uint32_t* TI = _triIds.data();
				float* TV = _triVerts.data();
				const uint32_t* I = ids.data();
				const float* V = verts.data();
				for (uint32_t i = 0; i < count; ++i)
				{
					TI[i] = I[O[i]];
					const float* src = V + size_t(O[i]) * 9;
					float* dst = TV + size_t(i) * 9;
					for (int k = 0; k < 9; ++k)
						dst[k] = src[k];
				}
			}

			bool Empty() const { return _nodes.empty(); }
			size_t NodeCount() const { return _nodes.size(); }
			size_t TriangleCount() const { return _triIds.size(); }
			size_t MemoryBytes() const { return _nodes.size() * sizeof(Node) + _triIds.size() * sizeof(uint32_t) + _triVerts.size() * sizeof(float); }

			/** @brief Same contract as NearestTriangle (triangle = index in the
			 *  ORIGINAL list, accept() vetoes candidates nearer than the best). */
			template<typename AcceptFn>
			TriangleHit Intersect(const ObjectRay& ray, float maxT, AcceptFn&& accept) const
			{
				TriangleHit best;
				if (_nodes.empty())
					return best;
				float bestT = maxT;

				const float o[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
				float inv[3];
				const float d[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
				for (int a = 0; a < 3; ++a)
				{
					// A zero component gets a huge-but-finite reciprocal: no 0*inf NaNs.
					const float da = (std::fabs(d[a]) < 1e-30f) ? (d[a] < 0.0f ? -1e-30f : 1e-30f) : d[a];
					inv[a] = 1.0f / da;
				}

				const Node* nodes = _nodes.data();
				const float* tv = _triVerts.data();
				auto slab = [&](const Node& nd, float& tEntry)
				{
					float t0 = 0.0f, t1 = bestT;
					for (int a = 0; a < 3; ++a)
					{
						float ta = (nd.bmin[a] - o[a]) * inv[a];
						float tb = (nd.bmax[a] - o[a]) * inv[a];
						if (ta > tb) std::swap(ta, tb);
						t0 = ta > t0 ? ta : t0;
						t1 = tb < t1 ? tb : t1;
						if (t0 > t1)
							return false;
					}
					tEntry = t0;
					return true;
				};

				struct Item { uint32_t node; float t; };
				Item stack[kStackSize];
				int sp = 0;
				float rootT;
				if (!slab(nodes[0], rootT))
					return best;
				stack[sp++] = { 0u, rootT };

				while (sp > 0)
				{
					const Item it = stack[--sp];
					if (!(it.t <= bestT))
						continue;
					const Node& nd = nodes[it.node];
					if (nd.count > 0)
					{
						for (uint32_t i = nd.leftFirst; i < nd.leftFirst + nd.count; ++i)
						{
							const float* p = tv + size_t(i) * 9;
							const Vec3 p0(p[0], p[1], p[2]), p1(p[3], p[4], p[5]), p2(p[6], p[7], p[8]);
							float t, u, v;
							if (!IntersectTriangle(ray.origin, ray.direction, p0, p1, p2, t, u, v))
								continue;
							if (!(t < bestT))
								continue;
							if (!accept(_triIds[i], t, p0, p1, p2))
								continue;
							bestT = t;
							best.hit = true;
							best.t = t;
							best.triangle = _triIds[i];
							best.u = u;
							best.v = v;
						}
						continue;
					}
					float tl, tr;
					const bool hl = slab(nodes[nd.leftFirst], tl);
					const bool hr = slab(nodes[nd.leftFirst + 1], tr);
					// Push the far child first so the near one is popped next.
					if (hl && hr)
					{
						if (tl <= tr) { stack[sp++] = { nd.leftFirst + 1, tr }; stack[sp++] = { nd.leftFirst, tl }; }
						else          { stack[sp++] = { nd.leftFirst, tl };     stack[sp++] = { nd.leftFirst + 1, tr }; }
					}
					else if (hl) stack[sp++] = { nd.leftFirst, tl };
					else if (hr) stack[sp++] = { nd.leftFirst + 1, tr };
				}
				return best;
			}

			TriangleHit Intersect(const ObjectRay& ray, float maxT) const
			{
				return Intersect(ray, maxT, [](uint32_t, float, const Vec3&, const Vec3&, const Vec3&) { return true; });
			}

		private:
			// Past this depth splits are balanced medians, so the tree depth
			// (and the traversal stack) is bounded by kMaxSahDepth + log2(n).
			static constexpr uint32_t kMaxSahDepth = 40;
			static constexpr int kStackSize = 128;

			struct Node
			{
				float bmin[3];
				uint32_t leftFirst; // interior: index of left child (right = +1); leaf: first triangle
				float bmax[3];
				uint32_t count;     // 0 = interior
			};

			std::vector<Node> _nodes;
			std::vector<uint32_t> _triIds;
			std::vector<float> _triVerts;
		};
	}
}
