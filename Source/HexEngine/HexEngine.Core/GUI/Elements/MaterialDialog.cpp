
#include "MaterialDialog.hpp"
#include "Button.hpp"
#include "MessageBox.hpp"
#include "DropDown.hpp"
#include "ColourPicker.hpp"
#include "AssetSearch.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../Environment/LogFile.hpp"

namespace HexEngine
{
	MaterialDialog::MaterialDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, std::shared_ptr<Material> material) :
		Dialog(parent, position, size, title),
		_material(material)
	{
		_layout = new ComponentWidget(this, Point(10, 10), Point(size.x - 20, size.y), L"Textures");

		for (auto i = 0; i < MaterialTexture::Count; ++i)
		{
			const auto pos = _layout->GetNextPos();

			_textures[i] = new AssetSearch(
				_layout,
				pos,
				Point(size.x - 40, 100),
				Material::GetMaterialTextureName((MaterialTexture)i),
				{ ResourceType::Image },
				[this, type = (MaterialTexture)i](AssetSearch* search, const AssetSearchResult& result)
				{
					(void)search;
					ApplyTextureFromSearch(type, result);
				});

			if (auto tex = _material->GetTexture((MaterialTexture)i); tex != nullptr)
				_textures[i]->SetValue(tex->GetFileSystemPath().wstring());

			AdditionalControls(_textures[i], (MaterialTexture)i, pos);
		}

		_smoothness = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Smoothness", &material->_properties.smoothness, 0.0f, 1.0f, 0.01f, 2);
		_specularProbability = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Specular Probability", &material->_properties.specularProbability, 0.0f, 1.0f, 0.01f, 2);

		auto metallicFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Metallic Factor", &material->_properties.metallicFactor, 0.0f, 1.0f, 0.01f, 2);
		auto roughnessFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Roughness Factor", &material->_properties.roughnessFactor, 0.0f, 1.0f, 0.01f, 2);

		auto format = new DropDown(_layout, _layout->GetNextPos(), Point(200, 18), L"Format");
		format->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Material::SetFormat, material.get(), MaterialFormat::None)));
		format->GetContextMenu()->AddItem(new ContextItem(L"ORM", std::bind(&Material::SetFormat, material.get(), MaterialFormat::ORM)));
		format->GetContextMenu()->AddItem(new ContextItem(L"RMA", std::bind(&Material::SetFormat, material.get(), MaterialFormat::RMA)));

		auto save = new Button(_layout, _layout->GetNextPos(), Point(80, 20), L"Save", std::bind(&MaterialDialog::Save, this));

		
	}

	bool MaterialDialog::Save()
	{
		_material->Save();
		DeleteMe();
		return true;
	}

	void MaterialDialog::ApplyTextureFromSearch(MaterialTexture type, const AssetSearchResult& result)
	{
		fs::path pathToLoad = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
		if (pathToLoad.empty())
			return;

		auto texture = ITexture2D::Create(pathToLoad);
		if (texture == nullptr && !result.absolutePath.empty() && pathToLoad != result.absolutePath)
		{
			texture = ITexture2D::Create(result.absolutePath);
		}

		if (texture == nullptr)
		{
			LOG_WARN("MaterialDialog: failed to load texture for '%S' from '%s'",
				Material::GetMaterialTextureName(type).c_str(),
				pathToLoad.string().c_str());
			return;
		}

		_material->SetTexture(type, texture);

		if (_textures[type] != nullptr)
		{
			const fs::path displayPath = !texture->GetFileSystemPath().empty()
				? texture->GetFileSystemPath()
				: texture->GetAbsolutePath();
			_textures[type]->SetValue(displayPath.wstring());
		}
	}

	void MaterialDialog::AdditionalControls(Element* parent, MaterialTexture type, const Point& pos)
	{
		switch (type)
		{
		case MaterialTexture::Emission:
			ColourPicker* emissiveClr = new ColourPicker(parent, Point(100, 25), Point(200, 18), L"Emission", (math::Color*) & _material->_properties.emissiveColour.x);
			emissiveClr->SetLabelMinSize(40);
			break;
		}
	}

	
}
