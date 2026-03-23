
#pragma once

#include "../HexEngine.hpp"

namespace HexEngine
{
	struct IconPending
	{
		bool isAsset = false;
		fs::path path;
		std::wstring assetPackage;
	};
	class HEX_API IconService
	{
	public:
		void Create(const std::wstring& sceneName);

		void Destroy();

		void PushFilePathForIconGeneration(const fs::path& path);
		void PushAssetPathForIconGeneration(const std::wstring& assetPackage, const fs::path& assetPath);

		ITexture2D* GetIcon(const fs::path& path);
		void RemoveIcon(const fs::path& path);

		void Render();
		void CompletedFrame();

	private:
		void ClearPreviewEntities();

		std::vector<IconPending> _pendingPaths;
		std::vector<fs::path> _generatedPaths;
		std::map<fs::path, ITexture2D*> _icons;
		std::map<fs::path, std::shared_ptr<ITexture2D>> _resourceIcons;
		

		std::shared_ptr<Scene> _iconScene;
		Camera* _camera = nullptr;
		std::vector<Entity*> _previewRootEntities;
	};
}
