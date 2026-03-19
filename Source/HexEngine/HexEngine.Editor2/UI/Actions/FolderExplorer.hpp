#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class FolderExplorer : public HexEngine::Element
	{
	public:
		using OnFolderSelectedFn = std::function<void(const fs::path& relativePath, HexEngine::FileSystem* fs)>;

		FolderExplorer(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		void SetOnFolderSelected(OnFolderSelectedFn fn);
		void UpdateFolderView();

	private:
		bool OnClickFolder(HexEngine::ListNode* item, int32_t mouseButton);
		void RecurseList(HexEngine::ListNode* parent, const fs::path& path);

	private:
		HexEngine::TreeList* _folderView = nullptr;
		OnFolderSelectedFn _onFolderSelected;
	};
}
