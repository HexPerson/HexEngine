#pragma once

#include "../Required.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct ID3D11Texture2D;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;

namespace HexEngine
{
	class Light;
	class Scene;
	class Camera;

	/**
	 * Phase 2 shadow atlas (see docs/rendering/aaa-graphics-plan.md, Phase 2).
	 *
	 * One shared depth atlas replacing the per-light dedicated shadow maps:
	 * today every shadow-casting point light permanently owns 6x1024^2 maps
	 * plus a colour mirror (~48 MB per light), MaxShadowCasters caps the
	 * per-frame set at 4, and every collected caster re-renders all of its
	 * faces every frame. The atlas removes the VRAM multiplication and - the
	 * real win - the unconditional re-renders: a face whose content is
	 * unchanged keeps its tile across frames for free.
	 *
	 * v1 layout: 4096^2 R32_TYPELESS, 16 tiles of 1024^2 in a 4x4 grid. A
	 * FACE (one spot map, or one of a point light's six cube faces) is the
	 * allocation unit. Raw D3D11, same convention as ClusteredLighting.
	 */
	class ShadowAtlas
	{
	public:
		static constexpr int32_t kAtlasSize = 4096;
		static constexpr int32_t kTileSize = 1024;
		static constexpr int32_t kTilesPerRow = kAtlasSize / kTileSize;
		static constexpr int32_t kTileCount = kTilesPerRow * kTilesPerRow;

		struct FaceKey
		{
			const Light* light = nullptr;
			uint8_t face = 0; // 0 for spots; 0..5 for point-light cube faces

			bool operator==(const FaceKey& o) const { return light == o.light && face == o.face; }
		};

		struct FaceKeyHash
		{
			size_t operator()(const FaceKey& k) const
			{
				return std::hash<const void*>()(k.light) ^ (size_t(k.face) * 0x9E3779B9u);
			}
		};

		struct Tile
		{
			FaceKey owner;                 // owner.light == nullptr -> free
			uint64_t lastUsedFrame = 0;    // LRU age
			uint64_t contentHash = 0;      // what the tile currently holds (0 = never rendered)
			bool renderedThisFrame = false;
			// View-proj CAPTURED WHEN THE TILE WAS RENDERED - never the
			// light's current matrices. A moved light whose re-render missed
			// the budget keeps depth rendered with the OLD matrices; sampling
			// must use those or every cached tile mis-projects.
			math::Matrix viewProj;
			bool hasContent = false;
		};

		// One entry per face the CURRENT frame's lists reference; consumed by
		// the render loop (which faces to draw where) and packed for the GPU.
		struct FaceAssignment
		{
			FaceKey key;
			int32_t tileIndex = -1;     // atlas tile
			bool needsRender = false;   // false = cached content is still valid
		};

		bool Create();
		void Destroy();

		/**
		 * Assign atlas tiles for this frame's shadow-casting lights.
		 *
		 * @param casters      Shadow-casting lights, any count (the atlas - not
		 *                     MaxShadowCasters - is the capacity limit now).
		 * @param camera       Priority source: faces of closer/larger lights win
		 *                     tiles when the atlas is over-subscribed.
		 * @param renderBudget Max faces marked needsRender this frame; faces
		 *                     whose content hash matches their tile keep cached
		 *                     depth for free and do not count against it.
		 * @return the frame's assignments, priority order.
		 */
		const std::vector<FaceAssignment>& AssignTiles(
			const std::vector<Light*>& casters,
			const Camera* camera,
			int32_t renderBudget);

		/** Viewport (x, y, w, h) of a tile in atlas texels. */
		void GetTileViewport(int32_t tileIndex, int32_t& x, int32_t& y, int32_t& w, int32_t& h) const;

		/** Record the matrices a tile's content was rendered with. */
		void SetTileViewProj(int32_t tileIndex, const math::Matrix& viewProj);

		/**
		 * Tile currently holding VALID content for a face, or -1. Used by the
		 * cluster gather to decide whether a shadowed light can shade on the
		 * clustered path this frame.
		 */
		int32_t FindContentTile(const Light* light, uint8_t face) const;

		const Tile& GetTile(int32_t tileIndex) const { return _tiles[tileIndex]; }

		ID3D11DepthStencilView* GetTileDsv(int32_t tileIndex) const;
		ID3D11ShaderResourceView* GetAtlasSrv() const { return _atlasSrv; }

		uint64_t GetFrameIndex() const { return _frame; }

	private:
		uint64_t ComputeFaceContentHash(const Light* light, uint8_t face) const;

		ID3D11Texture2D* _atlas = nullptr;
		ID3D11ShaderResourceView* _atlasSrv = nullptr;
		// One DSV per tile so a face renders with a plain viewport+DSV bind and
		// no scissor bookkeeping in the mesh path.
		ID3D11DepthStencilView* _tileDsvs[kTileCount] = {};

		Tile _tiles[kTileCount];
		std::vector<FaceAssignment> _assignments;
		uint64_t _frame = 0;
	};
}
