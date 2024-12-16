
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Terrain : public Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool)>;

		Terrain(Element* parent, const Point& position, const Point& size);
		~Terrain();

		static Terrain* CreateTerrainDialog(Element* parent, OnCompleted onCompletedAction);

		bool GenerateTerrain();

	private:
		ComponentWidget* _widgetBase = nullptr;
		ComponentWidget* _shadowSettings = nullptr;

		float _heightScale = 1.0f;
		float _width = 1024.0f;
		int32_t _gridSize = 10;
		float _uvScale = 4.4f;
		int32_t _resolution = 64;
		int32_t _maxLod = 0;
		bool _parent = true;
		bool _makeColliders = true;
		bool _useChunkSystem = true;

		LineEdit* _materialName;
		LineEdit* _seed;
		std::vector<Entity*> _created;
	};
}
