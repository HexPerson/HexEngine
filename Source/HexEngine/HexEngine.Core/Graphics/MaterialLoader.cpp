
#include "MaterialLoader.hpp"
#include "Material.hpp"
#include "MaterialGraphCompiler.hpp"

#include "../FileSystem/FileSystem.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../GUI/Elements/MaterialDialog.hpp"
#include "../GUI/Elements/MaterialGraphDialog.hpp"
#include "../GUI/UIManager.hpp"
#include "../GUI/Elements/MessageBox.hpp"
#include <chrono>
#include <thread>

namespace HexEngine
{
	namespace
	{
		bool TryParseMaterialJson(const std::string& data, json& outJson)
		{
			outJson = json::parse(data, nullptr, false, true);
			return !outJson.is_discarded();
		}

		bool TryReadAndParseMaterialJson(const fs::path& absolutePath, json& outJson, int32_t attempts = 3, int32_t retryDelayMs = 15)
		{
			for (int32_t attempt = 0; attempt < attempts; ++attempt)
			{
				JsonFile file(absolutePath, std::ios::in);
				if (!file.DoesExist() || !file.Open())
					return false;

				std::string data;
				file.ReadAll(data);
				if (TryParseMaterialJson(data, outJson))
					return true;

				if (attempt + 1 < attempts)
					std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
			}

			return false;
		}
	}

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
		json matData;
		if (!TryReadAndParseMaterialJson(absolutePath, matData))
		{
			LOG_WARN("Failed to parse material json '%s'", absolutePath.string().c_str());
			return nullptr;
		}

		ParseJson(&file, matData, material);

		_loadedMaterials[absolutePath] = material;

		material->SetName(absolutePath.filename().string());

		return material;
	}

	std::shared_ptr<IResource> MaterialLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::string materialData((const char*)data.data(), data.size());

		KeyValues kv;
		if(kv.Parse(materialData) == false)
		{
			LOG_CRIT("Failed to parse key values from material file '%s'", relativePath.string().c_str());
			return nullptr;
		}

		std::shared_ptr<Material> material = std::shared_ptr<Material>(new Material, ResourceDeleter());
		
		json matData;
		if (!TryParseMaterialJson(materialData, matData))
		{
			LOG_WARN("Failed to parse material json from memory '%s'", relativePath.string().c_str());
			return nullptr;
		}

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
		if (materialToReload != nullptr && materialToReload->IsHotReloadSuppressed())
		{
			LOG_DEBUG(
				"Skipping material hot-reload for '%s' while material graph editor is open.",
				resource->GetAbsolutePath().string().c_str());
			return;
		}

		json matData;
		if (!TryReadAndParseMaterialJson(resource->GetAbsolutePath(), matData))
		{
			LOG_WARN("Skipping material hot-reload; json parse failed for '%s' (likely mid-write).", resource->GetAbsolutePath().string().c_str());
			return;
		}

		ParseJson(&file, matData, materialToReload);
	}

	void MaterialLoader::ParseJson(JsonFile* file, json& json, std::shared_ptr<Material>& material)
	{
		material->_hasGraph = false;
		material->_hasGraphInstance = false;
		
		// Load textures
		//
		auto textures = json["textures"];

		for (auto i = 0; i < MaterialTexture::Count; ++i)
		{
			auto texName = Material::GetMaterialTextureName((MaterialTexture)i);
			auto texNameA = ws2s(texName);

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

			bool emissiveAffectsGI = false;
			file->Deserialize(properties, "emissiveAffectsGI", emissiveAffectsGI);
			material->SetEmissiveAffectsGI(emissiveAffectsGI);
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

		// Load graph data
		//
		if (auto graphIt = json.find("graph"); graphIt != json.end() && graphIt->is_object())
		{
			std::vector<std::string> graphErrors;
			if (MaterialGraph::Deserialize(*graphIt, material->_graph, &graphErrors))
			{
				material->_hasGraph = true;
			}
			else
			{
				for (const auto& error : graphErrors)
					LOG_WARN("Material graph parse warning: %s", error.c_str());
			}
		}

		// Load graph instance data
		//
		if (auto instanceIt = json.find("instance"); instanceIt != json.end() && instanceIt->is_object())
		{
			std::vector<std::string> instanceErrors;
			if (MaterialGraph::DeserializeInstance(*instanceIt, material->_graphInstance, &instanceErrors))
			{
				material->_hasGraphInstance = true;
			}
			else
			{
				for (const auto& error : instanceErrors)
					LOG_WARN("Material graph instance parse warning: %s", error.c_str());
			}
		}

		// Apply instance overrides through the graph compiler.
		if (material->_hasGraphInstance && !material->_graphInstance.parentMaterialPath.empty())
		{
			auto parent = Material::Create(material->_graphInstance.parentMaterialPath);
			if (parent != nullptr && parent->_hasGraph)
			{
				const auto compileResult = MaterialGraphCompiler::ApplyInstanceToMaterial(
					parent->_graph,
					material->_graphInstance,
					*material);

				if (!compileResult.success)
				{
					for (const auto& error : compileResult.errors)
						LOG_WARN("Material instance compile error: %s", error.c_str());
				}
				else
				{
					for (const auto& warning : compileResult.warnings)
						LOG_WARN("Material instance compile warning: %s", warning.c_str());
				}
			}
			else
			{
				LOG_WARN(
					"Material instance '%s' could not resolve parent graph material '%s'",
					material->GetFileSystemPath().string().c_str(),
					material->_graphInstance.parentMaterialPath.string().c_str());
			}
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
		if (paths.empty())
			return nullptr;

		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		uint32_t cx = width >> 1;
		uint32_t cy = height >> 1;

		const int32_t dlgW = 800;
		const int32_t dlgH = 600;

		auto& resourceSystem = g_pEnv->GetResourceSystem();
		const fs::path inputPath = paths[0];

		std::shared_ptr<Material> mat = dynamic_pointer_cast<Material>(resourceSystem.FindResourceByFileName(inputPath));

		if (!mat && inputPath.is_absolute())
		{
			for (auto* fileSystem : resourceSystem.GetFileSystems())
			{
				if (fileSystem == nullptr)
					continue;

				std::error_code ec;
				const fs::path rel = fs::relative(inputPath, fileSystem->GetDataDirectory(), ec);
				if (ec || rel.empty())
					continue;

				const auto relStr = rel.generic_string();
				if (relStr.size() >= 2 && relStr[0] == '.' && relStr[1] == '.')
					continue;

				const fs::path fsPath = fileSystem->GetRelativeResourcePath(rel.wstring());
				mat = dynamic_pointer_cast<Material>(resourceSystem.FindResourceByFileName(fsPath));
				if (mat)
					break;
			}
		}

		if (!mat)
			mat = Material::Create(inputPath);

		if (mat)
		{
			if (mat->_hasGraph)
			{
				MaterialGraphDialog* graphDialog = new MaterialGraphDialog(
					g_pEnv->GetUIManager().GetRootElement(),
					Point(cx - dlgW / 2, cy - dlgH / 2),
					Point(dlgW, dlgH),
					std::format(L"Editing Material Graph '{}'", paths[0].filename().wstring()),
					mat);
				return graphDialog;
			}
			else
			{
				MaterialDialog* dlg = new MaterialDialog(
					g_pEnv->GetUIManager().GetRootElement(),
					Point(cx - dlgW / 2, cy - dlgH / 2),
					Point(dlgW, dlgH),
					std::format(L"Editing Material '{}'", paths[0].filename().wstring()),
					mat);

				return dlg;
			}
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
					auto texNameA = ws2s(texName);

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
			file.Serialize(properties, "emissiveAffectsGI", material->GetEmissiveAffectsGI());
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

		if (material->_hasGraph)
		{
			auto& graphJson = data["graph"];
			MaterialGraph::Serialize(graphJson, material->_graph);
		}

		if (material->_hasGraphInstance)
		{
			auto& instanceJson = data["instance"];
			MaterialGraph::SerializeInstance(instanceJson, material->_graphInstance);
		}

		auto jsonString = data.dump(2);

		file.Write(jsonString.data(), (uint32_t)jsonString.length());
		file.Flush();
		file.Close();

		LOG_DEBUG("Material file successfully written");

		//_material->Save(&file);
	}
}
