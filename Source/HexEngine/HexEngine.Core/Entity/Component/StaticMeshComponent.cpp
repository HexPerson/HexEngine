

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

		if ((flags & MeshRenderFlags::MeshRenderShadowMap) != 0)
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
		mesh->UpdateConstantBuffer(GetEntity(), GetEntity()->GetLocalTM(), material.get(), instanceId);

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

		mesh->SetBuffers(GetEntity()->GetLocalTM());
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
		_mesh.reset();
	}

	void StaticMeshComponent::SetMesh(const std::shared_ptr<Mesh>& mesh)
	{
		_mesh = mesh;

		dx::BoundingBox bbox = GetEntity()->GetAABB();
		dx::BoundingBox::CreateMerged(bbox, bbox, mesh->GetAABB());

		GetEntity()->SetAABB(bbox);

		dx::BoundingOrientedBox obb = GetEntity()->GetOBB();
		dx::BoundingOrientedBox::CreateFromBoundingBox(obb, bbox);

		GetEntity()->SetOBB(obb);

		if (mesh->GetMaterial())
			SetMaterial(mesh->GetMaterial());

		// Set a default material
		if (GetMaterial() == nullptr)
		{ 
			SetMaterial(Material::Create("EngineData.Materials/Default.hmat"));
		}

		//mesh->CreateBuffers();
		mesh->CreateInstance();
	}

	/*void MeshRenderer::RemoveMesh()
	{
		_mesh->_meshRenderer = nullptr;
		_mesh = nullptr;
	}*/

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

		data["uvScale"] = { _uvScale.x, _uvScale.y };
		data["shadowCullingMode"] = _shadowCullingMode;
	}

	void StaticMeshComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
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
#if 1
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
				

				//LOG_DEBUG("mesh %s loaded a shader of %s", _mesh->GetRelativePath().string().c_str(), material->GetStandardShader()->GetAbsolutePath().string().c_str());

				++materialIndex;
			}
		}

		if (data.find("uvScale") != data.end())
		{
			_uvScale.x = data["uvScale"][0];
			_uvScale.y = data["uvScale"][1];
		}

		if (data.find("shadowCullingMode") != data.end())
		{
			_shadowCullingMode = data["shadowCullingMode"];
		}
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
		int32_t i = 0;
		auto mesh = _mesh;
		{
			std::wstring label = L"Mesh " + std::to_wstring(i);
			LineEdit* meshLine = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label);

			//meshLine->SetLabelMinSize(130);
			meshLine->SetValue(std::wstring(mesh->GetName().begin(), mesh->GetName().end()));

			if (auto material = GetMaterial(); material != nullptr)
			{
#if 1
				LineEdit* matName = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Material");
				matName->SetValue(std::wstring(material->GetName().begin(), material->GetName().end()));
				matName->SetOnDragAndDropFn(std::bind(&StaticMeshComponent::SetMaterialFromWidget, this, 0, std::placeholders::_2));
				matName->SetOnDoubleClickFn(std::bind(&StaticMeshComponent::DoubleClickMaterial, this, std::placeholders::_2));
#else
				// Albedo
				{
					auto albedoTex = material->GetTexture(MaterialTexture::Diffuse);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Albedo");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(albedoTex ? albedoTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Diffuse, std::placeholders::_2));
				}

				// Normal
				{
					auto normalTex = material->GetTexture(MaterialTexture::Normal);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Normal");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(normalTex ? normalTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Normal, std::placeholders::_2));
				}

				// Specular
				{
					auto specularTex = material->GetTexture(MaterialTexture::Specular);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Specular");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(specularTex ? specularTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Specular, std::placeholders::_2));
				}

				// Noise
				{
					auto noiseTex = material->GetTexture(MaterialTexture::Noise);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Noise");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(noiseTex ? noiseTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Noise, std::placeholders::_2));
				}

				// Height
				{
					auto heightTex = material->GetTexture(MaterialTexture::Height);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Height");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(heightTex ? heightTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Height, std::placeholders::_2));
				}

				// Emission
				{
					auto emissionTex = material->GetTexture(MaterialTexture::Emission);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emission");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(emissionTex ? emissionTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Emission, std::placeholders::_2));
				}

				// Opacity
				{
					auto opacityTex = material->GetTexture(MaterialTexture::Opacity);

					LineEdit* matEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Opacity");
					matEdit->SetLabelMinSize(150);
					matEdit->SetValue(opacityTex ? opacityTex->GetPath().filename() : L"None");
					matEdit->SetOnDragAndDropFn(std::bind(&MeshRenderer::SetTextureFromWidget, this, mesh, matEdit, MaterialTexture::Opacity, std::placeholders::_2));
				}

				// emission strength
				DragFloat* emissionR = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emission R", &material->_properties.emissiveColour.x, 0.0f, 1.0f, 0.1f);
				emissionR->SetLabelMinSize(160);
				DragFloat* emissionG = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emission G", &material->_properties.emissiveColour.y, 0.0f, 1.0f, 0.1f);
				emissionG->SetLabelMinSize(160);
				DragFloat* emissionB = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emission B", &material->_properties.emissiveColour.z, 0.0f, 1.0f, 0.1f);
				emissionB->SetLabelMinSize(160);
				DragFloat* emissionStrength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emission", &material->_properties.emissiveColour.w, 0.0f, 4.0f, 0.1f);
				emissionStrength->SetLabelMinSize(160);

				// Diffuse
				DragFloat* diffuseR = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Diffuse R", &material->_properties.diffuseColour.x, 0.0f, 1.0f, 0.1f);
				diffuseR->SetLabelMinSize(160);
				DragFloat* diffuseG = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Diffuse G", &material->_properties.diffuseColour.y, 0.0f, 1.0f, 0.1f);
				diffuseG->SetLabelMinSize(160);
				DragFloat* diffuseB = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Diffuse B", &material->_properties.diffuseColour.z, 0.0f, 1.0f, 0.1f);
				diffuseB->SetLabelMinSize(160);

				DragFloat* ShininessStrength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Shiny Strength", &material->_properties.shininessStrength, 0.0f, 1.0f, 0.01f);
				ShininessStrength->SetLabelMinSize(160);

				DragFloat* Shininess = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Exponent", &material->_properties.shininess, 0.0f, 1000.0f, 0.1f);
				Shininess->SetLabelMinSize(160);

				DragFloat* Smoothness = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Smoothness", &material->_properties.smoothness, 0.0f, 1.0f, 0.01f);
				Smoothness->SetLabelMinSize(160);

				DragFloat* SpecularProbability = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Specular Probability", &material->_properties.specularProbability, 0.0f, 1.0f, 0.01f);
				SpecularProbability->SetLabelMinSize(160);
#endif
			}

			Vector2Edit* uvScale = new Vector2Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"UV Scale", &_uvScale, nullptr);

			++i;
		}

		DropDown* shadowCull = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Depth State");
		shadowCull->GetContextMenu()->AddItem(new ContextItem(L"No culling", std::bind(&StaticMeshComponent::SetShadowCullMode, this, CullingMode::NoCulling)));
		shadowCull->GetContextMenu()->AddItem(new ContextItem(L"Back face", std::bind(&StaticMeshComponent::SetShadowCullMode, this, CullingMode::BackFace)));
		shadowCull->GetContextMenu()->AddItem(new ContextItem(L"Front face", std::bind(&StaticMeshComponent::SetShadowCullMode, this, CullingMode::FrontFace)));

		//bodyType->GetContextMenu()->AddItem({ L"Static", std::bind(&RigidBody::SetBodyTypeFromWidget, this, IRigidBody::BodyType::Static, bodyType) });

		/*DropDown* DepthState = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Depth State");
		DepthState->GetContextMenu()->AddItem({ L"No depth", std::bind(&MeshRenderer::SetDepthState, this, DepthBufferState::DepthNone) });
		DepthState->GetContextMenu()->AddItem({ L"Default", std::bind(&MeshRenderer::SetDepthState, this, DepthBufferState::DepthDefault) });
		DepthState->GetContextMenu()->AddItem({ L"Read", std::bind(&MeshRenderer::SetDepthState, this, DepthBufferState::DepthRead) });
		DepthState->GetContextMenu()->AddItem({ L"Reverse Z", std::bind(&MeshRenderer::SetDepthState, this, DepthBufferState::DepthReverseZ) });
		DepthState->GetContextMenu()->AddItem({ L"Read Reverse Z", std::bind(&MeshRenderer::SetDepthState, this, DepthBufferState::DepthReadReverseZ) });
		DepthState->GetContextMenu()->Disable();

		DropDown* CullMode = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Culling Mode");
		CullMode->GetContextMenu()->AddItem({ L"No culling", std::bind(&MeshRenderer::SetCullMode, this, CullingMode::NoCulling) });
		CullMode->GetContextMenu()->AddItem({ L"Back face", std::bind(&MeshRenderer::SetCullMode, this, CullingMode::BackFace) });
		CullMode->GetContextMenu()->AddItem({ L"Front face", std::bind(&MeshRenderer::SetCullMode, this, CullingMode::FrontFace) });
		CullMode->GetContextMenu()->Disable();*/

		//LineEdit* shader = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Material");
		//shader->SetLabelMinSize(130);
		//shader->SetValue(mesh->GetMaterial()->GetPath().filename().wstring());

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
		material->SetLoader(g_pEnv->_resourceSystem->FindResourceLoaderForExtension(".hmat"));

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
}