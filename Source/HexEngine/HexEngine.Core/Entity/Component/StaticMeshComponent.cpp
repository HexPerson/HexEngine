

#include "StaticMeshComponent.hpp"
#include "SkeletalAnimationComponent.hpp"
#include "../Entity.hpp"
#include "../../Scene/Scene.hpp"
#include "../../HexEngine.hpp"
#include "../../Terrain/TerrainGenerator.hpp"
#include "../../Graphics/MaterialLoader.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

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
		_includeInGIWhenHidden = clone->_includeInGIWhenHidden;
		_excludeFromGI = clone->_excludeFromGI;
		// Carry the bound bone name across the clone so prefab-spawned
		// instances can re-resolve against the spawned entity's skeletal
		// component. _boundBone itself stays null - it's a runtime cache
		// resolved lazily via TryResolveBoundBone().
		_boundBoneName = clone->_boundBoneName;
		_offsetPosition = clone->_offsetPosition;

		SetMesh(clone->GetMesh());
		SetMaterial(clone->GetMaterial());
	}

	StaticMeshComponent::~StaticMeshComponent()
	{
		if(_mesh)
			LOG_DEBUG("Mesh '%s' now has a ref count of %d (will be %d after this)", _mesh->GetName().c_str(), _mesh.use_count(), _mesh.use_count()-1);
	}

	bool StaticMeshComponent::RenderMesh(Mesh* mesh, MeshRenderFlags flags, int32_t instanceId, Material* cachedMaterial)
	{
		// make sure this MeshRenderer can actually render this Mesh
		if (mesh != _mesh.get())
			return false;

		auto material = cachedMaterial ? cachedMaterial : GetMaterial().get();

		if (!material)
		{
			LOG_WARN("Cannot render a mesh without a valid material!");
			return false;
		}

		auto shader = material->GetStandardShader();

		bool isShadowMap = (flags & MeshRenderFlags::MeshRenderShadowMap) != 0;

		if (isShadowMap)
		{
			auto shadowShader = material->GetShadowMapShader();
			if (!shadowShader)
				shadowShader = IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs");
			if (shadowShader)
				shader = shadowShader;
		}

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

		// Lazy-resolve the bound bone if a name was deserialized but the
		// SkeletalAnimationComponent wasn't ready at deserialize time.
		// No-op when _boundBone is already cached or _boundBoneName is
		// empty.
		if (_boundBone == nullptr && !_boundBoneName.empty())
			TryResolveBoundBone();

		if (_boundBone)
		{
			_offsetMatrix = math::Matrix::CreateFromQuaternion(_boundBone->Rotation) * math::Matrix::CreateTranslation(_boundBone->Position);//_boundBone->FinalTransformation.Invert().Transpose();//* math::Matrix::CreateTranslation(_boundBone->Position);
		}
		//else
		//{
		//	offsetMatrix = math::Matrix::CreateTranslation(_offsetPosition);
		//}

		mesh->UpdateConstantBuffer(GetEntity(), /*GetEntity()->GetLocalTM() **/ offsetMatrix, material, instanceId, (flags & MeshRenderFlags::MeshRenderTransparency) != 0);

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
		graphicsDevice->SetTexture2D(slotIdx + 0, material->GetTexture(MaterialTexture::Albedo).get());

		graphicsDevice->SetTexture2D(slotIdx + 1, material->GetTexture(MaterialTexture::Normal).get());

		graphicsDevice->SetTexture2D(slotIdx + 2, material->GetTexture(MaterialTexture::Roughness).get());

		graphicsDevice->SetTexture2D(slotIdx + 3, material->GetTexture(MaterialTexture::Metallic).get());

		graphicsDevice->SetTexture2D(slotIdx + 4, material->GetTexture(MaterialTexture::Height).get());

		graphicsDevice->SetTexture2D(slotIdx + 5, material->GetTexture(MaterialTexture::Emission).get());

		graphicsDevice->SetTexture2D(slotIdx + 6, material->GetTexture(MaterialTexture::Opacity).get());

		graphicsDevice->SetTexture2D(slotIdx + 7, material->GetTexture(MaterialTexture::AmbientOcclusion).get());

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
		if (!mesh)
		{
			_mesh.reset();
			if (auto* scene = (!_excludeFromGI && GetEntity()) ? GetEntity()->GetScene() : nullptr; scene != nullptr)
			{
				scene->NotifyStaticMeshChanged(this, true, false);
			}
			return;
		}

		_mesh = mesh;

		dx::BoundingBox bbox = mesh->GetAABB();

		GetEntity()->SetAABB(bbox);
		GetEntity()->SetOBB(mesh->GetOBB());

		GetEntity()->RecalculateBoundingVolumes(bbox);

		if (mesh->GetMaterial())
			SetMaterial(mesh->GetMaterial());

		// Set a default material
		if (GetMaterial() == nullptr)
		{ 
			SetMaterial(Material::Create("EngineData.Materials/Default.hmat"));
		}

		mesh->CreateInstance();
		if (auto* scene = (!_excludeFromGI && GetEntity()) ? GetEntity()->GetScene() : nullptr; scene != nullptr)
		{
			scene->NotifyStaticMeshChanged(this, true, false);
		}
	}

	std::shared_ptr<Mesh> StaticMeshComponent::GetMesh() const
	{
		return _mesh;
	}

	void StaticMeshComponent::SetBoundBoneName(const std::string& name)
	{
		_boundBoneName = name;
		// Invalidate the cached pointer - the next render tick will
		// re-resolve. Clearing here (rather than resolving immediately)
		// keeps SetBoundBoneName usable from any thread/context without
		// needing the skeletal component to already be wired up.
		_boundBone = nullptr;

		if (!_boundBoneName.empty())
			TryResolveBoundBone();
	}

	bool StaticMeshComponent::IsBoundToBone()
	{
		// PVS / Scene call this every frame to decide whether to apply
		// the bone offset to the entity's world matrix. Run the lazy
		// resolve here so deserialized / prefab-cloned components that
		// only have _boundBoneName (no live BoneInfo*) get hooked up on
		// first use.
		if (_boundBone == nullptr && !_boundBoneName.empty())
			TryResolveBoundBone();
		return _boundBone != nullptr;
	}

	const math::Matrix& StaticMeshComponent::GetOffsetMatrix()
	{
		// Refresh from the current bone pose on every read. The
		// SkeletalAnimationComponent updates _boundBone->Position /
		// Rotation per animation tick; we can't cache here because
		// nothing else writes back to _offsetMatrix any more.
		if (_boundBone == nullptr && !_boundBoneName.empty())
			TryResolveBoundBone();

		if (_boundBone != nullptr)
		{
			_offsetMatrix =
				math::Matrix::CreateFromQuaternion(_boundBone->Rotation) *
				math::Matrix::CreateTranslation(_boundBone->Position + _offsetPosition);
			_offsetMatrixTranspose = _offsetMatrix.Transpose();
		}
		return _offsetMatrix;
	}

	const math::Matrix& StaticMeshComponent::GetOffsetMatrixTranspose()
	{
		// Cheap path: just delegate to GetOffsetMatrix() which keeps
		// _offsetMatrixTranspose in sync alongside _offsetMatrix.
		GetOffsetMatrix();
		return _offsetMatrixTranspose;
	}

	bool StaticMeshComponent::TryResolveBoundBone()
	{
		if (_boundBone != nullptr)
			return true;
		if (_boundBoneName.empty())
			return false;

		// Try the owning entity first (a character with its own body mesh
		// rigged to its own skeleton), then walk up parents (typical
		// attachment case: hat / weapon child whose StaticMeshComponent
		// binds to a bone on the parent character entity).
		Entity* e = GetEntity();
		while (e != nullptr)
		{
			if (auto* skel = e->GetComponent<SkeletalAnimationComponent>(); skel != nullptr)
			{
				// SkeletalAnimationComponent auto-binds its AnimationData
				// from the entity's StaticMeshComponent on first Update /
				// CreateWidget. Try once here in case render happens
				// before Update has had a chance.
				skel->TryAutoBindFromEntityMesh();
				if (auto* bi = skel->GetBoneInfoByName(_boundBoneName); bi != nullptr)
				{
					_boundBone = bi;
					return true;
				}
			}
			e = e->GetParent();
		}
		return false;
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
		SERIALIZE_VALUE(_includeInGIWhenHidden);
		SERIALIZE_VALUE(_excludeFromGI);
		SERIALIZE_VALUE(_shadowCullingMode);
		SERIALIZE_VALUE(_offsetPosition);
		SERIALIZE_VALUE(_boundBoneName);
	}

	void StaticMeshComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		_serializationState = BaseComponent::SerializationState::Deserializing;

		for(auto& mesh : data["mesh"].items())
		{
			auto path = mesh.key();

			if (path.empty())
			{
				LOG_WARN("Skipping StaticMeshComponent with empty path");
				continue;
			}

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
		DESERIALIZE_VALUE(_includeInGIWhenHidden);
		DESERIALIZE_VALUE(_excludeFromGI);
		DESERIALIZE_VALUE(_shadowCullingMode);
		DESERIALIZE_VALUE(_offsetPosition);
		DESERIALIZE_VALUE(_boundBoneName);
		// _boundBone (the cached BoneInfo*) stays null until the owning
		// entity (or a parent) has its SkeletalAnimationComponent and
		// AnimatedMesh resolved. TryResolveBoundBone() does the
		// lookup lazily - safest place is on the first render/update tick
		// after deserialize, called via the operator[] in the render path.
	}

	const math::Vector2& StaticMeshComponent::GetUVScale() const
	{
		return _uvScale;
	}

	void StaticMeshComponent::SetUVScale(const math::Vector2& uvScale)
	{
		_uvScale = uvScale;
		if (auto* scene = GetEntity() ? GetEntity()->GetScene() : nullptr; scene != nullptr)
		{
			scene->NotifyStaticMeshChanged(this, false, true);
		}
	}

	void StaticMeshComponent::SetIncludeInGIWhenHidden(bool value)
	{
		if (_includeInGIWhenHidden == value)
			return;

		_includeInGIWhenHidden = value;
		if (auto* scene = GetEntity() ? GetEntity()->GetScene() : nullptr; scene != nullptr)
		{
			scene->NotifyStaticMeshChanged(this, true, false);
		}
	}

	bool StaticMeshComponent::GetIncludeInGIWhenHidden() const
	{
		return _includeInGIWhenHidden;
	}

	void StaticMeshComponent::SetExcludeFromGI(bool value)
	{
		if (_excludeFromGI == value)
			return;

		_excludeFromGI = value;
		// Mark GI dirty so the clipmap re-voxelizes this region - toggling the
		// flag must add or remove this mesh's contribution immediately, same as
		// IncludeInGIWhenHidden above.
		if (auto* scene = GetEntity() ? GetEntity()->GetScene() : nullptr; scene != nullptr)
		{
			scene->NotifyStaticMeshChanged(this, true, false);
		}
	}

	bool StaticMeshComponent::GetExcludeFromGI() const
	{
		return _excludeFromGI;
	}

	const math::Vector3& StaticMeshComponent::GetOffsetPosition() const
	{
		return _offsetPosition;
	}

	void StaticMeshComponent::SetOffsetPosition(const math::Vector3& offsetPosition)
	{
		_offsetPosition = offsetPosition;
	}

	const MeshInstanceData& StaticMeshComponent::GetCachedInstanceData(Material* material)
	{
		auto entity = GetEntity();
		const auto transformVersion = entity->GetTransformVersion();
		const auto colour = material ? material->_properties.diffuseColour : math::Vector4(1.0f);

		if (_cachedRenderTransformVersion != transformVersion || _cachedRenderColour != colour || _cachedRenderUvScale != _uvScale)
		{
			_cachedInstanceData.worldMatrix = entity->GetWorldTMTranspose();
			_cachedInstanceData.worldMatrixPrev = entity->GetWorldTMPrevTranspose();
			_cachedInstanceData.worldMatrixInverseTranspose = entity->GetWorldTMInvert();
			_cachedInstanceData.colour = colour;
			_cachedInstanceData.uvscale = _uvScale;

			_cachedRenderTransformVersion = transformVersion;
			_cachedRenderColour = colour;
			_cachedRenderUvScale = _uvScale;
		}
		else
		{
			// Cache hit: transform did not change this frame, so by definition there is no
			// motion between previous and current frame for this entity. Refresh
			// worldMatrixPrev to match the cached worldMatrix so velocity reads as zero.
			// Without this refresh the cached "prev" stays at whatever value was captured
			// the last time the entity moved (typically during scene load), so every
			// subsequent static frame emits the SAME one-frame motion delta forever - which
			// shows up as a persistent screen-space velocity gradient on every entity that
			// ever moved, even though the entity is now sitting still. The cache-miss path
			// above is unaffected: it correctly captures the real motion vector for whichever
			// frame the entity actually moves.
			_cachedInstanceData.worldMatrixPrev = _cachedInstanceData.worldMatrix;
		}

		return _cachedInstanceData;
	}

	const SimpleMeshInstanceData& StaticMeshComponent::GetCachedShadowInstanceData()
	{
		auto entity = GetEntity();
		const auto transformVersion = entity->GetTransformVersion();

		if (_cachedRenderTransformVersionShadow != transformVersion)
		{
			_cachedShadowInstanceData.worldMatrix = entity->GetWorldTMTranspose();
			_cachedRenderTransformVersionShadow = transformVersion;
		}

		return _cachedShadowInstanceData;
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
		AssetSearch* meshLine = new AssetSearch(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 84),
			L"Mesh",
			{ ResourceType::Mesh },
			std::bind(&StaticMeshComponent::SetMeshFromWidget, this, std::placeholders::_1, std::placeholders::_2));
		meshLine->SetPrefabOverrideBinding(GetComponentName(), "/mesh");

		if (_mesh)
		{
			if (!_mesh->GetFileSystemPath().empty())
				meshLine->SetValue(_mesh->GetFileSystemPath().wstring());
			else
				meshLine->SetValue(std::wstring(_mesh->GetName().begin(), _mesh->GetName().end()));
		}

		if (auto material = GetMaterial(); material != nullptr)
		{
			auto applyMaterialFromResult = [this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path pathToApply = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				if (!pathToApply.empty())
				{
					SetMaterialFromWidget(0, pathToApply);
				}
			};

			AssetSearch* matName = new AssetSearch(
				widget,
				widget->GetNextPos(),
				Point(widget->GetSize().x - 20, 84),
				L"Material",
				{ ResourceType::Material },
				applyMaterialFromResult);
			matName->SetPrefabOverrideBinding(GetComponentName(), "/mesh");
			if (!material->GetFileSystemPath().empty())
				matName->SetValue(material->GetFileSystemPath().wstring());
			else
				matName->SetValue(std::wstring(material->GetName().begin(), material->GetName().end()));
		}

		Vector2Edit* uvScale = new Vector2Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"UV Scale", &_uvScale, nullptr);
		Vector3Edit* offetPos = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Offset Position", &_offsetPosition);
		uvScale->SetPrefabOverrideBinding(GetComponentName(), "/_uvScale");
		offetPos->SetPrefabOverrideBinding(GetComponentName(), "/_offsetPosition");

		// Bound bone name. On commit we hand the new name to
		// SetBoundBoneName which walks self + parents for a
		// SkeletalAnimationComponent, looks the bone up by name, and
		// caches the BoneInfo* pointer used by the render path. Empty
		// string clears the binding. Validation is best-effort: if no
		// matching bone is found the name is still saved (so a later
		// attachment to a freshly-skinned parent will resolve on its own)
		// but we LOG_WARN so the user knows the look-up failed right now.
		LineEdit* boneEdit = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Bound Bone");
		boneEdit->SetValue(std::wstring(_boundBoneName.begin(), _boundBoneName.end()));
		boneEdit->SetDoesCallbackWaitForReturn(true);
		boneEdit->SetOnInputFn([this](LineEdit*, const std::wstring& text)
		{
			std::string narrow = ws2s(text);
			SetBoundBoneName(narrow);
			if (!narrow.empty() && _boundBone == nullptr)
			{
				LOG_WARN("StaticMeshComponent: no bone named '%s' found on this entity or its parent chain. The binding will retry each frame in case the skeletal component arrives later.", narrow.c_str());
			}
		});
		boneEdit->SetPrefabOverrideBinding(GetComponentName(), "/_boundBoneName");

		// Per-instance GI exclusion. The bool* binding toggles _excludeFromGI
		// directly; the OnCheck callback marks the GI clipmap dirty so the mesh
		// drops out of (or back into) the bounce immediately. We mark dirty
		// inline rather than calling SetExcludeFromGI here because the checkbox
		// has already flipped the bool, so SetExcludeFromGI's equality guard
		// would early-out before notifying.
		Checkbox* excludeGi = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Exclude From GI", &_excludeFromGI);
		excludeGi->SetOnCheckFn([this](Checkbox*, bool)
		{
			if (auto* scene = GetEntity() ? GetEntity()->GetScene() : nullptr; scene != nullptr)
				scene->NotifyStaticMeshChanged(this, true, false);
		});
		excludeGi->SetPrefabOverrideBinding(GetComponentName(), "/_excludeFromGI");

		return true;
	}

	void StaticMeshComponent::SetMeshFromWidget(AssetSearch* search, const AssetSearchResult& result)
	{
		(void)search;
		if (result.assetPath.empty())
			return;

		auto mesh = Mesh::Create(result.assetPath);
		if (!mesh)
		{
			LOG_WARN("Failed to set mesh from AssetSearch path '%s'", result.assetPath.string().c_str());
			return;
		}

		auto* entity = GetEntity();
		json beforeData = json::object();
		HexEngine::JsonFile serializer(fs::path(), std::ios::in);
		beforeData["name"] = GetComponentName();
		Serialize(beforeData, &serializer);

		SetMesh(mesh);
		if (auto* scene = entity != nullptr ? entity->GetScene() : nullptr; scene != nullptr)
		{
			scene->ForceRebuildPVS();
		}

		if (entity != nullptr && entity->IsPrefabInstance())
		{
			json afterData = json::object();
			afterData["name"] = GetComponentName();
			Serialize(afterData, &serializer);

			json diffOps = json::diff(beforeData, afterData);
			if (diffOps.is_array())
			{
				for (const auto& diffOp : diffOps)
				{
					if (!diffOp.is_object())
						continue;

					const auto op = diffOp.value("op", std::string());
					const auto patchPath = diffOp.value("path", std::string());
					if (op.empty() || patchPath.empty())
						continue;

					if (patchPath == "/name" || patchPath.rfind("/name/", 0) == 0)
						continue;

					Entity::PrefabOverridePatch patch;
					patch.componentName = GetComponentName();
					patch.path = patchPath;
					patch.op = op;
					const auto valueIt = diffOp.find("value");
					patch.value = valueIt != diffOp.end() ? *valueIt : json();
					entity->UpsertPrefabOverridePatch(patch);
				}
			}

			entity->ClearPrefabPropertyOverride("staticMesh.mesh");
		}
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
			auto* entity = GetEntity();
			json beforeData = json::object();
			HexEngine::JsonFile serializer(fs::path(), std::ios::in);
			beforeData["name"] = GetComponentName();
			Serialize(beforeData, &serializer);

			SetMaterial(mat);
			if (entity != nullptr)
			{
				if (auto* scene = entity->GetScene(); scene != nullptr)
				{
					scene->ForceRebuildPVS();
				}
			}

			if (entity != nullptr && entity->IsPrefabInstance())
			{
				json afterData = json::object();
				afterData["name"] = GetComponentName();
				Serialize(afterData, &serializer);

				json diffOps = json::diff(beforeData, afterData);
				if (diffOps.is_array())
				{
					for (const auto& diffOp : diffOps)
					{
						if (!diffOp.is_object())
							continue;

						const auto op = diffOp.value("op", std::string());
						const auto patchPath = diffOp.value("path", std::string());
						if (op.empty() || patchPath.empty())
							continue;

						if (patchPath == "/name" || patchPath.rfind("/name/", 0) == 0)
							continue;

						Entity::PrefabOverridePatch patch;
						patch.componentName = GetComponentName();
						patch.path = patchPath;
						patch.op = op;
						const auto valueIt = diffOp.find("value");
						patch.value = valueIt != diffOp.end() ? *valueIt : json();
						entity->UpsertPrefabOverridePatch(patch);
					}
				}

				entity->ClearPrefabPropertyOverride("staticMesh.material");
			}
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
			if (material)
			{
				material->Lock();
			}

			_material = material;

			if (material)
			{
				material->Unlock();
			}

			// A mesh excluded from GI contributes nothing to the voxel world, so
			// swapping its material must NOT bump the GI material revision -
			// otherwise per-frame render helpers (e.g. the shared light-volume
			// sphere, whose material is swapped between the point and spot passes
			// every frame) invalidate the GI triangle cache continuously.
			if (auto* scene = (!_excludeFromGI && GetEntity()) ? GetEntity()->GetScene() : nullptr; scene != nullptr)
			{
				scene->NotifyStaticMeshChanged(this, false, true);
			}
		}
	}

	void StaticMeshComponent::DestroyMaterial()
	{
		std::unique_lock lock(_lock);

		_material.reset();
		if (auto* scene = (!_excludeFromGI && GetEntity()) ? GetEntity()->GetScene() : nullptr; scene != nullptr)
		{
			scene->NotifyStaticMeshChanged(this, false, true);
		}
	}

	void StaticMeshComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		g_pEnv->_debugRenderer->DrawOBB(GetEntity()->GetWorldOBB(), math::Color(HEX_RGBA_TO_FLOAT4(255, 127, 40, 255)));
	}
}
