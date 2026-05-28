
#include "MaterialDialog.hpp"
#include "Button.hpp"
#include "MessageBox.hpp"
#include "DropDown.hpp"
#include "Checkbox.hpp"
#include "ColourPicker.hpp"
#include "AssetSearch.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Scene/SceneManager.hpp"

namespace HexEngine
{
	MaterialDialog::MaterialDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, std::shared_ptr<Material> material) :
		Dialog(parent, position, size, title),
		_material(material)
	{
		_rootView = new ScrollView(this, Point(10, 10), Point(GetSize().x - 20, GetSize().y - 20));

		_layout = new ComponentWidget(_rootView, Point(10, 10), Point(size.x - 20, size.y), L"Textures");

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

		auto metallicFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Metallic Factor", &material->_properties.metallicFactor, 0.0f, 1.0f, 0.01f, 2);
		auto roughnessFactor = new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18), L"Roughness Factor", &material->_properties.roughnessFactor, 0.0f, 1.0f, 0.01f, 2);
		_affectsGI = _material->GetAffectsGI();
		_affectsGiToggle = new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 40, 20), L"Affects GI", &_affectsGI);
		_emissiveAffectsGI = _material->GetEmissiveAffectsGI();
		_emissiveGiToggle = new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 40, 20), L"Emissive Affects GI", &_emissiveAffectsGI);

		auto format = new DropDown(_layout, _layout->GetNextPos(), Point(200, 18), L"Format");
		format->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Material::SetFormat, material.get(), MaterialFormat::None)));
		format->GetContextMenu()->AddItem(new ContextItem(L"ORM", std::bind(&Material::SetFormat, material.get(), MaterialFormat::ORM)));
		format->GetContextMenu()->AddItem(new ContextItem(L"RMA", std::bind(&Material::SetFormat, material.get(), MaterialFormat::RMA)));

		// Shading model selector. Standard = existing PBR (the only path until now).
		// Non-standard models route through the material-features GBuffer and
		// matching post-process passes (SubsurfaceScattering for SSS, etc).
		auto shadingModel = new DropDown(_layout, _layout->GetNextPos(), Point(200, 18), L"Shading Model");
		shadingModel->GetContextMenu()->AddItem(new ContextItem(L"Standard PBR",
			[material](const std::wstring&) { material->_properties.materialModel = 0; }));
		shadingModel->GetContextMenu()->AddItem(new ContextItem(L"Subsurface (SSS)",
			[material](const std::wstring&) { material->_properties.materialModel = 1; }));
		shadingModel->GetContextMenu()->AddItem(new ContextItem(L"Clearcoat",
			[material](const std::wstring&) { material->_properties.materialModel = 2; }));
		shadingModel->GetContextMenu()->AddItem(new ContextItem(L"Anisotropic",
			[material](const std::wstring&) { material->_properties.materialModel = 3; }));
		shadingModel->GetContextMenu()->AddItem(new ContextItem(L"Sheen / Cloth",
			[material](const std::wstring&) { material->_properties.materialModel = 4; }));

		// Per-model parameter sliders. Meaning depends on the active model - the
		// MaterialProperties.modelParams docstring lists the layout per model. We
		// expose all four as raw floats so the user can drive whichever the chosen
		// model interprets. (No need to gate on materialModel since modelParams is
		// ignored for Standard PBR.)
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18),
			L"Model Param X (SSS mask / clearcoat str / aniso / sheen str)",
			&material->_properties.modelParams.x, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18),
			L"Model Param Y (SSS tint R / clearcoat rough / tangent.x / sheen.r)",
			&material->_properties.modelParams.y, -1.0f, 1.0f, 0.01f, 3);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18),
			L"Model Param Z (SSS tint G / clearcoat IOR / tangent.y / sheen.g)",
			&material->_properties.modelParams.z, -1.0f, 1.0f, 0.01f, 3);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18),
			L"Model Param W (unused / clearcoat unused / unused / sheen.b)",
			&material->_properties.modelParams.w, -1.0f, 1.0f, 0.01f, 3);

		// Rain-drip intensity. Multiplied by g_weatherSurface.wetness in the
		// surface shader; 0 = ignore weather, 1 = full drip displacement when
		// it's raining hard. Authored per-material so glass / car bodies /
		// polished pavement opt in but brick walls and interiors stay dry.
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 40, 18),
			L"Rain Drip Intensity",
			&material->_properties.rainDripIntensity, 0.0f, 1.0f, 0.01f, 2);

		auto save = new Button(_layout, _layout->GetNextPos(), Point(80, 20), L"Save", std::bind(&MaterialDialog::Save, this));

		
	}

	bool MaterialDialog::Save()
	{
		_material->SetAffectsGI(_affectsGI);
		_material->SetEmissiveAffectsGI(_emissiveAffectsGI);
		if (auto scene = g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr; scene != nullptr)
		{
			scene->NotifyGiMaterialStateChanged();
		}
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
		case MaterialTexture::Albedo:
		{
			ColourPicker* diffuseClr = new ColourPicker(parent, Point(0, 25), Point(250, 20), L"Tint", (math::Color*)&_material->_properties.diffuseColour.x);
			diffuseClr->SetLabelMinSize(120);
			break;
		}

		case MaterialTexture::Emission:
		{
			ColourPicker* emissiveClr = new ColourPicker(parent, Point(0, 25), Point(250, 20), L"Emission", (math::Color*)&_material->_properties.emissiveColour.x);
			emissiveClr->SetLabelMinSize(120);
			break;
		}
		}
	}

	
}
