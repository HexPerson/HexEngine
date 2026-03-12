

#pragma once

#include "BaseComponent.hpp"
#include "../../Scene/Mesh.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"

namespace HexEngine
{
	class LineEdit;	

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

		bool		RenderMesh(Mesh* mesh, MeshRenderFlags flags, int32_t instanceId);

		// Materials
		std::shared_ptr<Material>	GetMaterial() const;
		void						SetMaterial(const std::shared_ptr<Material>& material);
		std::shared_ptr<Material>	CreateMaterial();
		void						DestroyMaterial();

		CullingMode GetShadowCullMode() const;
		void		SetShadowCullMode(CullingMode mode);

		bool IsBoundToBone() const { return _boundBone != nullptr; }
		void BindToBone(BoneInfo* boneInfo) { _boundBone = boneInfo; }
		const math::Matrix& GetOffsetMatrix() const { return _offsetMatrix; }
		const math::Matrix& GetOffsetMatrixTranspose() const { return _offsetMatrixTranspose; }

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

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void SetTextureFromWidget(Mesh* mesh, LineEdit* edit, MaterialTexture type, const fs::path& path);
		void SetMaterialFromWidget(int32_t index, const fs::path& path);
		void DoubleClickMaterial(const std::wstring& path);
		//void ShowTextureBrowserFromWidget(Material::MaterialTexture type);

	private:
		std::shared_ptr<Mesh> _mesh = nullptr;
		std::shared_ptr<Material> _material;
		mutable std::recursive_mutex _lock;

		math::Vector2 _uvScale;
		math::Vector3 _offsetPosition;

		BoneInfo* _boundBone = nullptr;
		math::Matrix _offsetMatrix;
		math::Matrix _offsetMatrixTranspose;

		CullingMode _shadowCullingMode = CullingMode::FrontFace;
		

		/*BlendState _blendState = BlendState::Opaque;
		CullingMode _cullMode = CullingMode::BackFace;
		DepthBufferState _depthState = DepthBufferState::DepthDefault;

		BlendState _previousBlendState = BlendState::Opaque;
		CullingMode _previousCullMode = CullingMode::BackFace;
		DepthBufferState _previousDepthState = DepthBufferState::DepthDefault;*/
	};
}
