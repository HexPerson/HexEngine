
#include "MaterialLoader.hpp"
#include "Material.hpp"

#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../GUI/Elements/MaterialDialog.hpp"
#include "../GUI/UIManager.hpp"
#include "../GUI/Elements/MessageBox.hpp"

namespace HexEngine
{
	MaterialLoader::MaterialLoader()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	MaterialLoader::~MaterialLoader()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> MaterialLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		JsonFile file(absolutePath, std::ios::in);

		if (file.DoesExist() == false)
		{
			LOG_CRIT("Material file '%s' does not exist", absolutePath.u8string().c_str());
			return nullptr;
		}

		LOG_INFO("Loading material '%s'", absolutePath.filename().string().c_str());

		std::shared_ptr<Material> material = std::shared_ptr<Material>(new Material, ResourceDeleter());

		file.Open();

		std::string data;
		file.ReadAll(data);

		json matData = json::parse(data);		

		ParseJson(&file, matData, material);

		_loadedMaterials[absolutePath] = material;

		material->SetName(absolutePath.filename().string());

		return material;
	}

	std::shared_ptr<IResource> MaterialLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::string materialData = (const char*)data.data();

		KeyValues kv;
		if(kv.Parse(materialData) == false)
		{
			LOG_CRIT("Failed to parse key values from material file '%s'", relativePath.string().c_str());
			return nullptr;
		}

		std::shared_ptr<Material> material = std::shared_ptr<Material>(new Material, ResourceDeleter());
		
		json matData = json::parse(data);

		//ParseJson(&file, matData, material);

		_loadedMaterials[relativePath] = material;

		material->SetName(relativePath.filename().string());

		return material;
	}

	void MaterialLoader::OnResourceChanged(std::shared_ptr<IResource> resource)
	{
		auto it = _loadedMaterials.find(resource->GetAbsolutePath());

		if (it == _loadedMaterials.end())
			return;

		JsonFile file(resource->GetAbsolutePath(), std::ios::in);

		if (file.DoesExist() == false)
		{
			LOG_CRIT("Material file '%s' does not exist", resource->GetAbsolutePath().string().c_str());
			return;
		}

		std::shared_ptr<Material> materialToReload = dynamic_pointer_cast<Material>(resource);

		file.Open();

		std::string data;
		file.ReadAll(data);

		json matData = json::parse(data);

		ParseJson(&file, matData, materialToReload);
	}

	void MaterialLoader::ParseJson(JsonFile* file, json& json, std::shared_ptr<Material>& material)
	{
		
		// Load textures
		//
		auto textures = json["textures"];

		for (auto i = 0; i < MaterialTexture::Count; ++i)
		{
			auto texName = Material::GetMaterialTextureName((MaterialTexture)i);
			auto texNameA = std::string(texName.begin(), texName.end());

			if (auto tex = textures.find(texNameA); tex != textures.end())
				material->SetTexture((MaterialTexture)i, ITexture2D::Create(tex.value()));
		}

		// Load properties
		//
		auto properties = json["properties"];

		{
			MaterialProperties& props = material->_properties;

			file->Deserialize(properties, "metallicFactor", props.metallicFactor);
			file->Deserialize(properties, "roughnessFactor", props.roughnessFactor);
			file->Deserialize(properties, "diffuseColour", props.diffuseColour);
			file->Deserialize(properties, "emissiveColour", props.emissiveColour);
			file->Deserialize(properties, "hasTransparency", props.hasTransparency);
			file->Deserialize(properties, "isWater", props.isWater);
			file->Deserialize(properties, "specularProbability", props.specularProbability);
		}

		// Load properties
		//
		auto renderer = json["renderer"];
		{

			DepthBufferState depthState = DepthBufferState::DepthDefault;
			file->Deserialize(renderer, "depthState", depthState);

			/*if (values[0] == "DepthNone")
				state = DepthBufferState::DepthNone;
			else if (values[0] == "DepthDefault")
				state = DepthBufferState::DepthDefault;
			else if (values[0] == "DepthRead")
				state = DepthBufferState::DepthRead;
			else if (values[0] == "DepthReverseZ")
				state = DepthBufferState::DepthReverseZ;
			else if (values[0] == "DepthReadReverseZ")
				state = DepthBufferState::DepthReadReverseZ;*/

			material->SetDepthState(depthState);


			BlendState blendState = BlendState::Opaque;
			file->Deserialize(renderer, "blendState", blendState);

			/*if (values[0] == "Opaque")
				state = BlendState::Opaque;
			else if (values[0] == "Additive")
				state = BlendState::Additive;
			else if (values[0] == "Subtractive")
				state = BlendState::Subtractive;
			else if (values[0] == "Transparency")
				state = BlendState::Transparency;*/

			material->SetBlendState(blendState);


			CullingMode mode = CullingMode::BackFace;
			file->Deserialize(renderer, "cullingMode", mode);
			/*if (values[0] == "NoCulling")
				mode = CullingMode::NoCulling;
			else if (values[0] == "BackFace")
				mode = CullingMode::BackFace;
			else if (values[0] == "FrontFace")
				mode = CullingMode::FrontFace;*/

			material->SetCullMode(mode);

			MaterialFormat format = MaterialFormat::None;
			file->Deserialize(renderer, "format", format);
			/*if (values[0] == "NoCulling")
				mode = CullingMode::NoCulling;
			else if (values[0] == "BackFace")
				mode = CullingMode::BackFace;
			else if (values[0] == "FrontFace")
				mode = CullingMode::FrontFace;*/

			material->SetFormat(format);

			float cullDistance = 0.0f;
			file->Deserialize(renderer, "cullDistance", cullDistance);
			material->SetCullDistance(cullDistance);
		}

		// Load shaders
		//
		auto shaders = json["shaders"];
		{
			if (auto shader = shaders.find("standard"); shader != shaders.end() && !shader.value().is_null())
				material->SetStandardShader(IShader::Create(shader.value()));
			
			if (auto shader = shaders.find("shadowmap"); shader != shaders.end() && !shader.value().is_null())
				material->SetShadowMapShader(IShader::Create(shader.value()));
		}

		// Load sounds
		//
		/*if (auto properties = kvMap.find("Sounds"); properties != kvMap.end())
		{
			std::stringstream ss;
			ss << properties->second.value;

			std::string textureLine;

			while (std::getline(ss, textureLine))
			{
				if (textureLine.length() == 0)
					continue;

				while (textureLine.at(0) == '\t')
					textureLine.erase(textureLine.begin());

				std::string variable;
				std::vector<std::string> values;
				ParseValue(textureLine, variable, values);

				material->AddSoundTag(variable, values[0]);
			}
		}*/
	}

	void MaterialLoader::UnloadResource(IResource* resource)
	{		
		_loadedMaterials.erase(resource->GetAbsolutePath());

		SAFE_DELETE(resource);
	}

	std::vector<std::string> MaterialLoader::GetSupportedResourceExtensions()
	{
		return { ".hmat" };
	}

	std::wstring MaterialLoader::GetResourceDirectory() const
	{
		return L"Materials";
	}

	void MaterialLoader::AddMaterial(const std::shared_ptr<Material>& material)
	{
		_loadedMaterials[material->GetAbsolutePath()] = material;
	}

	void MaterialLoader::RemoveMaterial(const std::shared_ptr<Material> material)
	{
		_loadedMaterials.erase(material->GetAbsolutePath());
	}

	std::shared_ptr<Material> MaterialLoader::FindMaterialByName(const std::string& name) const
	{
		for (auto& it : _loadedMaterials)
		{
			auto mat = it.second.lock();
			if (mat->GetName() == name)
				return mat;
		}

		return nullptr;
	}

	Dialog* MaterialLoader::CreateEditorDialog(const std::vector<fs::path>& paths)
	{
		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		uint32_t cx = width >> 1;
		uint32_t cy = height >> 1;

		const int32_t dlgW = 800;
		const int32_t dlgH = 600;

		auto mat = Material::Create(paths[0]);

		if (mat)
		{
			MaterialDialog* dlg = new MaterialDialog(
				g_pEnv->GetUIManager().GetRootElement(),
				Point(cx - dlgW / 2, cy - dlgH / 2),
				Point(dlgW, dlgH),
				std::format(L"Editing Material '{}'", paths[0].filename().wstring()),
				mat);

			return dlg;
		}

		return nullptr;

	}

	void MaterialLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		Material* material = dynamic_cast<Material*>(resource);

		//auto path = material->GetAbsolutePath();// _material->GetOwningFileSystem()->GetLocalAbsoluteDataPath();

		JsonFile file(path, std::ios::out | std::ios::trunc);

		if (file.Open() == false)
		{
			MessageBox::Info(L"Could not save material", L"Failed to open material file for saving");
			return;
		}

		json data;

		auto& textures = data["textures"];
		{
			for (auto i = 0; i < MaterialTexture::Count; ++i)
			{
				auto tex = material->GetTexture((MaterialTexture)i);

				if (tex)
				{
					auto texName = Material::GetMaterialTextureName((MaterialTexture)i);
					auto texNameA = std::string(texName.begin(), texName.end());

					textures[texNameA] = tex->GetFileSystemPath();
				}
			}
		}

		auto& properties = data["properties"];
		{
			file.Serialize(properties, "metallicFactor", material->_properties.metallicFactor);
			file.Serialize(properties, "roughnessFactor", material->_properties.roughnessFactor);
			file.Serialize(properties, "diffuseColour", material->_properties.diffuseColour);
			file.Serialize(properties, "emissiveColour", material->_properties.emissiveColour);
			file.Serialize(properties, "hasTransparency", material->_properties.hasTransparency);
			file.Serialize(properties, "isWater", material->_properties.isWater);
		}

		auto& renderer = data["renderer"];
		{
			file.Serialize(renderer, "depthState", material->GetDepthState());
			file.Serialize(renderer, "blendState", material->GetBlendState());
			file.Serialize(renderer, "cullingMode", material->GetCullMode());
			file.Serialize(renderer, "format", material->GetFormat());
		}

		auto& shaders = data["shaders"];
		{
			if (auto shader = material->GetStandardShader(); shader != nullptr)
				file.Serialize(shaders, "standard", shader->GetFileSystemPath());

			if (auto shader = material->GetShadowMapShader(); shader != nullptr)
				file.Serialize(shaders, "shadowmap", shader->GetFileSystemPath());
		}

		auto jsonString = data.dump(2);

		file.Write(jsonString.data(), (uint32_t)jsonString.length());
		file.Flush();
		file.Close();

		LOG_DEBUG("Material file successfully written");

		//_material->Save(&file);
	}
}