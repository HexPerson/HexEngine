
#pragma once

#include "../Graphics/UIInstance.hpp"
#include "../Graphics/TextureAtlas.hpp"

namespace HexEngine
{
	struct GuiInstanceData
	{
		UIInstance* instance;
		//ITexture2D* texture;
	};

	class DrawList
	{
	public:		
		DrawList()
		{
			_quadInstance = new UIInstance;
		}

		~DrawList()
		{
			SAFE_DELETE(_quadInstance);
		}

		void Clear()
		{
			_atlas.Clear();
			_instances.clear();
			//_scissorRects.clear();
		}

		std::vector<GuiInstanceData> _instances;
		TextureAtlas _atlas;
		UIInstance* _quadInstance = nullptr;
		//std::vector<RECT> _scissorRects;
	};
}
