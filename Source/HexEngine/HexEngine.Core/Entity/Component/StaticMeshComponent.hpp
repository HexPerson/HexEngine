

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

	class StaticMeshComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(StaticMeshComponent);

		StaticMeshComponent(Entity* entity);

		StaticMeshComponent(Entity* entity, StaticMeshComponent* clone);

		virtual void Destroy() override;

		void		ReleaseAllMeshes();

		bool		RenderMesh(Mesh* mesh, MeshRenderFlags flags, int32_t instanceId);

		// Materials
		Material*	GetMaterial(int32_t id = 0) const;
		void		SetMaterial(int32_t id, Material* material);
		Material*	CreateMaterial();
		void		DestroyMaterial(int32_t id);
		int32_t		GetNumMaterials() const;

		CullingMode GetShadowCullMode() const;
		void		SetShadowCullMode(CullingMode mode);

		/*void				SetBlendState(BlendState state);
		void				SetCullMode(CullingMode mode);
		void				SetDepthState(DepthBufferState state);*/

		/*BlendState			GetBlendState() const;
		CullingMode			GetCullMode() const;
		DepthBufferState	GetDepthState() const;

		void		RestorePreviousRenderState();*/

		// Mesh
		//
		void SetMesh(Mesh* mesh);
		Mesh* GetMesh() const;

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
		Mesh* _mesh = nullptr;
		int32_t _numMaterials = 0;
		Material* _materials[MeshMaxMaterials] = { nullptr };
		mutable std::recursive_mutex _lock;

		math::Vector2 _uvScale;

		CullingMode _shadowCullingMode = CullingMode::FrontFace;
		

		/*BlendState _blendState = BlendState::Opaque;
		CullingMode _cullMode = CullingMode::BackFace;
		DepthBufferState _depthState = DepthBufferState::DepthDefault;

		BlendState _previousBlendState = BlendState::Opaque;
		CullingMode _previousCullMode = CullingMode::BackFace;
		DepthBufferState _previousDepthState = DepthBufferState::DepthDefault;*/
	};
}
