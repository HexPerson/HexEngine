#include "ShadowAtlas.hpp"
#include "../HexEngine.hpp"
#include "IGraphicsDevice.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Light.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Scene/Scene.hpp"

#include <algorithm>
#include <d3d11.h>

namespace HexEngine
{
	bool ShadowAtlas::Create(int32_t atlasSize, int32_t tileSize)
	{
		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		Destroy();

		_atlasSize = std::max(256, atlasSize);
		_tileSize = std::clamp(tileSize, 64, _atlasSize);
		_tilesPerRow = _atlasSize / _tileSize;
		_tileCount = _tilesPerRow * _tilesPerRow;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = (UINT)_atlasSize;
		desc.Height = (UINT)_atlasSize;
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
		if (FAILED(device->CreateDepthStencilView(_atlas, &dsv, &_dsv)))
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

		_tiles.assign((size_t)_tileCount, Tile{});
		_frame = 0;

		return true;
	}

	void ShadowAtlas::Destroy()
	{
		if (_dsv != nullptr) { _dsv->Release(); _dsv = nullptr; }
		if (_atlasSrv != nullptr) { _atlasSrv->Release(); _atlasSrv = nullptr; }
		if (_atlas != nullptr) { _atlas->Release(); _atlas = nullptr; }
	}

	void ShadowAtlas::GetTileViewport(int32_t tileIndex, int32_t& x, int32_t& y, int32_t& w, int32_t& h) const
	{
		x = (tileIndex % _tilesPerRow) * _tileSize;
		y = (tileIndex / _tilesPerRow) * _tileSize;
		w = _tileSize;
		h = _tileSize;
	}

	ID3D11DepthStencilView* ShadowAtlas::GetTileDsv(int32_t) const
	{
		return _dsv;
	}

	void ShadowAtlas::SetTileViewProj(int32_t tileIndex, const math::Matrix& viewProj)
	{
		if (tileIndex < 0 || tileIndex >= _tileCount)
			return;
		_tiles[tileIndex].viewProj = viewProj;
		_tiles[tileIndex].hasContent = true;
	}

	int32_t ShadowAtlas::FindContentTile(const Light* light, uint8_t face) const
	{
		const FaceKey key{ light, face };
		for (int32_t i = 0; i < _tileCount; ++i)
		{
			if (_tiles[i].owner == key && _tiles[i].hasContent)
				return i;
		}
		return -1;
	}

	uint64_t ShadowAtlas::ComputeFaceContentHash(const Light* light, uint8_t face) const
	{
		// The light's own state: a light that has not moved or changed keeps
		// its cached tile. Geometry moving inside the light's volume is the
		// caller's job (scene shadow-invalidation journal, see AssignTiles).
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
		return h;
	}

	const std::vector<ShadowAtlas::FaceAssignment>& ShadowAtlas::AssignTiles(
		const std::vector<Light*>& casters,
		const Camera* camera,
		int32_t renderBudget,
		Scene* scene)
	{
		++_frame;
		_assignments.clear();
		for (auto& t : _tiles)
			t.renderedThisFrame = false;

		if (_atlas == nullptr || camera == nullptr || camera->GetEntity() == nullptr)
			return _assignments;

		// World positions: GetPosition() is parent-relative, and lamps are
		// usually children of a post or a building.
		const math::Vector3 camPos = const_cast<Camera*>(camera)->GetEntity()->GetWorldTM().Translation();

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
				(light->GetEntity()->GetWorldTM().Translation() - camPos).Length());
			const float priority = light->GetRadius() / dist;
			for (int32_t f = 0; f < faces; ++f)
				wants.push_back({ FaceKey{ light, (uint8_t)f }, priority });
		}

		// Stable, so a light's faces stay together in face order.
		std::stable_sort(wants.begin(), wants.end(),
			[](const Want& a, const Want& b) { return a.priority > b.priority; });
		// Over-subscribed: keep WHOLE lights only. Half a cube is useless (the
		// consumers need every face), and it would steal tiles from the next
		// light that could have fitted completely.
		if ((int32_t)wants.size() > _tileCount)
		{
			std::vector<Want> kept;
			kept.reserve((size_t)_tileCount);
			for (size_t i = 0; i < wants.size();)
			{
				size_t j = i;
				while (j < wants.size() && wants[j].key.light == wants[i].key.light)
					++j;
				if (kept.size() + (j - i) <= (size_t)_tileCount)
					kept.insert(kept.end(), wants.begin() + i, wants.begin() + j);
				i = j;
			}
			wants.swap(kept);
		}

		// Index of current ownership for O(1) reuse.
		std::unordered_map<FaceKey, int32_t, FaceKeyHash> owned;
		for (int32_t i = 0; i < _tileCount; ++i)
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
			for (int32_t i = 0; i < _tileCount; ++i)
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

		// Budgeted dirty marking, priority order. A face is dirty when the light
		// itself changed (hash) or when shadow-casting geometry moved/changed
		// inside the light's volume since the tile was last known valid (user-
		// found gap: an object placed into a lamp's frustum cast nothing - the
		// cached tile never knew). That second test used to be a single scene-
		// wide revision, so ANY moving mesh anywhere (walking NPCs, traffic)
		// re-dirtied every shadowed light every frame.
		const uint64_t journalSeq = scene != nullptr ? scene->GetShadowDirtySequence() : 0ull;
		int32_t budget = std::max(0, renderBudget);
		for (auto& a : _assignments)
		{
			Tile& tile = _tiles[a.tileIndex];
			const uint64_t hash = ComputeFaceContentHash(a.key.light, a.key.face);
			bool dirty = tile.contentHash != hash;
			if (!dirty && scene != nullptr && tile.dirtySeq < journalSeq)
			{
				// World position: lamp lights are usually children of their post.
				Light* light = const_cast<Light*>(a.key.light);
				const dx::BoundingSphere volume(light->GetEntity()->GetWorldTM().Translation(), light->GetRadius());
				dirty = scene->IsShadowVolumeDirtySince(tile.dirtySeq, volume);
				if (!dirty)
					tile.dirtySeq = journalSeq; // verified clean through now
			}
			if (dirty && budget > 0)
			{
				a.needsRender = true;
				tile.contentHash = hash;
				tile.dirtySeq = journalSeq;
				tile.renderedThisFrame = true;
				--budget;
			}
		}

		return _assignments;
	}
}
