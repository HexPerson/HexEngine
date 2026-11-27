#include "RtScene.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/SkeletalAnimationComponent.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Graphics/Material.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace
{
	using HexEngine::RtScene;

	struct BuildPrim
	{
		float bmin[3];
		float bmax[3];
		float c[3];
	};

	inline float SurfaceArea(const float mn[3], const float mx[3])
	{
		const float dx = std::max(0.0f, mx[0] - mn[0]);
		const float dy = std::max(0.0f, mx[1] - mn[1]);
		const float dz = std::max(0.0f, mx[2] - mn[2]);
		return 2.0f * (dx * dy + dy * dz + dz * dx);
	}

	inline void Grow(float mn[3], float mx[3], const float pmn[3], const float pmx[3])
	{
		for (int a = 0; a < 3; ++a)
		{
			mn[a] = std::min(mn[a], pmn[a]);
			mx[a] = std::max(mx[a], pmx[a]);
		}
	}

	// Binned-SAH BVH2. Children of an interior node are adjacent
	// (leftFirst, leftFirst + 1); leaves hold [leftFirst, leftFirst + count)
	// of `order`. Mirrors RtNode in RtCommon.shader.
	void BuildBvh(const std::vector<BuildPrim>& prims, uint32_t maxLeaf, std::vector<RtScene::GpuNode>& nodes, std::vector<uint32_t>& order)
	{
		const uint32_t n = static_cast<uint32_t>(prims.size());
		nodes.clear();
		order.resize(n);
		for (uint32_t i = 0; i < n; ++i)
			order[i] = i;
		if (n == 0)
			return;
		nodes.reserve(static_cast<size_t>(n) * 2u / std::max(1u, maxLeaf / 2u) + 2u);

		RtScene::GpuNode root = {};
		root.leftFirst = 0;
		root.count = n;
		nodes.push_back(root);

		constexpr int kBins = 12;
		std::vector<uint32_t> work;
		work.reserve(64);
		work.push_back(0);
		while (!work.empty())
		{
			const uint32_t nodeIndex = work.back();
			work.pop_back();
			const uint32_t first = nodes[nodeIndex].leftFirst;
			const uint32_t count = nodes[nodeIndex].count;

			float bmn[3] = { 1e30f, 1e30f, 1e30f };
			float bmx[3] = { -1e30f, -1e30f, -1e30f };
			float cmn[3] = { 1e30f, 1e30f, 1e30f };
			float cmx[3] = { -1e30f, -1e30f, -1e30f };
			for (uint32_t i = first; i < first + count; ++i)
			{
				const BuildPrim& p = prims[order[i]];
				Grow(bmn, bmx, p.bmin, p.bmax);
				Grow(cmn, cmx, p.c, p.c);
			}
			std::memcpy(nodes[nodeIndex].bmin, bmn, sizeof(bmn));
			std::memcpy(nodes[nodeIndex].bmax, bmx, sizeof(bmx));
			if (count <= maxLeaf)
				continue;

			// Best SAH split over all three axes.
			float bestCost = 1e30f;
			int bestAxis = -1;
			int bestBin = -1;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float extent = cmx[axis] - cmn[axis];
				if (extent <= 1e-9f)
					continue;
				uint32_t binCount[kBins] = {};
				float binMin[kBins][3];
				float binMax[kBins][3];
				for (int b = 0; b < kBins; ++b)
				{
					binMin[b][0] = binMin[b][1] = binMin[b][2] = 1e30f;
					binMax[b][0] = binMax[b][1] = binMax[b][2] = -1e30f;
				}
				const float scale = static_cast<float>(kBins) / extent;
				for (uint32_t i = first; i < first + count; ++i)
				{
					const BuildPrim& p = prims[order[i]];
					const int b = std::min(kBins - 1, static_cast<int>((p.c[axis] - cmn[axis]) * scale));
					++binCount[b];
					Grow(binMin[b], binMax[b], p.bmin, p.bmax);
				}
				float leftArea[kBins - 1];
				uint32_t leftCount[kBins - 1];
				float mn[3] = { 1e30f, 1e30f, 1e30f };
				float mx[3] = { -1e30f, -1e30f, -1e30f };
				uint32_t sum = 0;
				for (int b = 0; b < kBins - 1; ++b)
				{
					sum += binCount[b];
					if (binCount[b] > 0)
						Grow(mn, mx, binMin[b], binMax[b]);
					leftCount[b] = sum;
					leftArea[b] = sum > 0 ? SurfaceArea(mn, mx) : 0.0f;
				}
				float rmn[3] = { 1e30f, 1e30f, 1e30f };
				float rmx[3] = { -1e30f, -1e30f, -1e30f };
				uint32_t rsum = 0;
				for (int b = kBins - 1; b > 0; --b)
				{
					rsum += binCount[b];
					if (binCount[b] > 0)
						Grow(rmn, rmx, binMin[b], binMax[b]);
					const float rightArea = rsum > 0 ? SurfaceArea(rmn, rmx) : 0.0f;
					const float cost = leftArea[b - 1] * static_cast<float>(leftCount[b - 1]) + rightArea * static_cast<float>(rsum);
					if (leftCount[b - 1] > 0 && rsum > 0 && cost < bestCost)
					{
						bestCost = cost;
						bestAxis = axis;
						bestBin = b - 1;
					}
				}
			}

			const float parentArea = std::max(SurfaceArea(bmn, bmx), 1e-12f);
			const float splitCost = 0.125f + bestCost / parentArea;
			if (bestAxis < 0 || (splitCost >= static_cast<float>(count) && count <= 16u))
				continue; // leaf (oversized leaves only when splitting cannot help)

			uint32_t mid = first;
			{
				const float extent = cmx[bestAxis] - cmn[bestAxis];
				const float scale = static_cast<float>(kBins) / extent;
				auto it = std::partition(order.begin() + first, order.begin() + first + count, [&](uint32_t pi)
				{
					const int b = std::min(kBins - 1, static_cast<int>((prims[pi].c[bestAxis] - cmn[bestAxis]) * scale));
					return b <= bestBin;
				});
				mid = static_cast<uint32_t>(it - order.begin());
			}
			if (mid == first || mid == first + count)
			{
				mid = first + count / 2u;
				std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count, [&](uint32_t a, uint32_t b)
				{
					return prims[a].c[bestAxis] < prims[b].c[bestAxis];
				});
			}

			const uint32_t left = static_cast<uint32_t>(nodes.size());
			RtScene::GpuNode l = {};
			l.leftFirst = first;
			l.count = mid - first;
			RtScene::GpuNode r = {};
			r.leftFirst = mid;
			r.count = first + count - mid;
			nodes.push_back(l);
			nodes.push_back(r);
			nodes[nodeIndex].leftFirst = left;
			nodes[nodeIndex].count = 0;
			work.push_back(left + 1);
			work.push_back(left);
		}
	}

	float SrgbToLinear(uint8_t v)
	{
		static float table[256] = {};
		static bool init = false;
		if (!init)
		{
			for (int i = 0; i < 256; ++i)
				table[i] = static_cast<float>(std::pow(static_cast<double>(i) / 255.0, 2.2));
			init = true;
		}
		return table[v];
	}

	uint32_t PackHalf2(float a, float b)
	{
		using namespace DirectX::PackedVector;
		return static_cast<uint32_t>(XMConvertFloatToHalf(a)) | (static_cast<uint32_t>(XMConvertFloatToHalf(b)) << 16);
	}
}

namespace HexEngine
{
	HVar r_rtRadius("r_rtRadius", "Ray tracing scene radius around the camera (m) - instances beyond are not traceable", 300.0f, 32.0f, 4000.0f);
	HVar r_rtBlasBudget("r_rtBlasBudget", "Max triangles of new BLAS built per frame (spreads first-load cost)", 400000, 1000, 50000000);
	HVar r_rtDynamic("r_rtDynamic", "Include moving / dynamic-layer meshes (vehicles, props) in the ray tracing scene; their transforms are refit every frame", true, false, true);
	HVar r_rtRespectGiExclude("r_rtRespectGiExclude", "Skip meshes flagged ExcludeFromGI in the ray tracing scene (off: that flag is a voxel-GI workaround for movers)", false, false, true);
	HVar r_rtRebuildSeconds("r_rtRebuildSeconds", "Full instance-tree rebuild interval (picks up spawned / despawned meshes); transforms refit every frame in between", 2.0f, 0.1f, 60.0f);

	RtScene::~RtScene()
	{
		Destroy();
	}

	void RtScene::Destroy()
	{
		SAFE_RELEASE(_blasNodeSrv);
		SAFE_RELEASE(_blasNodeBuffer);
		SAFE_RELEASE(_triSrv);
		SAFE_RELEASE(_triBuffer);
		SAFE_RELEASE(_tlasSrv);
		SAFE_RELEASE(_tlasBuffer);
		SAFE_RELEASE(_instanceSrv);
		SAFE_RELEASE(_instanceBuffer);
		_blasNodeCapacity = _triCapacity = _tlasCapacity = _instanceCapacity = 0;
		_uploadedNodes = _uploadedTris = 0;
		_blas.clear();
		_textures.clear();
		_blasNodes.clear();
		_tris.clear();
		_live.clear();
		_instancesCpu.clear();
		_tlasCpu.clear();
		_tlasNodeCount = 0;
		_instanceCount = 0;
		_lastGeometryRevision = ~0ull;
		_tlasDirty = true;
	}

	const RtScene::TexturePixels* RtScene::GetTexturePixels(const Material* material, bool emission)
	{
		if (material == nullptr)
			return nullptr;
		const auto& texture = material->GetTexture(emission ? MaterialTexture::Emission : MaterialTexture::Albedo);
		if (!texture)
			return nullptr;
		const void* identity = texture.get();
		auto& entry = _textures[identity];
		if (entry.identity == identity)
			return entry.valid ? &entry : nullptr;
		entry.identity = identity;
		int32_t w = 0;
		int32_t h = 0;
		if (!texture->GetPixelsScaled(entry.pixels, 256, w, h) || w <= 0 || h <= 0)
		{
			w = std::max(1, texture->GetWidth());
			h = std::max(1, texture->GetHeight());
			texture->GetPixels(entry.pixels);
		}
		entry.width = w;
		entry.height = h;
		const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(texture->GetFormat());
		entry.bgra = fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
			fmt == DXGI_FORMAT_B8G8R8X8_UNORM || fmt == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		// Decode to linear ONLY for _SRGB formats, which the sampler decodes
		// before the surface shader sees them. A plain UNORM albedo is used
		// raw as linear by DefaultPixel, so decoding it here would bake GI
		// albedo far darker than what is on screen (0.5 -> 0.22). The voxel
		// GI's bake decodes unconditionally - same convention as
		// ClusteredLighting's area-texture blit, which checks the view format.
		entry.srgb = fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
			fmt == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || fmt == DXGI_FORMAT_BC1_UNORM_SRGB ||
			fmt == DXGI_FORMAT_BC2_UNORM_SRGB || fmt == DXGI_FORMAT_BC3_UNORM_SRGB || fmt == DXGI_FORMAT_BC7_UNORM_SRGB;
		entry.valid = entry.pixels.size() >= static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
		return entry.valid ? &entry : nullptr;
	}

	bool RtScene::BuildBlas(const BlasKey& key, BlasEntry& entry)
	{
		const Mesh* mesh = key.mesh;
		const Material* material = key.material;
		const math::Vector2 uvScale(key.uvScaleX, key.uvScaleY);
		const auto& verts = mesh->GetVertices();
		const auto& indices = mesh->GetIndices();
		const uint32_t triCount = indices.empty()
			? static_cast<uint32_t>(verts.size() / 3u)
			: static_cast<uint32_t>(indices.size() / 3u);
		entry.valid = false;
		entry.geometryRevision = mesh->GetGeometryRevision();
		if (triCount == 0 || verts.empty())
			return false;

		const TexturePixels* albedoTex = GetTexturePixels(material, false);
		entry.albedoIdentity = albedoTex ? albedoTex->identity : nullptr;
		math::Vector3 tint(0.5f, 0.5f, 0.5f);
		if (material != nullptr)
		{
			const math::Vector4 t = material->GetGiAlbedoTint();
			tint = math::Vector3(std::clamp(t.x, 0.0f, 1.0f), std::clamp(t.y, 0.0f, 1.0f), std::clamp(t.z, 0.0f, 1.0f));
		}
		math::Vector3 emissiveBase(0.0f, 0.0f, 0.0f);
		const TexturePixels* emissiveTex = nullptr;
		if (material != nullptr && material->GetEmissiveAffectsGI())
		{
			// DefaultPixel emits ONLY with an emission map (map x colour x
			// strength); a colour without a map does not glow on screen, so it
			// must not glow in GI either.
			emissiveTex = GetTexturePixels(material, true);
			if (emissiveTex != nullptr)
			{
				const math::Vector4 e = material->_properties.emissiveColour;
				emissiveBase = math::Vector3(e.x, e.y, e.z) * std::max(0.0f, e.w);
			}
		}

		auto sampleTex = [](const TexturePixels& tex, math::Vector2 uv) -> math::Vector3
		{
			float u = uv.x - std::floor(uv.x);
			float v = uv.y - std::floor(uv.y);
			const int32_t x = std::clamp(static_cast<int32_t>(u * static_cast<float>(tex.width - 1) + 0.5f), 0, tex.width - 1);
			const int32_t y = std::clamp(static_cast<int32_t>(v * static_cast<float>(tex.height - 1) + 0.5f), 0, tex.height - 1);
			const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(tex.width) + static_cast<size_t>(x)) * 4u;
			if (idx + 3u >= tex.pixels.size())
				return math::Vector3(1.0f, 1.0f, 1.0f);
			const int cR = tex.bgra ? 2 : 0;
			const int cB = tex.bgra ? 0 : 2;
			if (tex.srgb)
				return math::Vector3(SrgbToLinear(tex.pixels[idx + cR]), SrgbToLinear(tex.pixels[idx + 1]), SrgbToLinear(tex.pixels[idx + cB]));
			return math::Vector3(tex.pixels[idx + cR], tex.pixels[idx + 1], tex.pixels[idx + cB]) * (1.0f / 255.0f);
		};

		std::vector<GpuTri> tris;
		std::vector<BuildPrim> prims;
		tris.reserve(triCount);
		prims.reserve(triCount);
		for (uint32_t t = 0; t < triCount; ++t)
		{
			const uint32_t i0 = indices.empty() ? t * 3u : indices[t * 3u + 0u];
			uint32_t i1 = indices.empty() ? t * 3u + 1u : indices[t * 3u + 1u];
			uint32_t i2 = indices.empty() ? t * 3u + 2u : indices[t * 3u + 2u];
			if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
				continue;
			const math::Vector3 p0(verts[i0]._position.x, verts[i0]._position.y, verts[i0]._position.z);
			math::Vector3 p1(verts[i1]._position.x, verts[i1]._position.y, verts[i1]._position.z);
			math::Vector3 p2(verts[i2]._position.x, verts[i2]._position.y, verts[i2]._position.z);
			math::Vector3 e1 = p1 - p0;
			math::Vector3 e2 = p2 - p0;
			math::Vector3 flat = e1.Cross(e2);
			if (flat.LengthSquared() <= 1e-16f)
				continue; // degenerate: can never be hit
			const math::Vector3 authored = verts[i0]._normal + verts[i1]._normal + verts[i2]._normal;
			if (authored.LengthSquared() > 1e-8f && flat.Dot(authored) < 0.0f)
			{
				std::swap(p1, p2);
				e1 = p1 - p0;
				e2 = p2 - p0;
			}

			const math::Vector2 uv = (verts[i0]._texcoord + verts[i1]._texcoord + verts[i2]._texcoord) * (1.0f / 3.0f) * uvScale;
			math::Vector3 albedo = tint;
			if (albedoTex != nullptr)
				albedo *= sampleTex(*albedoTex, uv);
			math::Vector3 emissive = emissiveBase;
			if (emissiveTex != nullptr && emissiveBase.LengthSquared() > 0.0f)
				emissive *= sampleTex(*emissiveTex, uv);

			GpuTri tri = {};
			tri.v0[0] = p0.x; tri.v0[1] = p0.y; tri.v0[2] = p0.z;
			tri.e1[0] = e1.x; tri.e1[1] = e1.y; tri.e1[2] = e1.z;
			tri.e2[0] = e2.x; tri.e2[1] = e2.y; tri.e2[2] = e2.z;
			const uint32_t r = static_cast<uint32_t>(std::clamp(albedo.x, 0.0f, 1.0f) * 255.0f + 0.5f);
			const uint32_t g = static_cast<uint32_t>(std::clamp(albedo.y, 0.0f, 1.0f) * 255.0f + 0.5f);
			const uint32_t b = static_cast<uint32_t>(std::clamp(albedo.z, 0.0f, 1.0f) * 255.0f + 0.5f);
			tri.albedo = r | (g << 8) | (b << 16) | (255u << 24);
			tri.emisRG = PackHalf2(emissive.x, emissive.y);
			tri.emisBFlags = PackHalf2(emissive.z, 0.0f);
			tris.push_back(tri);

			BuildPrim prim = {};
			const math::Vector3 mn = math::Vector3::Min(math::Vector3::Min(p0, p1), p2);
			const math::Vector3 mx = math::Vector3::Max(math::Vector3::Max(p0, p1), p2);
			prim.bmin[0] = mn.x; prim.bmin[1] = mn.y; prim.bmin[2] = mn.z;
			prim.bmax[0] = mx.x; prim.bmax[1] = mx.y; prim.bmax[2] = mx.z;
			prim.c[0] = (mn.x + mx.x) * 0.5f; prim.c[1] = (mn.y + mx.y) * 0.5f; prim.c[2] = (mn.z + mx.z) * 0.5f;
			prims.push_back(prim);
		}
		if (tris.empty())
			return false;

		std::vector<GpuNode> nodes;
		std::vector<uint32_t> order;
		BuildBvh(prims, 4u, nodes, order);

		entry.nodeOffset = static_cast<uint32_t>(_blasNodes.size());
		entry.nodeCount = static_cast<uint32_t>(nodes.size());
		entry.triOffset = static_cast<uint32_t>(_tris.size());
		entry.triCount = static_cast<uint32_t>(tris.size());
		_blasNodes.insert(_blasNodes.end(), nodes.begin(), nodes.end());
		for (uint32_t i : order)
			_tris.push_back(tris[i]);
		std::memcpy(entry.boundsMin, nodes[0].bmin, sizeof(entry.boundsMin));
		std::memcpy(entry.boundsMax, nodes[0].bmax, sizeof(entry.boundsMax));
		entry.valid = true;
		return true;
	}

	bool RtScene::EnsureBuffer(ID3D11Buffer*& buffer, ID3D11ShaderResourceView*& srv, uint32_t& capacity,
		uint32_t requiredElements, uint32_t stride, bool dynamic, const void* data, uint32_t count)
	{
		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		bool recreated = false;
		if (buffer == nullptr || capacity < requiredElements)
		{
			SAFE_RELEASE(srv);
			SAFE_RELEASE(buffer);
			const uint32_t newCapacity = std::max<uint32_t>(1024u, requiredElements + requiredElements / 2u);
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = newCapacity * stride;
			desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = stride;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)))
			{
				capacity = 0;
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = newCapacity;
			if (FAILED(device->CreateShaderResourceView(buffer, &srvDesc, &srv)))
			{
				SAFE_RELEASE(buffer);
				capacity = 0;
				return false;
			}
			capacity = newCapacity;
			recreated = true;
		}
		if (dynamic && data != nullptr && count > 0)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, data, static_cast<size_t>(count) * stride);
				context->Unmap(buffer, 0);
			}
		}
		return recreated;
	}

	void RtScene::UploadBlasData()
	{
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		const uint32_t nodeCount = static_cast<uint32_t>(_blasNodes.size());
		const uint32_t triCount = static_cast<uint32_t>(_tris.size());
		if (nodeCount > 0 && EnsureBuffer(_blasNodeBuffer, _blasNodeSrv, _blasNodeCapacity, nodeCount, sizeof(GpuNode), false, nullptr, 0))
			_uploadedNodes = 0;
		if (triCount > 0 && EnsureBuffer(_triBuffer, _triSrv, _triCapacity, triCount, sizeof(GpuTri), false, nullptr, 0))
			_uploadedTris = 0;
		if (_blasNodeBuffer != nullptr && nodeCount > _uploadedNodes)
		{
			D3D11_BOX box = { _uploadedNodes * static_cast<UINT>(sizeof(GpuNode)), 0, 0, nodeCount * static_cast<UINT>(sizeof(GpuNode)), 1, 1 };
			context->UpdateSubresource(_blasNodeBuffer, 0, &box, _blasNodes.data() + _uploadedNodes, 0, 0);
			_uploadedNodes = nodeCount;
		}
		if (_triBuffer != nullptr && triCount > _uploadedTris)
		{
			D3D11_BOX box = { _uploadedTris * static_cast<UINT>(sizeof(GpuTri)), 0, 0, triCount * static_cast<UINT>(sizeof(GpuTri)), 1, 1 };
			context->UpdateSubresource(_triBuffer, 0, &box, _tris.data() + _uploadedTris, 0, 0);
			_uploadedTris = triCount;
		}
	}

	void RtScene::FillInstance(const math::Matrix& world, const BlasEntry& b, GpuInstance& gi, float bmin[3], float bmax[3], uint32_t ownerKey) const
	{
		math::Matrix inv;
		world.Invert(inv);
		// Row-vector convention (p' = p * M): object = world * inv, so
		// object.x = dot(world, inv column 0) + inv._41, etc.
		gi.worldToObject[0] = inv._11; gi.worldToObject[1] = inv._21; gi.worldToObject[2] = inv._31; gi.worldToObject[3] = inv._41;
		gi.worldToObject[4] = inv._12; gi.worldToObject[5] = inv._22; gi.worldToObject[6] = inv._32; gi.worldToObject[7] = inv._42;
		gi.worldToObject[8] = inv._13; gi.worldToObject[9] = inv._23; gi.worldToObject[10] = inv._33; gi.worldToObject[11] = inv._43;
		gi.blasNodeOffset = b.nodeOffset;
		gi.blasTriOffset = b.triOffset;
		gi.flags = 0;
		gi.pad = ownerKey;
		math::Vector3 mn(1e30f, 1e30f, 1e30f);
		math::Vector3 mx(-1e30f, -1e30f, -1e30f);
		for (int c = 0; c < 8; ++c)
		{
			const math::Vector3 corner(
				(c & 1) ? b.boundsMax[0] : b.boundsMin[0],
				(c & 2) ? b.boundsMax[1] : b.boundsMin[1],
				(c & 4) ? b.boundsMax[2] : b.boundsMin[2]);
			const math::Vector3 w = math::Vector3::Transform(corner, world);
			mn = math::Vector3::Min(mn, w);
			mx = math::Vector3::Max(mx, w);
		}
		bmin[0] = mn.x; bmin[1] = mn.y; bmin[2] = mn.z;
		bmax[0] = mx.x; bmax[1] = mx.y; bmax[2] = mx.z;
	}

	void RtScene::UploadTlas()
	{
		_tlasNodeCount = static_cast<uint32_t>(_tlasCpu.size());
		_instanceCount = static_cast<uint32_t>(_instancesCpu.size());
		if (_tlasNodeCount > 0)
			EnsureBuffer(_tlasBuffer, _tlasSrv, _tlasCapacity, _tlasNodeCount, sizeof(GpuNode), true, _tlasCpu.data(), _tlasNodeCount);
		if (_instanceCount > 0)
			EnsureBuffer(_instanceBuffer, _instanceSrv, _instanceCapacity, _instanceCount, sizeof(GpuInstance), true, _instancesCpu.data(), _instanceCount);
	}

	// Per-frame transform refresh between full rebuilds: re-read every
	// instance's entity BY ID (a raw pointer could dangle after a despawn),
	// update moved instances' matrix + AABB, refit the TLAS bottom-up (the
	// builder appends children after their parent, so a reverse sweep sees
	// children first) and re-upload. Returns false when a full rebuild is
	// needed (an instance's entity is gone).
	bool RtScene::Refit(Scene* scene)
	{
		bool changed = false;
		for (size_t i = 0; i < _live.size(); ++i)
		{
			LiveInstance& li = _live[i];
			Entity* entity = scene->TryGetEntity(li.entity);
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;
			const uint64_t version = entity->GetTransformVersion();
			if (version == li.transformVersion)
				continue;
			li.transformVersion = version;
			FillInstance(entity->GetWorldTM(), *li.blas, _instancesCpu[i], li.bmin, li.bmax, li.entity.index + 1u);
			changed = true;
		}
		if (!changed)
			return true;
		for (size_t n = _tlasCpu.size(); n-- > 0;)
		{
			GpuNode& node = _tlasCpu[n];
			float mn[3] = { 1e30f, 1e30f, 1e30f };
			float mx[3] = { -1e30f, -1e30f, -1e30f };
			if (node.count > 0)
			{
				for (uint32_t k = node.leftFirst; k < node.leftFirst + node.count; ++k)
					Grow(mn, mx, _live[k].bmin, _live[k].bmax);
			}
			else
			{
				Grow(mn, mx, _tlasCpu[node.leftFirst].bmin, _tlasCpu[node.leftFirst].bmax);
				Grow(mn, mx, _tlasCpu[node.leftFirst + 1].bmin, _tlasCpu[node.leftFirst + 1].bmax);
			}
			std::memcpy(node.bmin, mn, sizeof(mn));
			std::memcpy(node.bmax, mx, sizeof(mx));
		}
		UploadTlas();
		++_revision;
		return true;
	}

	bool RtScene::Update(Scene* scene, const math::Vector3& cameraPosition)
	{
		if (scene == nullptr || g_pEnv == nullptr || g_pEnv->_graphicsDevice == nullptr ||
			g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return false;

		const float radius = std::max(32.0f, r_rtRadius._val.f32);
		const uint64_t geometryRevision = scene->GetGiGeometryRevision();
		const bool moved = (cameraPosition - _lastBuildCenter).LengthSquared() > (radius * 0.125f) * (radius * 0.125f);
		const auto now = std::chrono::steady_clock::now();
		const bool stale = std::chrono::duration<float>(now - _lastFullBuild).count() >= r_rtRebuildSeconds._val.f32;
		if (!_tlasDirty && !moved && !stale && geometryRevision == _lastGeometryRevision)
		{
			if (Refit(scene))
				return IsReady();
		}

		const auto t0 = std::chrono::high_resolution_clock::now();
		const dx::BoundingBox bounds(cameraPosition, math::Vector3(radius, radius, radius));
		// Every StaticMeshComponent, not the GI spatial grid: the grid omits
		// motion-excluded meshes (traffic), which the voxel GI kept out to
		// avoid rebakes - DDGI handles movers by refit, so it wants them.
		std::vector<StaticMeshComponent*> components;
		scene->GetComponents<StaticMeshComponent>(components);

		std::vector<LiveInstance> live;
		std::vector<math::Matrix> worlds;
		live.reserve(4096);
		worlds.reserve(4096);
		int64_t budget = static_cast<int64_t>(std::max(1000, r_rtBlasBudget._val.i32));
		uint32_t pending = 0;
		float blasMs = 0.0f;

		for (StaticMeshComponent* smc : components)
		{
			// ExcludeFromGI is a VOXEL-GI workaround (its own field description:
			// "use for moving/transient meshes" - movers rebaked clipmaps). A ray
			// tracer handles movers by refit, and anything visible on screen
			// should occlude and bounce, so it is ignored unless asked.
			if (smc == nullptr || smc->IsBoundToBone() || (r_rtRespectGiExclude._val.b && smc->GetExcludeFromGIAuthored()))
				continue;
			if (!r_rtDynamic._val.b && smc->IsGiMotionExcluded())
				continue;
			Entity* entity = smc->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion() || entity->HasA<SkeletalAnimationComponent>())
				continue;
			// Generated HLOD proxies duplicate their (already traced) source
			// meshes as one cluster-sized instance that overlaps everything,
			// and the runtime HLOD switch distance doesn't apply here.
			if (entity->GetName().rfind("HLOD_", 0) == 0)
				continue;
			const Layer layer = entity->GetLayer();
			if (layer == Layer::Sky || layer == Layer::Invisible || layer == Layer::Camera || layer == Layer::Particle ||
				layer == Layer::Trigger || layer == Layer::Water || layer == Layer::Grass)
				continue;
			if (!r_rtDynamic._val.b && layer == Layer::DynamicGeometry)
				continue;
			if (entity->HasFlag(EntityFlags::DoNotRender) && !smc->GetIncludeInGIWhenHidden())
				continue;
			if (!bounds.Intersects(entity->GetWorldAABB()))
				continue;
			const auto meshPtr = smc->GetMesh();
			if (!meshPtr)
				continue;
			const auto materialPtr = smc->GetMaterial();
			const Material* material = materialPtr ? materialPtr.get() : nullptr;
			if (material != nullptr && !material->GetAffectsGI())
				continue;
			// Glass and other blended materials are drawn in the transparent
			// pass; as BVH triangles they would be opaque to probe rays and
			// block exactly the sunlight that comes in through windows.
			if (material != nullptr && material->GetBlendState() != BlendState::Opaque)
				continue;

			const math::Vector2 uvs = smc->GetUVScale();
			BlasKey key{ meshPtr.get(), material, uvs.x, uvs.y };
			BlasEntry& entry = _blas[key];
			const TexturePixels* tex = GetTexturePixels(material, false);
			const void* albedoIdentity = tex ? tex->identity : nullptr;
			if (!entry.valid || entry.geometryRevision != meshPtr->GetGeometryRevision() || entry.albedoIdentity != albedoIdentity)
			{
				if (budget <= 0)
				{
					++pending;
					continue;
				}
				// A rebuilt BLAS is appended; the old range becomes garbage
				// (reclaimed by Destroy on scene change - meshes rarely change).
				const auto b0 = std::chrono::high_resolution_clock::now();
				const bool ok = BuildBlas(key, entry);
				blasMs += std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - b0).count();
				budget -= static_cast<int64_t>(entry.triCount) + 1;
				if (!ok)
					continue;
			}
			LiveInstance li;
			li.entity = entity->GetId();
			li.transformVersion = entity->GetTransformVersion();
			li.blas = &entry;
			live.push_back(li);
			worlds.push_back(entity->GetWorldTM());
		}

		std::vector<GpuInstance> instances(live.size());
		std::vector<BuildPrim> prims(live.size());
		for (size_t i = 0; i < live.size(); ++i)
		{
			FillInstance(worlds[i], *live[i].blas, instances[i], live[i].bmin, live[i].bmax, live[i].entity.index + 1u);
			BuildPrim& p = prims[i];
			std::memcpy(p.bmin, live[i].bmin, sizeof(p.bmin));
			std::memcpy(p.bmax, live[i].bmax, sizeof(p.bmax));
			p.c[0] = (p.bmin[0] + p.bmax[0]) * 0.5f; p.c[1] = (p.bmin[1] + p.bmax[1]) * 0.5f; p.c[2] = (p.bmin[2] + p.bmax[2]) * 0.5f;
		}
		std::vector<uint32_t> order;
		BuildBvh(prims, 2u, _tlasCpu, order);
		_live.resize(order.size());
		_instancesCpu.resize(order.size());
		for (size_t i = 0; i < order.size(); ++i)
		{
			_live[i] = live[order[i]];
			_instancesCpu[i] = instances[order[i]];
		}

		UploadBlasData();
		UploadTlas();

		_tlasDirty = pending > 0;
		++_stats.tlasRebuilds;
		_lastGeometryRevision = geometryRevision;
		_lastBuildCenter = cameraPosition;
		_lastFullBuild = now;
		++_revision;

		_stats.blasCount = 0;
		for (const auto& [k, e] : _blas)
			_stats.blasCount += e.valid ? 1u : 0u;
		_stats.blasTriangles = static_cast<uint32_t>(_tris.size());
		_stats.blasNodes = static_cast<uint32_t>(_blasNodes.size());
		_stats.instances = _instanceCount;
		_stats.pendingBlas = pending;
		_stats.lastBlasBuildMs = blasMs;
		_stats.lastTlasBuildMs = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - t0).count() - blasMs;
		_stats.gpuBytes =
			static_cast<uint64_t>(_blasNodeCapacity) * sizeof(GpuNode) + static_cast<uint64_t>(_triCapacity) * sizeof(GpuTri) +
			static_cast<uint64_t>(_tlasCapacity) * sizeof(GpuNode) + static_cast<uint64_t>(_instanceCapacity) * sizeof(GpuInstance);
		return IsReady();
	}

	void RtScene::BindCS(ID3D11DeviceContext* context) const
	{
		ID3D11ShaderResourceView* srvs[4] = { _blasNodeSrv, _triSrv, _tlasSrv, _instanceSrv };
		context->CSSetShaderResources(52, 4, srvs);
	}

	void RtScene::BindPS(ID3D11DeviceContext* context) const
	{
		ID3D11ShaderResourceView* srvs[4] = { _blasNodeSrv, _triSrv, _tlasSrv, _instanceSrv };
		context->PSSetShaderResources(52, 4, srvs);
	}

	void RtScene::UnbindCS(ID3D11DeviceContext* context)
	{
		ID3D11ShaderResourceView* srvs[4] = {};
		context->CSSetShaderResources(52, 4, srvs);
	}

	void RtScene::UnbindPS(ID3D11DeviceContext* context)
	{
		ID3D11ShaderResourceView* srvs[4] = {};
		context->PSSetShaderResources(52, 4, srvs);
	}
}
