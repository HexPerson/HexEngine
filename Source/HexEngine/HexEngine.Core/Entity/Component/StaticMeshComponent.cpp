

#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Terrain/TerrainGenerator.hpp"
#include "../../Graphics/MaterialLoader.hpp"

namespace HexEngine
{
	StaticMeshComponent::StaticMeshComponent(Entity* entity) :
		BaseComponent(entity)
	{
		_uvScale = math::Vector2(1.0f, 1.0f);
	}

	StaticMeshComponent::StaticMeshComponent(Entity* entity, StaticMeshComponent* clone) :
		BaseComponent(entity)
	{
		_uvScale = clone->_uvScale;

		SetMesh(clone->GetMesh());

		SetMaterial(clone->GetMaterial());
	}

	StaticMeshComponent::~StaticMeshComponent()
	{
		if(_mesh)
			LOG_DEBUG("Mesh '%s' now has a ref count of %d (will be %d after this)", _mesh->GetName().c_str(), _mesh.use_count(), _mesh.use_count()-1);
	}

	bool StaticMeshComponent::RenderMesh(Mesh* mesh, MeshRenderFlags flags, int32_t instanceId)
	{
		// make sure this MeshRenderer can actually render this Mesh
		if (mesh != _mesh.get())
			return false;

		auto material = GetMaterial();

		if (!material)
		{
			LOG_WARN("Cannot render a mesh without a valid material!");
			return false;
		}

		auto shader = material->GetStandardShader();

		bool isShadowMap = (flags & MeshRenderFlags::MeshRenderShadowMap) != 0;

		if (isShadowMap)
			shader = material->GetShadowMapShader();

		if (!shader)
		{
			LOG_WARN("Cannot render a mesh without a valid shader, please check the material has a shader applied!");
			return false;
		}

		auto graphicsDevice = g_pEnv->_graphicsDevice;

		// prepare the graphics device for rendering the mesh
		//
		graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
		graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));
		graphicsDevice->SetInputLayout(shader->GetInputLayout());

		// Set the correct render state
		//
		material->SaveRenderState();

		graphicsDevice->SetBlendState(material->GetBlendState());
		graphicsDevice->SetDepthBufferState(material->GetDepthState());
		//graphicsDevice->SetCullingMode((flags & MeshRenderFlags::MeshRenderShadowMap) != 0 ? CullingMode::FrontFace : material->GetCullMode());

		graphicsDevice->SetCullingMode((flags & MeshRenderFlags::MeshRenderShadowMap) != 0 ? _shadowCullingMode : material->GetCullMode());

		// Update the per-object constant buffer
		//
		math::Matrix offsetMatrix;
		
		if (_boundBone)
		{
			_offsetMatrix = math::Matrix::CreateFromQuaternion(_boundBone->Rotation) * math::Matrix::CreateTranslation(_boundBone->Position);//_boundBone->FinalTransformation.Invert().Transpose();//* math::Matrix::CreateTranslation(_boundBone->Position);
		}
		//else
		//{
		//	offsetMatrix = math::Matrix::CreateTranslation(_offsetPosition);
		//}

		mesh->UpdateConstantBuffer(GetEntity(), /*GetEntity()->GetLocalTM() **/ offsetMatrix, material.get(), instanceId);

		// bind the shader's requirements
		auto requirements = shader->GetRequirements();

		if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresGBuffer))
		{
			g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();
		}

		if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresShadowMaps))
		{
			g_pEnv->_sceneRenderer->GetCurrentShadowMap()->BindAsShaderResource();
		}

		if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresBeauty))
		{
			graphicsDevice->SetTexture2D(g_pEnv->_sceneRenderer->GetBeautyTexture());
		}

		uint32_t slotIdx = graphicsDevice->GetBoundResourceIndex();

		// Set the textures
		//
		auto materialSet = GetMaterial();

		graphicsDevice->SetTexture2D(slotIdx + 0, materialSet->GetTexture(MaterialTexture::Albedo).get());

		graphicsDevice->SetTexture2D(slotIdx + 1, materialSet->GetTexture(MaterialTexture::Normal).get());

		graphicsDevice->SetTexture2D(slotIdx + 2, materialSet->GetTexture(MaterialTexture::Roughness).get());

		graphicsDevice->SetTexture2D(slotIdx + 3, materialSet->GetTexture(MaterialTexture::Metallic).get());

		graphicsDevice->SetTexture2D(slotIdx + 4, materialSet->GetTexture(MaterialTexture::Height).get());

		graphicsDevice->SetTexture2D(slotIdx + 5, materialSet->GetTexture(MaterialTexture::Emission).get());

		graphicsDevice->SetTexture2D(slotIdx + 6, materialSet->GetTexture(MaterialTexture::Opacity).get());

		graphicsDevice->SetTexture2D(slotIdx + 7, materialSet->GetTexture(MaterialTexture::AmbientOcclusion).get());

		slotIdx += MaterialTexture::Count;

		mesh->SetBuffers(isShadowMap);
		return true;
	}

	/*void MeshRenderer::RestorePreviousRenderState()
	{
		auto graphicsDevice = g_pEnv->_graphicsDevice;

		graphicsDevice->SetBlendState(_previousBlendState);
		graphicsDevice->SetDepthBufferState(_previousDepthState);
		graphicsDevice->SetCullingMode(_previousCullMode);
	}*/

	void StaticMeshComponent::Destroy()
	{
		ReleaseAllMeshes();
	}

	void StaticMeshComponent::ReleaseAllMeshes()
	{
		//_mesh.reset();
	}

	void StaticMeshComponent::SetMesh(std::shared_ptr<Mesh> mesh)
	{
		_mesh = mesh;

		dx::BoundingBox bbox = mesh->GetAABB();

		GetEntity()->RecalculateBoundingVolumes(bbox);

		if (mesh->GetMaterial())
			SetMaterial(mesh->GetMaterial());

		// Set a default material
		if (GetMaterial() == nullptr)
		{ 
			SetMaterial(Material::Create("EngineData.Materials/Default.hmat"));
		}

		mesh->CreateInstance();
	}

	std::shared_ptr<Mesh> StaticMeshComponent::GetMesh() const
	{
		return _mesh;
	}

	void StaticMeshComponent::Serialize(json& data, JsonFile* file)
	{
		auto mesh = GetMesh();		

		if (!mesh)
			return;

		json& meshDataArray = data["mesh"];
		
		auto path = mesh->GetFileSystemPath();

		auto& meshData = meshDataArray[path.string()];

		if (path == "TERRAIN")
		{
			//mesh->_terrainParams.Save(meshData["terrain"], file);
		}

		auto& materials = meshData["materials"];

		auto material = GetMaterial();

		if (material)
			materials.push_back(material->GetFileSystemPath().string());

		SERIALIZE_VALUE(_uvScale);
		SERIALIZE_VALUE(_shadowCullingMode);
		SERIALIZE_VALUE(_offsetPosition);
	}

	void StaticMeshComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		_serializationState = BaseComponent::SerializationState::Deserializing;

		for(auto& mesh : data["mesh"].items())
		{
			auto path = mesh.key();

			if (path == "TERRAIN")
			{
				TerrainGenerationParams params;
				params.Load(mesh.value()["terrain"], file);

				auto terrain = CreateTerrain(params);

				SetMesh(terrain);

				/*if (params.createInstance)
				{
					terrain->CreateInstance();
				}*/
			}
			else
			{
#if 0
				auto mesh = Mesh::CreateAsync(path,
					[&](std::shared_ptr<IResource> resource)
					{
						auto mesh = dynamic_pointer_cast<Mesh>(resource);

						if (!mesh)
						{
							LOG_CRIT("Could not load model '%s' from save file", path.c_str());
							return;
						}
						else
						{
							SetMesh(mesh);
							g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();
						}

						_serializationState = BaseComponent::SerializationState::Ready;
					});
#else

				auto mesh = Mesh::Create(path);

				if (!mesh)
				{
					LOG_CRIT("Could not load model '%s' from save file", path.c_str());
					return;
				}
				else
				{
					SetMesh(mesh);
				}
#endif
			}
			int32_t materialIndex = 0;

			for (auto& materials : mesh.value()["materials"].items())
			{
				auto path = materials.value();

#if 0

				auto material = Material::CreateAsync(path, 
					[&](std::shared_ptr<IResource> resource) {

						auto material = dynamic_pointer_cast<Material>(resource);

						if (!material)
						{
							LOG_CRIT("Failed to load material '%s' when deserialising MeshRenderer", ((std::string)path).c_str());
							return;
						}

						SetMaterial(material);

						//g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();

					});
#else
				auto material = Material::Create(path);

				if (!material)
				{
					LOG_CRIT("Failed to load material '%s' when deserialising MeshRenderer", ((std::string)path).c_str());
					return;
				}
				SetMaterial(material);

#endif

				++materialIndex;
			}
		}

		DESERIALIZE_VALUE(_uvScale);
		DESERIALIZE_VALUE(_shadowCullingMode);
		DESERIALIZE_VALUE(_offsetPosition);
	}

	const math::Vector2& StaticMeshComponent::GetUVScale() const
	{
		return _uvScale;
	}

	void StaticMeshComponent::DoubleClickMaterial(const std::wstring& path)
	{
		auto material = GetMaterial();

		if (!material)
			return;

		material->GetLoader()->CreateEditorDialog({ material->GetFileSystemPath() });
	}

	bool StaticMeshComponent::CreateWidget(ComponentWidget* widget)
	{
		LineEdit* meshLine = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Mesh");

		//meshLine->SetLabelMinSize(130);
		meshLine->SetValue(std::wstring(_mesh->GetName().begin(), _mesh->GetName().end()));

		if (auto material = GetMaterial(); material != nullptr)
		{
			LineEdit* matName = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Material");
			matName->SetValue(std::wstring(material->GetName().begin(), material->GetName().end()));
			matName->SetOnDragAndDropFn(std::bind(&StaticMeshComponent::SetMaterialFromWidget, this, 0, std::placeholders::_2));
			matName->SetOnDoubleClickFn(std::bind(&StaticMeshComponent::DoubleClickMaterial, this, std::placeholders::_2));
		}

		Vector2Edit* uvScale = new Vector2Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"UV Scale", &_uvScale, nullptr);
		Vector3Edit* offetPos = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Offset Position", &_offsetPosition);


		return true;
	}

	CullingMode StaticMeshComponent::GetShadowCullMode() const
	{
		return _shadowCullingMode;
	}

	void StaticMeshComponent::SetShadowCullMode(CullingMode mode)
	{
		_shadowCullingMode = mode;
	}

	void StaticMeshComponent::SetTextureFromWidget(Mesh* mesh, LineEdit* edit, MaterialTexture type, const fs::path& path)
	{
		auto tex = ITexture2D::Create(path);

		if (tex)
		{
			GetMaterial()->SetTexture(type, tex);
			edit->SetValue(path.filename().wstring());

			if (type == MaterialTexture::Opacity)
			{
				GetMaterial()->_properties.hasTransparency = 1;
			}
		}
	}

	void StaticMeshComponent::SetMaterialFromWidget(int32_t index, const fs::path& path)
	{
		auto mat = Material::Create(path);

		if (mat)
		{
			SetMaterial(mat);
		}
	}

	std::shared_ptr<Material> StaticMeshComponent::GetMaterial() const
	{
		std::unique_lock lock(_lock);

		return _material;
	}

	std::shared_ptr<Material> StaticMeshComponent::CreateMaterial()
	{
		std::unique_lock lock(_lock);

		std::shared_ptr<Material> material = std::shared_ptr<Material>(new Material);

		// set the correct resource loader so it can be unloaded
		material->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));

		_material = material;

		g_pEnv->_materialLoader->AddMaterial(material);

		return material;
	}

	void StaticMeshComponent::SetMaterial(const std::shared_ptr<Material>& material)
	{
		std::unique_lock lock(_lock);

		if (_material != material)
		{
			material->Lock();

			_material = material;

			material->Unlock();
		}
	}

	void StaticMeshComponent::DestroyMaterial()
	{
		std::unique_lock lock(_lock);

		_material.reset();
	}

	void StaticMeshComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		g_pEnv->_debugRenderer->DrawAABB(GetEntity()->GetWorldAABB(), math::Color(HEX_RGBA_TO_FLOAT4(255, 127, 40, 255)));
	}
}