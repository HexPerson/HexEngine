#include "ShadowAtlas.hpp"
#include "../HexEngine.hpp"
#include "IGraphicsDevice.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Light.hpp"
#include "../Entity/Component/Camera.hpp"

#include <algorithm>
#include <d3d11.h>

namespace HexEngine
{
	bool ShadowAtlas::Create()
	{
		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		Destroy();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = kAtlasSize;
		desc.Height = kAtlasSize;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &_atlas)))
			return false;

		// One DSV per tile is not possible on a single subresource - a DSV
		// covers the whole subresource and tiles are carved out with
		// viewports at render time instead. Store ONE DSV; GetTileDsv returns
		// it for every tile and GetTileViewport supplies the carve.
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateDepthStencilView(_atlas, &dsv, &_tileDsvs[0])))
		{
			Destroy();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R32_FLOAT;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(_atlas, &srv, &_atlasSrv)))
		{
			Destroy();
			return false;
		}

		for (auto& tile : _tiles)
			tile = Tile{};
		_frame = 0;

		return true;
	}

	void ShadowAtlas::Destroy()
	{
		if (_tileDsvs[0] != nullptr) { _tileDsvs[0]->Release(); }
		for (auto& d : _tileDsvs)
			d = nullptr;
		if (_atlasSrv != nullptr) { _atlasSrv->Release(); _atlasSrv = nullptr; }
		if (_atlas != nullptr) { _atlas->Release(); _atlas = nullptr; }
	}

	void ShadowAtlas::GetTileViewport(int32_t tileIndex, int32_t& x, int32_t& y, int32_t& w, int32_t& h) const
	{
		x = (tileIndex % kTilesPerRow) * kTileSize;
		y = (tileIndex / kTilesPerRow) * kTileSize;
		w = kTileSize;
		h = kTileSize;
	}

	ID3D11DepthStencilView* ShadowAtlas::GetTileDsv(int32_t) const
	{
		return _tileDsvs[0];
	}

	void ShadowAtlas::SetTileViewProj(int32_t tileIndex, const math::Matrix& viewProj)
	{
		if (tileIndex < 0 || tileIndex >= kTileCount)
			return;
		_tiles[tileIndex].viewProj = viewProj;
		_tiles[tileIndex].hasContent = true;
	}

	int32_t ShadowAtlas::FindContentTile(const Light* light, uint8_t face) const
	{
		const FaceKey key{ light, face };
		for (int32_t i = 0; i < kTileCount; ++i)
		{
			if (_tiles[i].owner == key && _tiles[i].hasContent)
				return i;
		}
		return -1;
	}

	uint64_t ShadowAtlas::ComputeFaceContentHash(const Light* light, uint8_t face, uint64_t sceneGeometryRevision) const
	{
		// v1 invalidation: the light's own state. A light that has not moved or
		// changed keeps its cached tile. Deliberately NOT yet covered: dynamic
		// geometry moving inside a static light's volume - v1 pairs this hash
		// with the scene geometry-revision check in the caller, which
		// invalidates every cached tile on any structural scene change. Finer
		// per-volume tracking is a follow-up.
		// GetWorldTM is non-const on Entity (it lazily rebuilds), so the cast
		// is unavoidable for a logically-read-only hash.
		Entity* ent = const_cast<Light*>(light)->GetEntity();
		if (ent == nullptr)
			return 0;

		const auto& tm = ent->GetWorldTM();
		uint64_t h = 1469598103934665603ull;
		auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };

		const float* m = &tm.m[0][0];
		for (int i = 0; i < 16; ++i)
			mix((uint64_t)*reinterpret_cast<const uint32_t*>(&m[i]));

		const float radius = const_cast<Light*>(light)->GetRadius();
		mix((uint64_t)*reinterpret_cast<const uint32_t*>(&radius));
		mix(face);
		// Scene geometry epoch (user-found gap: an object placed into a
		// lamp's frustum cast nothing - the cached tile never knew). Any
		// mesh/transform mutation bumps the GI geometry revision, which
		// dirties every cached face; the render budget then rolls the
		// refresh over a few frames in priority order. Coarse by design -
		// per-light-volume intersection dirtying is the recorded follow-up.
		mix(sceneGeometryRevision);
		return h;
	}

	const std::vector<ShadowAtlas::FaceAssignment>& ShadowAtlas::AssignTiles(
		const std::vector<Light*>& casters,
		const Camera* camera,
		int32_t renderBudget,
		uint64_t sceneGeometryRevision)
	{
		++_frame;
		_assignments.clear();
		for (auto& t : _tiles)
			t.renderedThisFrame = false;

		if (_atlas == nullptr || camera == nullptr || camera->GetEntity() == nullptr)
			return _assignments;

		const math::Vector3 camPos = camera->GetEntity()->GetPosition();

		// Wanted faces, priority = projected size proxy (radius / distance).
		struct Want { FaceKey key; float priority; };
		std::vector<Want> wants;
		wants.reserve(casters.size() * 6);

		for (Light* light : casters)
		{
			if (light == nullptr || light->GetEntity() == nullptr)
				continue;
			const int32_t faces = std::clamp(light->GetMaxSupportedShadowCascades(), 1, 6);
			const float dist = std::max(0.5f,
				(light->GetEntity()->GetPosition() - camPos).Length());
			const float priority = light->GetRadius() / dist;
			for (int32_t f = 0; f < faces; ++f)
				wants.push_back({ FaceKey{ light, (uint8_t)f }, priority });
		}

		std::sort(wants.begin(), wants.end(),
			[](const Want& a, const Want& b) { return a.priority > b.priority; });
		if ((int32_t)wants.size() > kTileCount)
			wants.resize(kTileCount);

		// Index of current ownership for O(1) reuse.
		std::unordered_map<FaceKey, int32_t, FaceKeyHash> owned;
		for (int32_t i = 0; i < kTileCount; ++i)
			if (_tiles[i].owner.light != nullptr)
				owned.emplace(_tiles[i].owner, i);

		// Pass 1: keep existing tiles (stickiness beats churn - a face that
		// keeps its tile and its hash pays nothing this frame).
		std::vector<const Want*> unplaced;
		for (const Want& want : wants)
		{
			if (auto it = owned.find(want.key); it != owned.end())
			{
				_tiles[it->second].lastUsedFrame = _frame;
				_assignments.push_back({ want.key, it->second, false });
			}
			else
			{
				unplaced.push_back(&want);
			}
		}

		// Pass 2: place the rest into free tiles, then LRU-steal.
		for (const Want* want : unplaced)
		{
			int32_t best = -1;
			uint64_t bestAge = ~0ull;
			for (int32_t i = 0; i < kTileCount; ++i)
			{
				if (_tiles[i].lastUsedFrame == _frame)
					continue; // taken this frame
				if (_tiles[i].owner.light == nullptr) { best = i; break; }
				if (_tiles[i].lastUsedFrame < bestAge) { best = i; bestAge = _tiles[i].lastUsedFrame; }
			}
			if (best < 0)
				break; // atlas fully subscribed by higher priorities

			_tiles[best].owner = want->key;
			_tiles[best].lastUsedFrame = _frame;
			_tiles[best].contentHash = 0;     // stolen tile holds someone else's depth
			_tiles[best].hasContent = false;  // consumers must not sample it yet
			_assignments.push_back({ want->key, best, false });
		}

		// Budgeted dirty marking, priority order.
		int32_t budget = std::max(0, renderBudget);
		for (auto& a : _assignments)
		{
			Tile& tile = _tiles[a.tileIndex];
			const uint64_t hash = ComputeFaceContentHash(a.key.light, a.key.face, sceneGeometryRevision);
			if (tile.contentHash != hash && budget > 0)
			{
				a.needsRender = true;
				tile.contentHash = hash;
				tile.renderedThisFrame = true;
				--budget;
			}
		}

		return _assignments;
	}
}
