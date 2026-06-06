

#pragma once

#include "BaseComponent.hpp"
#include "../../Scene/Mesh.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"

namespace HexEngine
{
	class LineEdit;	
	class AssetSearch;
	struct AssetSearchResult;

	enum MeshRenderFlags
	{
		MeshRenderNormal		= HEX_BITSET(0),
		MeshRenderShadowMap		= HEX_BITSET(1),
		MeshRenderTransparency	= HEX_BITSET(2)
	};

	DEFINE_ENUM_FLAG_OPERATORS(MeshRenderFlags);

	class HEX_API StaticMeshComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(StaticMeshComponent);

		StaticMeshComponent(Entity* entity);

		StaticMeshComponent(Entity* entity, StaticMeshComponent* clone);

		virtual ~StaticMeshComponent();

		virtual void Destroy() override;

		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		void		ReleaseAllMeshes();

		bool		RenderMesh(Mesh* mesh, MeshRenderFlags flags, int32_t instanceId, Material* cachedMaterial = nullptr);

		// Materials
		std::shared_ptr<Material>	GetMaterial() const;
		void						SetMaterial(const std::shared_ptr<Material>& material);
		std::shared_ptr<Material>	CreateMaterial();
		void						DestroyMaterial();

		CullingMode GetShadowCullMode() const;
		void		SetShadowCullMode(CullingMode mode);

		// IsBoundToBone is the gate that the PVS / Scene render snapshot
		// uses to switch between "cached entity transform" (unbound) and
		// "entity * bone-offset" (bound). Non-const because it performs
		// lazy resolution via TryResolveBoundBone() - a name was
		// deserialized but the BoneInfo* hadn't been resolved yet.
		bool IsBoundToBone();
		void BindToBone(BoneInfo* boneInfo) { _boundBone = boneInfo; }
		// Offset accessors RECOMPUTE the matrix from the current bone
		// pose each call (cheap: one quaternion -> matrix + one
		// translation multiply). The cached _offsetMatrix used to be
		// updated inside the legacy RenderMesh path; that path is no
		// longer in the render pipeline, so the only correct source of
		// truth is _boundBone->Position / Rotation which the
		// SkeletalAnimationComponent updates every animation tick.
		const math::Matrix& GetOffsetMatrix();
		const math::Matrix& GetOffsetMatrixTranspose();

		// Bone-binding by name. `_boundBoneName` is what gets serialized;
		// `_boundBone` (a raw BoneInfo*) is a runtime cache resolved via
		// TryResolveBoundBone() against a SkeletalAnimationComponent found
		// on this entity or a parent (typical: hat/weapon child entity
		// attaches to a bone on the character parent). Setting an empty
		// name clears the binding.
		const std::string& GetBoundBoneName() const { return _boundBoneName; }
		void SetBoundBoneName(const std::string& name);

		/** Walks the entity's component graph (self first, then up the
		 *  parent chain) looking for a SkeletalAnimationComponent whose
		 *  bone map contains `_boundBoneName`. Caches the result in
		 *  `_boundBone`. No-op if `_boundBoneName` is empty or `_boundBone`
		 *  is already set. Returns true on a successful (re-)resolution. */
		bool TryResolveBoundBone();

		/*void				SetBlendState(BlendState state);
		void				SetCullMode(CullingMode mode);
		void				SetDepthState(DepthBufferState state);*/

		/*BlendState			GetBlendState() const;
		CullingMode			GetCullMode() const;
		DepthBufferState	GetDepthState() const;

		void		RestorePreviousRenderState();*/

		// Mesh
		//
		void SetMesh(std::shared_ptr<Mesh> mesh);
		std::shared_ptr<Mesh> GetMesh() const;

		const math::Vector2& GetUVScale() const;
		void SetUVScale(const math::Vector2& uvScale);
		void SetIncludeInGIWhenHidden(bool value);
		bool GetIncludeInGIWhenHidden() const;
		const math::Vector3& GetOffsetPosition() const;
		void SetOffsetPosition(const math::Vector3& offsetPosition);
		const MeshInstanceData& GetCachedInstanceData(Material* material);
		const SimpleMeshInstanceData& GetCachedShadowInstanceData();

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void SetTextureFromWidget(Mesh* mesh, LineEdit* edit, MaterialTexture type, const fs::path& path);
		void SetMaterialFromWidget(int32_t index, const fs::path& path);
		void SetMeshFromWidget(AssetSearch* search, const AssetSearchResult& result);
		void DoubleClickMaterial(const std::wstring& path);
		//void ShowTextureBrowserFromWidget(Material::MaterialTexture type);

	private:
		std::shared_ptr<Mesh> _mesh = nullptr;
		std::shared_ptr<Material> _material;
		mutable std::recursive_mutex _lock;
		bool _includeInGIWhenHidden = false;

		math::Vector2 _uvScale;
		math::Vector3 _offsetPosition;

		BoneInfo* _boundBone = nullptr;
		std::string _boundBoneName;
		math::Matrix _offsetMatrix;
		math::Matrix _offsetMatrixTranspose;

		CullingMode _shadowCullingMode = CullingMode::FrontFace;

		uint64_t _cachedRenderTransformVersion = 0;
		uint64_t _cachedRenderTransformVersionShadow = 0;
		math::Vector4 _cachedRenderColour = math::Vector4::Zero;
		math::Vector2 _cachedRenderUvScale = math::Vector2(0.0f, 0.0f);
		MeshInstanceData _cachedInstanceData = {};
		SimpleMeshInstanceData _cachedShadowInstanceData = {};
		

		/*BlendState _blendState = BlendState::Opaque;
		CullingMode _cullMode = CullingMode::BackFace;
		DepthBufferState _depthState = DepthBufferState::DepthDefault;

		BlendState _previousBlendState = BlendState::Opaque;
		CullingMode _previousCullMode = CullingMode::BackFace;
		DepthBufferState _previousDepthState = DepthBufferState::DepthDefault;*/
	};
}
