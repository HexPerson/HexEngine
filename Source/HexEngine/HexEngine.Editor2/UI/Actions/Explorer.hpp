
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "AssetExplorer.hpp"
#include "FolderExplorer.hpp"
#include <mutex>

namespace HexEditor
{
	class LogScrollView;

	class Explorer : public HexEngine::Dock, public HexEngine::ILogFileListener
	{
	public:
		using AssetDesc = AssetExplorer::AssetDesc;

		Explorer(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		virtual ~Explorer();

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void UpdateFolderView();
		void InvalidateAssetPreview(const fs::path& assetPath);

		void SetProjectPath(const fs::path& path);

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void OnLogMessage(const HexEngine::LogMessage& message) override;

		AssetDesc* GetCurrentlyDraggedAsset() const;
		bool ConsumeRecentlyDroppedAssetPath(fs::path& outPath);

	private:
		void OnFolderSelected(const fs::path& relativePath, HexEngine::FileSystem* fs);
		void OnEnterSearchText(const std::wstring& text);

	private:
		fs::path _projectPath;
		FolderExplorer* _folderExplorer = nullptr;
		AssetExplorer* _assetExplorer = nullptr;
		LogScrollView* _logView = nullptr;
		HexEngine::TabView* _tab;
		HexEngine::TabItem* _assetsTab = nullptr;
		HexEngine::TabItem* _logTab = nullptr;
		HexEngine::LineEdit* _fileSearchBar = nullptr;
		HexEngine::Checkbox* _showDebugCheckbox = nullptr;
		HexEngine::Checkbox* _showInfoCheckbox = nullptr;
		HexEngine::Checkbox* _showWarnCheckbox = nullptr;
		HexEngine::Checkbox* _showCritCheckbox = nullptr;

		bool _showDebugLogs = true;
		bool _showInfoLogs = true;
		bool _showWarnLogs = true;
		bool _showCritLogs = true;

		std::recursive_mutex _pendingLogLock;
		std::vector<HexEngine::LogMessage> _pendingMessages;
	};
}
