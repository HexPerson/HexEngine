#include "FolderExplorer.hpp"

namespace HexEditor
{
	FolderExplorer::FolderExplorer(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Element(parent, position, size)
	{
		_folderView = new HexEngine::TreeList(this, HexEngine::Point(0, 0), size);
	}

	void FolderExplorer::SetOnFolderSelected(OnFolderSelectedFn fn)
	{
		_onFolderSelected = std::move(fn);
	}

	bool FolderExplorer::OnClickFolder(HexEngine::ListNode* item, int32_t mouseButton)
	{
		if (mouseButton != VK_LBUTTON || item == nullptr)
			return true;

		HexEngine::ListNode* pathItem = item;
		std::wstring assetFolderPath;
		HexEngine::FileSystem* fs = nullptr;

		if (fs::path(pathItem->GetLabel()).extension() == ".pkg")
		{
			assetFolderPath = pathItem->GetLabel();
		}
		else
		{
			while (pathItem)
			{
				if (pathItem->GetParent() == nullptr && fs == nullptr)
				{
					fs = static_cast<HexEngine::FileSystem*>(pathItem->_userData);
				}
				else
				{
					assetFolderPath.insert(0, pathItem->GetLabel() + L"/");
				}

				pathItem = pathItem->GetParent();
			}
		}

		if (_onFolderSelected)
		{
			_onFolderSelected(assetFolderPath, fs);
		}

		return true;
	}

	void FolderExplorer::RecurseList(HexEngine::ListNode* parent, const fs::path& path)
	{
		for (auto it = fs::directory_iterator(path); it != fs::directory_iterator(); ++it)
		{
			auto p = *it;

			if (!fs::is_directory(p))
				continue;

			auto* currentItem = new HexEngine::ListNode(
				_folderView,
				fs::relative(p, path),
				{
					HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.img_folder_open.get(),
					HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.img_folder_closed.get()
				});

			currentItem->_onClick = std::bind(&FolderExplorer::OnClickFolder, this, std::placeholders::_1, std::placeholders::_2);

			_folderView->AddNode(currentItem, parent);
			RecurseList(currentItem, p.path());
		}
	}

	void FolderExplorer::UpdateFolderView()
	{
		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (scene != nullptr)
			scene->Lock();

		_folderView->Clear();

		for (auto& fs : HexEngine::g_pEnv->GetResourceSystem().GetFileSystems())
		{
			auto* rootPath = new HexEngine::ListNode(
				_folderView,
				std::wstring(fs->GetName().begin(), fs->GetName().end()),
				{
					HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.img_folder_open.get(),
					HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.img_folder_closed.get()
				},
				fs);

			rootPath->_onClick = std::bind(&FolderExplorer::OnClickFolder, this, std::placeholders::_1, std::placeholders::_2);
			_folderView->AddNode(rootPath);
			RecurseList(rootPath, fs->GetDataDirectory());
		}

		if (scene != nullptr)
			scene->Unlock();
	}
}
