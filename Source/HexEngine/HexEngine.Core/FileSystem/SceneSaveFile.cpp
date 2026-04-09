

#include "SceneSaveFile.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	SceneSaveFile::SceneSaveFile(const fs::path& absolutePath, std::ios_base::openmode openMode, std::shared_ptr<Scene> scene, SceneFileFlags flags) :
		JsonFile(absolutePath, openMode, HexEngine::DiskFileOptions::CreateSubDirs),
		_scene(scene),
		_flags(flags)
	{
	}

	bool SceneSaveFile::Save()
	{
		LOG_DEBUG("Opening save file for writing");

		if (!Open())
		{
			LOG_CRIT("Failed to open save file for writing");
			return false;
		}

		const auto& ents = _scene->GetEntities();

		std::vector<HexEngine::Entity*> finalEntityList;

		for (auto&& vec : ents)
		{
			for (auto&& ent : vec.second)
			{
				if(ent->HasFlag(EntityFlags::DoNotSave) == false)
					finalEntityList.push_back(ent);
			}
		}

		json saveFileData;

		saveFileData["header"] = {
			{"version", Version},
			//{"numEntities", (uint32_t)finalEntityList.size()},
			//{"numVariables", g_pEnv->_commandManager->GetNumVars()}
		};

		/*SaveFileHeader header;
		header.version = Version;
		header.numEnts = (uint32_t)finalEntityList.size();
		header.numVars = g_pEnv->_commandManager->GetNumVars();*/

		/*if (!Write(&header, sizeof(header)))
		{
			LOG_CRIT("Unable to write save data header");
			return false;
		}*/

		if (!HEX_HASFLAG(_flags, SceneFileFlags::DontSaveVariables))
		{
			auto& variables = saveFileData["variables"];

			g_pEnv->_commandManager->SaveVars(variables);
		}

		auto& entities = saveFileData["entities"];

		for (auto&& ent : finalEntityList)
		{
			LOG_DEBUG("Saving entity '%s' with %d components", ent->GetName().c_str(), ent->GetAllComponents().size());

			ent->Serialize(entities, this);
		}

		if (!HEX_HASFLAG(_flags, SceneFileFlags::DontSaveHierarchy))
		{
			auto& hierarchy = saveFileData["hierarchy"];

			// now save all the parent information
			for (auto&& ent : finalEntityList)
			{
				if (ent->GetParent() != nullptr)
				{
					hierarchy[ent->GetName()] = ent->GetParent()->GetName();
				}
			}
		}

		_scene->Save(saveFileData, this);

		auto jsonString = saveFileData.dump(2);

		Write(jsonString.data(), (uint32_t)jsonString.length());

		LOG_DEBUG("Save file successfully written");

		Flush();
		Close();		

		return true;
	}

	bool SceneSaveFile::Save(const std::vector<HexEngine::Entity*>& entitiesVec)
	{
		LOG_DEBUG("Opening save file for writing");

		if (!Open())
		{
			LOG_CRIT("Failed to open save file for writing");
			return false;
		}

		json saveFileData;

		bool isPrefab = HEX_HASFLAG(_flags, SceneFileFlags::IsPrefab);

		saveFileData["header"] = {
			{"version", Version},
			//{"numEntities", (uint32_t)finalEntityList.size()},
			//{"numVariables", g_pEnv->_commandManager->GetNumVars()}
		};

		if (!HEX_HASFLAG(_flags, SceneFileFlags::DontSaveVariables) && isPrefab == false)
		{
			auto& variables = saveFileData["variables"];

			g_pEnv->_commandManager->SaveVars(variables);
		}

		auto& entities = saveFileData["entities"];

		for (auto&& ent : entitiesVec)
		{
			LOG_DEBUG("Saving entity '%s' with %d components", ent->GetName().c_str(), ent->GetAllComponents().size());

			if (ent->HasFlag(EntityFlags::DoNotSave) == false)
				ent->Serialize(entities, this);
		}

		if (!HEX_HASFLAG(_flags, SceneFileFlags::DontSaveHierarchy))
		{
			auto& hierarchy = saveFileData["hierarchy"];

			// now save all the parent information
			for (auto&& ent : entitiesVec)
			{
				if (ent->HasFlag(EntityFlags::DoNotSave) == false)
				{
					if (ent->GetParent() != nullptr)
					{
						hierarchy[ent->GetName()] = ent->GetParent()->GetName();
					}
				}
			}
		}

		auto jsonString = saveFileData.dump(2);

		Write(jsonString.data(), (uint32_t)jsonString.length());

		LOG_DEBUG("Save file successfully written");

		Flush();
		Close();


		return true;
	}

	bool SceneSaveFile::Load(std::shared_ptr<Scene> loadIntoExistingScene, SceneSaveProgressCallback callback)
	{
		LOG_DEBUG("Opening save file for reading");

		if (!Open())
		{
			LOG_CRIT("Failed to open save file for reading");
			return false;
		}

		std::string data;
		ReadAll(data);

		if (data.length() == 0)
		{
			CON_ECHO("^rInput file is empty!");
			return false;
		}

		json sceneData = json::parse(data);

		bool isPrefab = HEX_HASFLAG(_flags, SceneFileFlags::IsPrefab);

		if (isPrefab == false)
			loadIntoExistingScene->SetName(_fsPathObj.stem().wstring());

		const auto& headerData = sceneData["header"];

		if (headerData["version"].get<int32_t>() != Version)
		{
			LOG_CRIT("Save file version is not compatible!");
			return false;
		}

		if (isPrefab == false)
			g_pEnv->_commandManager->LoadVars(sceneData["variables"]);

		//LOG_DEBUG("Loading %d entities", header.numEnts);

		std::vector<std::pair<json, Entity*>> createdEnts;

		for (auto& ent : sceneData["entities"].items())
		{
			auto entity = Entity::LoadFromFile(ent.value(), ent.key(), loadIntoExistingScene.get(), this);

			createdEnts.push_back({
				ent.value(),
				entity
				});
		}

		

		auto& hierarchy = sceneData["hierarchy"];

		for (auto p : hierarchy.items())
		{
			auto child = loadIntoExistingScene->GetEntityByName(p.key());
			auto parent = loadIntoExistingScene->GetEntityByName(p.value());

			if (child && parent)
			{
				loadIntoExistingScene->Lock();
				child->SetParent(parent);
				loadIntoExistingScene->Unlock();
			}
		}

		_loadedEntities.clear();

		// Deserialize the transforms first, because other components may depends on the transforms being correct
		for (auto& ent : createdEnts)
		{
			//loadIntoExistingScene->Lock();
			ent.second->Deserialize(ent.first, this, 1 << Transform::_GetComponentId());
			//loadIntoExistingScene->Unlock();

			_loadedEntities.push_back(ent.second);
		}		

		int32_t loadedCount = 0;

		for (auto& ent : createdEnts)
		{
			if (callback)
				callback(std::wstring(ent.second->GetName().begin(), ent.second->GetName().end()), loadedCount, (uint32_t)createdEnts.size());

			//loadIntoExistingScene->Lock();
			ent.second->Deserialize(ent.first, this);
			//loadIntoExistingScene->Unlock();

			//  we want to force the PVS to rebuild each time an entity is loaded, in the case of streaming scene files
			if (auto mainCamera = loadIntoExistingScene->GetMainCamera(); mainCamera != nullptr)
			{
				mainCamera->GetPVS()->DisableUpdates(true);
			}

			loadedCount++;
		}

		if (auto mainCamera = loadIntoExistingScene->GetMainCamera(); mainCamera != nullptr)
		{
			mainCamera->GetPVS()->DisableUpdates(false);
		}

		// Rebuild all camera/light PVS sets once after load, instead of flushing per-transform during deserialize.
		loadIntoExistingScene->ForceRebuildPVS();

		loadIntoExistingScene->Load(sceneData, this);

		LOG_DEBUG("Scene save file successfully loaded");

		Close();

		return true;
	}

	bool SceneSaveFile::Load(SceneSaveProgressCallback callback)
	{
		return Load(_scene, callback);
	}

	std::shared_ptr<Scene> SceneSaveFile::GetScene() const
	{
		return _scene;
	}

	bool SceneSaveFile::IsSceneAttached() const
	{
		return _scene != nullptr;
	}

	const std::vector<Entity*>& SceneSaveFile::GetLoadedEntities() const
	{
		return _loadedEntities;
	}
}
