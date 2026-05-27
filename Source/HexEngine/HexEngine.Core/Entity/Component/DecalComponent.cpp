

#include "DecalComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include "../../GUI/Elements/DragFloat.hpp"

namespace HexEngine
{
	DecalComponent::DecalComponent(Entity* entity) :
		BaseComponent(entity)
	{
	}

	DecalComponent::DecalComponent(Entity* entity, DecalComponent* copy) :
		BaseComponent(entity)
	{
		if (copy == nullptr)
			return;

		_albedoTexture     = copy->_albedoTexture;
		_normalTexture     = copy->_normalTexture;
		_matTexture        = copy->_matTexture;
		_albedoPath        = copy->_albedoPath;
		_normalPath        = copy->_normalPath;
		_matPath           = copy->_matPath;
		_opacity           = copy->_opacity;
		_normalCutoff      = copy->_normalCutoff;
		_albedoWeight      = copy->_albedoWeight;
		_normalWeight      = copy->_normalWeight;
		_matWeight         = copy->_matWeight;
		_roughnessOverride = copy->_roughnessOverride;
		_metallicOverride  = copy->_metallicOverride;
	}

	void DecalComponent::SetAlbedoTexture(const std::shared_ptr<ITexture2D>& tex)
	{
		_albedoTexture = tex;
		_albedoPath = tex ? tex->GetFileSystemPath() : fs::path();
	}

	void DecalComponent::SetNormalTexture(const std::shared_ptr<ITexture2D>& tex)
	{
		_normalTexture = tex;
		_normalPath = tex ? tex->GetFileSystemPath() : fs::path();
	}

	void DecalComponent::SetMatTexture(const std::shared_ptr<ITexture2D>& tex)
	{
		_matTexture = tex;
		_matPath = tex ? tex->GetFileSystemPath() : fs::path();
	}

	bool DecalComponent::IsRenderable() const
	{
		// A decal with no textures and default mat overrides still produces a
		// roughness lift via _roughnessOverride - that IS the puddle use case. Only
		// skip when the entity has been disabled or opacity drops to zero.
		if (_opacity <= 0.0f)
			return false;
		return true;
	}

	void DecalComponent::Serialize(json& data, JsonFile* file)
	{
		// Texture paths are stored as strings; the renderer just needs a valid
		// IResource handle on the other side. ITexture2D::Create handles loading.
		std::string albedoStr = _albedoPath.string();
		std::string normalStr = _normalPath.string();
		std::string matStr    = _matPath.string();
		file->Serialize(data, "_albedoPath", albedoStr);
		file->Serialize(data, "_normalPath", normalStr);
		file->Serialize(data, "_matPath",    matStr);

		SERIALIZE_VALUE(_opacity);
		SERIALIZE_VALUE(_normalCutoff);
		SERIALIZE_VALUE(_albedoWeight);
		SERIALIZE_VALUE(_normalWeight);
		SERIALIZE_VALUE(_matWeight);
		SERIALIZE_VALUE(_roughnessOverride);
		SERIALIZE_VALUE(_metallicOverride);
	}

	void DecalComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;

		std::string albedoStr, normalStr, matStr;
		file->Deserialize(data, "_albedoPath", albedoStr);
		file->Deserialize(data, "_normalPath", normalStr);
		file->Deserialize(data, "_matPath",    matStr);

		// Resolve each path through the resource system. Missing files leave the
		// slot null - the shader treats null channels as "skip this channel".
		if (!albedoStr.empty())
		{
			_albedoPath = albedoStr;
			if (auto tex = ITexture2D::Create(_albedoPath); tex)
				_albedoTexture = tex;
		}
		if (!normalStr.empty())
		{
			_normalPath = normalStr;
			if (auto tex = ITexture2D::Create(_normalPath); tex)
				_normalTexture = tex;
		}
		if (!matStr.empty())
		{
			_matPath = matStr;
			if (auto tex = ITexture2D::Create(_matPath); tex)
				_matTexture = tex;
		}

		DESERIALIZE_VALUE(_opacity);
		DESERIALIZE_VALUE(_normalCutoff);
		DESERIALIZE_VALUE(_albedoWeight);
		DESERIALIZE_VALUE(_normalWeight);
		DESERIALIZE_VALUE(_matWeight);
		DESERIALIZE_VALUE(_roughnessOverride);
		DESERIALIZE_VALUE(_metallicOverride);

		_serializationState = BaseComponent::SerializationState::Ready;
	}

	bool DecalComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		// Texture pickers. Same AssetSearch widget pattern StaticMeshComponent uses
		// for its mesh / material slots - drops textures by drag-and-drop or via the
		// asset-browser dropdown.
		const auto wireTextureSlot = [&](const wchar_t* label, std::shared_ptr<ITexture2D>* slot, fs::path* pathSlot)
		{
			auto* search = new AssetSearch(
				widget,
				widget->GetNextPos(),
				Point(fullWidth, 84),
				label,
				{ ResourceType::Image },
				[slot, pathSlot](AssetSearch*, const AssetSearchResult& result)
				{
					const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					if (chosen.empty())
					{
						*slot = nullptr;
						*pathSlot = fs::path();
						return;
					}
					if (auto tex = ITexture2D::Create(chosen); tex)
					{
						*slot = tex;
						*pathSlot = chosen;
					}
				});
			if (*slot && !(*slot)->GetFileSystemPath().empty())
				search->SetValue((*slot)->GetFileSystemPath().wstring());
		};

		wireTextureSlot(L"Albedo",   &_albedoTexture, &_albedoPath);
		wireTextureSlot(L"Normal",   &_normalTexture, &_normalPath);
		wireTextureSlot(L"Mat (RM)", &_matTexture,    &_matPath);

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Opacity",            &_opacity,           0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Normal Cutoff",      &_normalCutoff,      0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Albedo Weight",      &_albedoWeight,      0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Normal Weight",      &_normalWeight,      0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Mat Weight",         &_matWeight,         0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Roughness Override", &_roughnessOverride, 0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Metallic Override",  &_metallicOverride,  0.0f, 1.0f, 0.01f);

		return true;
	}
}
