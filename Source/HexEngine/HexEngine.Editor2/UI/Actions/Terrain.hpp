
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Terrain : public HexEngine::Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool)>;

		Terrain(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~Terrain();

		static Terrain* CreateTerrainDialog(Element* parent, OnCompleted onCompletedAction);
		static Terrain* CreateOceanDialog(Element* parent, OnCompleted onCompletedAction);

		bool GenerateTerrain();

	private:
		HexEngine::ComponentWidget* _widgetBase = nullptr;
		HexEngine::ComponentWidget* _shadowSettings = nullptr;

		float _heightScale = 1.0f;
		float _width = 1024.0f;
		int32_t _gridSize = 4;
		float _uvScale = 5.0f;
		int32_t _resolution = 32;
		int32_t _maxLod = 0;
		bool _parent = true;
		bool _makeColliders = true;
		bool _useChunkSystem = true;

		HexEngine::AssetSearch* _materialName;
		HexEngine::LineEdit* _seed;
		std::vector<HexEngine::Entity*> _created;
	};
}
