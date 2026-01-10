
#include "MaterialDialog.hpp"
#include "Button.hpp"
#include "MessageBox.hpp"
#include "DropDown.hpp"
#include "../../FileSystem/FileSystem.hpp"

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

			_textures[i] = new TextureSearch(
				_layout,
				pos,
				Point(size.x - 40, 100),
				Material::GetMaterialTextureName((MaterialTexture)i),
				material->GetTexture((MaterialTexture)i),
				material,
				(MaterialTexture)i);	

			AdditionalControls(_textures[i], (MaterialTexture)i, pos);
		}

		_smoothness = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Smoothness", &material->_properties.smoothness, 0.0f, 1.0f, 0.01f, 2);
		_specularProbability = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Specular Probability", &material->_properties.specularProbability, 0.0f, 1.0f, 0.01f, 2);

		auto metallicFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Metallic Factor", &material->_properties.metallicFactor, 0.0f, 1.0f, 0.01f, 2);
		auto roughnessFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Roughness Factor", &material->_properties.roughnessFactor, 0.0f, 1.0f, 0.01f, 2);

		auto save = new Button(_layout, _layout->GetNextPos(), Point(80, 20), L"Save", std::bind(&MaterialDialog::Save, this));

		auto format = new DropDown(_layout, _layout->GetNextPos(), Point(200, 18), L"Format");
		format->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Material::SetFormat, material.get(), MaterialFormat::None)));
		format->GetContextMenu()->AddItem(new ContextItem(L"ORM", std::bind(&Material::SetFormat, material.get(), MaterialFormat::ORM)));
	}

	bool MaterialDialog::Save()
	{
		_material->Save();
		DeleteMe();
		return true;
	}

	void MaterialDialog::AdditionalControls(Element* parent, MaterialTexture type, const Point& pos)
	{
		switch (type)
		{
		case MaterialTexture::Emission:
			DragFloat* emissionR = new DragFloat(parent, Point(100, 25), Point(200, 18), L"R", &_material->_properties.emissiveColour.x, 0.0f, 1.0f, 0.1f);
			emissionR->SetLabelMinSize(40);
			DragFloat* emissionG = new DragFloat(parent, Point(100, 45), Point(200, 18), L"G", &_material->_properties.emissiveColour.y, 0.0f, 1.0f, 0.1f);
			emissionG->SetLabelMinSize(40);
			DragFloat* emissionB = new DragFloat(parent, Point(100, 65), Point(200, 18), L"B", &_material->_properties.emissiveColour.z, 0.0f, 1.0f, 0.1f);
			emissionB->SetLabelMinSize(40);
			DragFloat* emissionStrength = new DragFloat(parent, Point(100, 85), Point(200, 18), L"Strength", &_material->_properties.emissiveColour.w, 0.0f, 4.0f, 0.1f);
			emissionStrength->SetLabelMinSize(40);
			break;
		}
	}
}