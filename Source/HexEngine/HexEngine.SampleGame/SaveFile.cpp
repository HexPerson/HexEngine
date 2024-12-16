

#include "SaveFile.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../HexEngine.Core/Entity/Reflection/GUID.hpp"
#include "Game\World\World.hpp"
#include "Game\StreetLight.hpp"

namespace CityBuilder
{
	SaveFile::SaveFile(const fs::path& absolutePath, std::ios_base::openmode openMode) : 
		DiskFile(absolutePath, openMode, HexEngine::DiskFileOptions::CreateSubDirs)
	{
	}

	bool SaveFile::Save()
	{
		LOG_DEBUG("Opening save file for writing");

		if (!Open())
		{
			LOG_CRIT("Failed to open save file for writing");
			return false;
		}

		const auto& ents = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetEntities();

		std::vector<HexEngine::Entity*> finalEntityList;

		for (auto&& ent : ents)
		{
			auto guid = ent->GetGUID();

#define IGNORE_ENTITY_SAVE(type) else if(guid == type::GetObjectGUID()) { continue; }

			if (false) {}
			IGNORE_ENTITY_SAVE(HexEngine::Chunk)
			IGNORE_ENTITY_SAVE(HexEngine::Terrain)
			IGNORE_ENTITY_SAVE(HexEngine::SpotLight)
			IGNORE_ENTITY_SAVE(StreetLight)

			finalEntityList.push_back(ent);
		}

		SaveFileHeader header;
		header.version = Version;
		header.numEnts = finalEntityList.size();

		if (!Write(&header, sizeof(header)))
		{
			LOG_CRIT("Unable to write save data header");
			return false;
		}

		
		for (auto&& ent : finalEntityList)
		{
			auto pos = ent->GetPosition();
			auto rot = ent->GetRotation();
			auto scale = ent->GetScale();
			auto guid = ent->GetGUID();

			LOG_DEBUG("Saving entity '%s' GUID: %s", ent->GetName().c_str(), GUID_toString(guid).c_str());

			// guid must come first
			//
			Write(&guid, sizeof(GUID));

			ent->Save(this);
		}	

		LOG_DEBUG("Save file successfully written");

		Close();

		return true;
	}

	bool SaveFile::Load()
	{
		LOG_DEBUG("Opening save file for reading");

		if (!Open())
		{
			LOG_CRIT("Failed to open save file for reading");
			return false;
		}

		auto ents = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetEntities();

		SaveFileHeader header = {};

		if (!Read(&header, sizeof(header)))
		{
			LOG_CRIT("Unable to read save data header");
			return false;
		}

		if (header.version != Version)
		{
			LOG_CRIT("Save file version is not compatible!");
			return false;
		}

		LOG_DEBUG("Loading %d entities", header.numEnts);

		for (auto i = 0ul; i < header.numEnts; ++i)
		{
			math::Vector3 pos, rot, scale;
			GUID guid;

			Read(&guid, sizeof(GUID));

			LOG_DEBUG("Loading entity GUID '%s'", GUID_toString(guid).c_str());

#define HANDLE_ENTITY_LOAD(type) else if(guid == type::GetObjectGUID()) { type* _entity = type::Load(this); }
#define IGNORE_ENTITY_LOAD(type) else if(guid == type::GetObjectGUID()) { }

			if (false) {}
			HANDLE_ENTITY_LOAD(HexEngine::Terrain)
			HANDLE_ENTITY_LOAD(HexEngine::Camera)
			HANDLE_ENTITY_LOAD(HexEngine::DirectionalLight)
			//HANDLE_ENTITY_LOAD(HexEngine::SpotLight)
			HANDLE_ENTITY_LOAD(World)
			IGNORE_ENTITY_LOAD(HexEngine::Chunk) // chunks are created from the world, so we don't need to handle them
			else
			{
				LOG_CRIT("Unknown GUID '%s'", GUID_toString(guid).c_str());
			}
		}

		LOG_DEBUG("Save file successfully read");

		Close();

		return true;
	}
}