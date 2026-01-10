
#include "Explorer.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <HexEngine.Core/GUI/Elements/MessageBox.hpp>

namespace HexEditor
{
	Explorer::Explorer(Element* parent, const Point& position, const Point& size) :
		Dock(parent, position, size, Dock::Anchor::Bottom)
	{
		_folderView = new TreeList(this, Point(10, 10), Point(size.y-20, size.y-20));
		//_folderView->_onSelect = std::bind(&Explorer::OnClickFolder, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

		_tab = new TabView(this, Point(size.y, 10), Point(g_pEnv->_uiManager->GetWidth() - (size.y + 10), size.y - 20));
		_tab->AddTab(L"Assets");
		_tab->AddTab(L"Log");

		_fileSearchBar = new LineEdit(_tab, Point(10, 20), Point(_tab->GetSize().x - 20, 20), L"");
		_fileSearchBar->SetIcon(ITexture2D::Create(L"EngineData.Textures/UI/magnifying_glass.png"), math::Color(1,1,1,1));
		_fileSearchBar->SetOnInputFn(std::bind(&Explorer::OnEnterSearchText, this, std::placeholders::_2));
		_fileSearchBar->SetDoesCallbackWaitForReturn(false);
	}

	void Explorer::OnEnterSearchText(const std::wstring& text)
	{
		_searchFilter = text;

		std::transform(_searchFilter.begin(), _searchFilter.end(), _searchFilter.begin(), ::tolower);
		UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);
	}

	bool Explorer::OnClickFolder(TreeList* list, ListNode* item, int32_t mouseButton)
	{
		if (mouseButton == VK_LBUTTON)
		{
			ListNode* pathItem = item;

			std::wstring assetFolderPath;

			FileSystem* fs = nullptr;

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
						fs = (FileSystem*)pathItem->_userData;
					}
					else
						assetFolderPath.insert(0, pathItem->GetLabel() + L"/");

					pathItem = pathItem->GetParent();
				}

				_currentlyBrowsedFS = fs;
				_currentlyBrowsedFolder = assetFolderPath;
			}

			OpenAssetFolder(assetFolderPath, fs);
		}
		return true;
	}

	void Explorer::OpenAssetFolder(const fs::path& relativePath, FileSystem* fs)
	{
		UpdateAssets(relativePath, fs);
	}

	void Explorer::CreateNewMaterial(const fs::path& baseDir)
	{
		const int32_t width = 800;
		const int32_t height = 600;

		fs::path newMaterialPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(baseDir / L"NewMaterial.hmat");

		/*JsonFile file(newMaterialPath, std::ios::out);

		if (file.Open() == false)
		{
			MessageBox::Info(L"Error", L"Failed to create material on disk!");
			return;
		}*/

		Material* material = new Material;

		// copy from the standard material just so that the shaders are valid
		material->SetPaths(newMaterialPath, _currentlyBrowsedFS);
		material->CopyFrom(Material::GetDefaultMaterial());
		material->SetLoader(g_pEnv->_resourceSystem->FindResourceLoaderForExtension(".hmat"));
		material->Save();

		//file.Close();	

		// update the folder view
		UpdateAssets(baseDir, _currentlyBrowsedFS);

		auto assetToEdit = FindAssetInView(newMaterialPath);

		if (assetToEdit)
		{
			EditAssetName(assetToEdit);
		}
	}

	Explorer::AssetDesc* Explorer::FindAssetInView(const fs::path& filename)
	{
		for (auto& desc : _assetsInView)
		{
			if (desc.path == filename)
			{
				return &desc;
			}
		}
		return nullptr;
	}

	void Explorer::EditAssetName(AssetDesc* asset)
	{
		_assetNameToEdit = asset;
		_editingAssetExtension = asset->path.extension();
	}

	bool Explorer::OnInputEvent(InputEvent event, InputData* data)
	{
		auto pos = GetAbsolutePosition();

		if (_assetNameToEdit && event == InputEvent::Char)
		{
			if (data->Char.ch == VK_BACK && _editingAssetTempName.size() > 0)
				_editingAssetTempName.pop_back();
			else if (data->Char.ch == VK_RETURN)
			{
				fs::path assetRenamedName = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / (_editingAssetTempName + _editingAssetExtension));

				// rename the file on disk
				fs::rename(_assetNameToEdit->path, assetRenamedName);

				// reset the cached names so they are rebuilt
				_assetNameToEdit->assetNameFull.clear();
				_assetNameToEdit->assetNameShort.clear();
				_assetNameToEdit->path = assetRenamedName;

				_assetNameToEdit = nullptr;
			}
			else
			{
				_editingAssetTempName.push_back(data->Char.ch);
			}

			return true;
		}

		if (IsMouseOver())
		{
			if (event == InputEvent::DragAndDrop)
			{
				if (data->DragAndDrop.x >= pos.x && data->DragAndDrop.x < pos.x + _size.x &&
					data->DragAndDrop.y >= pos.y && data->DragAndDrop.y < pos.y + _size.y)
				{
					fs::path fileName = data->DragAndDrop.path;

					if (fileName.wstring().find(L".pkg") != std::wstring::npos)
					{
						LOG_DEBUG("Skipping drag and drop request for file '%S' because it belongs to a package file", fileName.wstring().c_str());
						return true;
					}

					LOG_DEBUG("Received drag and drop request for file '%S'", fileName.wstring().c_str());

					IResourceLoader* resourceLoader = g_pEnv->_resourceSystem->FindResourceLoaderForExtension(fileName.extension().string());

					if (!resourceLoader)
					{
						LOG_CRIT("Unable to load resource as we don't have a loader for extension '%s'", fileName.extension().string().c_str());
						return true;
					}

					//fs::path localResourcePath = g_pEditor->_projectFS->GetLocalAbsoluteDataPath(resourceLoader->GetResourceDirectory());

					//if (!fs::exists(localResourcePath))
					//	fs::create_directories(localResourcePath);

					std::wstring relative = _currentlyBrowsedFS->GetRelativeResourcePath(fs::relative(fileName, _currentlyBrowsedFS->GetDataDirectory()));// _currentlyBrowsedFS->GetName() + L".") + fs::relative(fileName, _currentlyBrowsedFS->GetDataDirectory()).wstring();

					//if(relative.empty())
					//	localResourcePath /= fileName.filename();
					//else
					//	localResourcePath /= relative;

					//// perform the copy
					//if (!fs::exists(localResourcePath))
					//	fs::copy_file(fileName, localResourcePath);

					LoadAsset(relative);
					return true;
				}
			}
			else if (event == InputEvent::MouseDoubleClick)
			{
				if (data->MouseDown.button == VK_LBUTTON)
				{
					if (_hoveredAsset != nullptr && IsMouseOver(true))
					{
						//LoadAsset(fs::relative(_hoveredAsset->path, g_pEditor->_projectFS->GetDataDirectory()));

						IResourceLoader* resourceLoader = g_pEnv->_resourceSystem->FindResourceLoaderForExtension(_hoveredAsset->path.extension().string());

						if (resourceLoader)
						{
							std::wstring relative = (_currentlyBrowsedFS->GetName() + L".") + fs::relative(_hoveredAsset->path, _currentlyBrowsedFS->GetDataDirectory()).wstring();
							
							// if the editor dialog is null, we should just presume that no import options are needed and immediately load the resource
							if (auto dlg = resourceLoader->CreateEditorDialog({ relative }); dlg == nullptr)
							{
								g_pEnv->_resourceSystem->LoadResource(_hoveredAsset->path);
							}
						}

						_hoveredAsset = nullptr;
						return true;
					}
				}
			}
			else if (event == InputEvent::MouseDown)
			{
				if (data->MouseDown.button == VK_LBUTTON)
				{
					if (_hoveredAsset != nullptr && IsMouseOver(true))
					{
						_hoveredAsset->dragging = true;

						_dragStart = { data->MouseDown.xpos, data->MouseDown.ypos };
					}
				}
				else if (data->MouseDown.button == VK_RBUTTON && IsMouseOver(true))
				{
					if (_contextMenu)
					{
						_contextMenu->DeleteMe();
						_contextMenu = nullptr;
					}

					if (IsMouseOver(true))
					{
						Point p;
						p.x = data->MouseDown.xpos - GetAbsolutePosition().x;
						p.y = data->MouseDown.ypos - GetAbsolutePosition().y;

						// shift it slightly up and left so the cursor is not right on the edge pixel
						p.x -= 10;
						p.y -= 10;

						if (_hoveredAsset != nullptr)
						{
							_contextMenu = new ContextMenu(this, p);

							_contextMenu->AddItem(new ContextItem(L"Set Material", std::bind(&Explorer::SetMassMaterial, this)));
						}
						else
						{
							_contextMenu = new ContextMenu(this, p);

							_contextMenu->AddItem(new ContextItem(L"Select All", std::bind(&Explorer::SelectAll, this)));

							ContextItem* createNewItem = new ContextItem(L"Create new...", nullptr);

							_contextMenu->AddItem(createNewItem);
							auto newRoot = _contextMenu->CreateSubMenu(createNewItem);

							_contextMenu->AddItem(new ContextItem(L"Material", std::bind(&Explorer::CreateNewMaterial, this, _currentlyBrowsedFolder)), newRoot);
						}
					}
				}
			}
			else if (event == InputEvent::KeyDown)
			{
				if (data->KeyDown.key == VK_RETURN)
				{
					for (auto& asset : _assetsInView)
					{
						if (asset.selected)
						{
							LoadAsset(asset.path);
							asset.selected = false;
						}
					}
				}
			}
			else if (event == InputEvent::MouseUp)
			{
				if (data->KeyUp.key == VK_LBUTTON)
				{
					_dragStart = { -1, -1 };

					// drop the asset
					if (_draggingAsset != nullptr)
					{
						int32_t mx, my;
						g_pEnv->_inputSystem->GetMousePosition(mx, my);

						g_pEnv->_inputSystem->OnDragAndDropFiles({ _draggingAsset->path }, mx, my);

						_draggingAsset = nullptr;
					}
					else if(_hoveredAsset != nullptr)
					{
						g_pUIManager->GetInspector()->InspectResource(_hoveredAsset->path);
					}
				}
			}
			else if (event == InputEvent::MouseMove)
			{
				if (_hoveredAsset != nullptr && _hoveredAsset->dragging == true && _dragStart.x != -1 && _dragStart.y != -1 && (abs(_dragStart.x - data->MouseMove.x) >= 2 || abs(_dragStart.y - data->MouseMove.y) >= 2))
				{
					_draggingAsset = _hoveredAsset;
				}
				else if (_draggingAsset != nullptr)
				{
					if (g_pUIManager->GetSceneView()->IsMouseOver())
					{
						bool a = false;
					}
				}

				if (IsMouseOver(true))
				{
					_canvas.Redraw();
				}
			}
			else if (event == InputEvent::MouseWheel)
			{
				_scrollOffset += data->MouseWheel.delta * 20;

				if (_scrollOffset > 0)
					_scrollOffset = 0;

				_canvas.Redraw();
			}
		}
		else
		{
			if (event == InputEvent::MouseUp)
			{
				if (data->KeyUp.key == VK_LBUTTON)
				{
					_dragStart = { -1, -1 };

					// drop the asset
					if (_draggingAsset != nullptr)
					{
						int32_t mx, my;
						g_pEnv->_inputSystem->GetMousePosition(mx, my);

						std::wstring relative = (_currentlyBrowsedFS->GetName() + L".") + fs::relative(_draggingAsset->path, _currentlyBrowsedFS->GetDataDirectory()).wstring();


						g_pEnv->_inputSystem->OnDragAndDropFiles({ relative }, mx, my);

						_draggingAsset = nullptr;
					}
				}
			}
		}
		return false;
	}

	void Explorer::SetMassMaterial()
	{
		if (_contextMenu)
		{
			_contextMenu->DeleteMe();
			_contextMenu = nullptr;
		}

		//std::thread doit([this]() {
			for (auto& asset : _assetsInView)
			{
				if (asset.selected)
				{
					if (asset.path.extension() == ".hmesh")
					{
						auto mesh = Mesh::Create(asset.path);
						mesh->SetMaterial(Material::Create("GameData.Materials/Main.hmat"));
						mesh->Save();

						g_pEnv->_iconService->RemoveIcon(asset.path);

						SAFE_DELETE(asset.generatedIcon);
					}
				}
			}
			//});

		//doit.detach();
	}

	void Explorer::SelectAll()
	{
		if (_contextMenu)
		{
			_contextMenu->DeleteMe();
			_contextMenu = nullptr;
		}

		for (auto& asset : _assetsInView)
		{
			asset.selected = true;
		}
	}

	void Explorer::LoadAsset(const fs::path& path)
	{
		if (path.extension() == ".mtl")
			return;

		IResourceLoader* resourceLoader = g_pEnv->_resourceSystem->FindResourceLoaderForExtension(path.extension().string());

		if (!resourceLoader)
		{
			LOG_CRIT("Unable to load resource as we don't have a loader for extension '%s'", path.extension().string().c_str());
			return;
		}

		ResourceLoadOptions opts;
		opts.silenceErrors = true;		

		std::shared_ptr<IResource> resource = g_pEnv->_resourceSystem->LoadResource(path, &opts);

		if (resource && resourceLoader->GetResourceDirectory() == L"Meshes")
		{
			//Model* model = (Model*)resource;

			//if (model->GetNumMeshes() > 0)
			{
				Entity* entity = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(path.stem().string()/*, math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(0.1f)*/);

				auto meshRenderer = entity->AddComponent<StaticMeshComponent>();

				auto mesh = dynamic_pointer_cast<Mesh>(resource);

				meshRenderer->SetMesh(mesh);

				entity->GetComponent<Transform>()->SetScale(math::Vector3(10.0f));

				if (mesh->HasAnimations())
				{
					auto sac = entity->AddComponent<SkeletalAnimationComponent>();

					auto animatedMesh = dynamic_pointer_cast<AnimatedMesh>(mesh);

					sac->SetAnimationData(animatedMesh, animatedMesh->GetAnimationData());
				}

				//auto rigidBody = entity->AddComponent<RigidBody>();
				//rigidBody->AddTriangleMeshCollider((Mesh*)resource, true);
			}
		}
	}

	void Explorer::SetProjectPath(const fs::path& path)
	{
		_projectPath = path;

		UpdateFolderView();
	}

	void Explorer::RecurseList(ListNode* parent, const fs::path& path)
	{
		for (auto it = fs::directory_iterator(path); it != fs::directory_iterator(); it++)
		{
			auto p = *it;

			if (fs::is_directory(p))
			{
				ListNode* currentItem = new ListNode(_folderView, fs::relative(p, path), { g_pEnv->_uiManager->GetRenderer()->_style.img_folder_open.get(), g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed.get() });

				currentItem->_onClick = std::bind(&Explorer::OnClickFolder, this, _folderView, std::placeholders::_1, std::placeholders::_2);
				
				_folderView->AddNode(currentItem, parent);

				RecurseList(currentItem, p.path());
			}
		}
	};

	void Explorer::UpdateFolderView()
	{
		g_pEnv->_sceneManager->GetCurrentScene()->Lock();

		_canvas.Redraw();

		/*{
			auto rootPath = new ListNode(_folderView, L"Game Data", { g_pEnv->_uiManager->GetRenderer()->_style.img_folder_open, g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed }); 
			_folderView->AddNode(rootPath);

			ListNode* lastItem = rootPath;

			std::list<ListNode*> directoryList;
			directoryList.push_back(lastItem);

			int32_t lastDepth = 0;
			RecurseList(rootPath, g_pEditor->_projectFS->GetDataDirectory());
		}*/

		for (auto& fs : g_pEnv->_resourceSystem->GetFileSystems())
		{
			bool a = false;

			auto rootPath = new ListNode(
				_folderView,
				std::wstring(fs->GetName().begin(), fs->GetName().end()),
				{ g_pEnv->_uiManager->GetRenderer()->_style.img_folder_open.get(), g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed.get() },
				fs);
			_folderView->AddNode(rootPath);

			ListNode* lastItem = rootPath;

			std::list<ListNode*> directoryList;
			directoryList.push_back(lastItem);

			int32_t lastDepth = 0;
			RecurseList(rootPath, fs->GetDataDirectory());
		}

#if 0
		// add the items from standard assets
		for(auto& package : g_pEnv->_assetPackageManager->GetLoadedAssetPackages())
		{
			auto rootPath = new ListNode(_folderView, package->GetPath().filename().wstring(), { g_pEnv->_uiManager->GetRenderer()->_style.img_folder_open, g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed });
			_folderView->AddNode(rootPath);
			ListNode* lastItem = rootPath;

			std::list<ListNode*> directoryList;
			directoryList.push_back(lastItem);

			for (auto& asset : package->GetAssetMap())
			{
				auto path = asset.first;

				lastItem = rootPath;

				while (true)
				{
					size_t p = path.find('/');

					if (p == path.npos)
						break;

					auto parentDir = path.substr(0, p);

					if (path.find(L"Models") != std::string::npos)
					{
						bool a = false;
					}

					auto parentItem = _folderView->FindItemByLabelParented(parentDir, lastItem);

					if (parentItem == nullptr)
					{
						ListNode* currentItem = new ListNode(_folderView, parentDir, { g_pEnv->_uiManager->GetRenderer()->_style.img_folder_open, g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed });
						_folderView->AddNode(currentItem, lastItem);

						lastItem = currentItem;
					}
					else
						lastItem = parentItem;

					path.erase(0, p + 1);

					if (path.empty())
						break;
				}
			}

			

			//int32_t lastDepth = 0;
			//RecurseList(rootPath, g_pEditor->_projectFS->GetDataDirectory());
		}
#endif

		//for (auto it = fs::recursive_directory_iterator(g_pEditor->_projectFS->GetDataDirectory()); it != fs::recursive_directory_iterator(); it++)
		//{
		//	auto depth = it.depth();

		//	auto p = *it;

		//	if (fs::is_directory(p))
		//	{
		//		TreeList::Item* currentItem = _folderView->AddItem(lastItem, fs::relative(p, g_pEditor->_projectFS->GetDataDirectory()), icons);

		//		if (depth > lastDepth)
		//		{
		//			//lastItem = currentItem;
		//			directoryList.push_back(currentItem);
		//		}
		//		else if (depth < lastDepth)
		//		{
		//			lastItem = directoryList.back();
		//			directoryList.pop_back();
		//		}

		//		lastDepth = depth;
		//		lastItem = currentItem;
		//	}
		//}

		g_pEnv->_sceneManager->GetCurrentScene()->Unlock();
	}

	void Explorer::UpdateAssets(const fs::path& relativePath, FileSystem* fs)
	{
		_assetsPath = relativePath;

		_scrollOffset = 0;

		// remove the previous icons
		for (auto& asset : _assetsInView)
		{
			SAFE_DELETE(asset.icon);
		}
		_assetsInView.clear();

		_canvas.Redraw();

		// asset packages need to be handled separately
		//
		//std::wstring relativePathStr = relativePath.wstring();
		//size_t p = relativePathStr.find(L".pkg");
		//if (p != std::wstring::npos)
		//{
		//	auto packageName = relativePathStr.substr(0, p + 4);
		//	auto package = g_pEnv->_assetPackageManager->FindLoadAssetPackageByName(packageName);

		//	if (!package)
		//		return;

		//	auto packageFolder = p + 5 >= relativePathStr.length() ? L"" : relativePathStr.substr(p + 5);

		//	for (auto& asset : package->GetAssetMap())
		//	{
		//		auto assetParentFolders = asset.first;
		//		auto p = assetParentFolders.find_last_of('/');

		//		if (p != assetParentFolders.npos)
		//		{
		//			assetParentFolders = assetParentFolders.substr(0, p+1);
		//		}
		//		if (packageFolder.length() > 0 && packageFolder.find(assetParentFolders) == packageFolder.npos || packageFolder.length() == 0)
		//			continue;

		//		g_pEnv->_iconService->PushAssetPathForIconGeneration(packageName, asset.first);

		//		AssetDesc desc;
		//		desc.path = /*packageName + L"/" +*/ asset.first;
		//		desc.icon = nullptr;

		//		_assetsInView.push_back(desc);
		//	}

		//	return;
		//}

		for (auto& p : fs::directory_iterator(fs->GetLocalAbsoluteDataPath(relativePath)))
		{
			if (fs::is_regular_file(p))
			{
				//if (p.path().extension() != ".obj")
				//	continue;

				if (_searchFilter.length() > 0)
				{
					auto actualPath = p.path().wstring();
					std::transform(actualPath.begin(), actualPath.end(), actualPath.begin(), ::tolower);

					if (actualPath.find(_searchFilter) == std::wstring::npos)
						continue;
				}

				AssetDesc desc;
				desc.path = p;

				if (auto existingIcon = g_pEnv->_iconService->GetIcon(p); existingIcon != nullptr)
					desc.icon = existingIcon;
				else
				{
					g_pEnv->_iconService->PushFilePathForIconGeneration(p);

					SHFILEINFO stFileInfo;
					SHGetFileInfo(p.path().wstring().c_str(),
						FILE_ATTRIBUTE_NORMAL,
						&stFileInfo,
						sizeof(stFileInfo),
						SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_LARGEICON);

					ICONINFO stIconInfo;
					GetIconInfo(stFileInfo.hIcon, &stIconInfo);
					HBITMAP hBmp = stIconInfo.hbmColor;

					BITMAP BM;
					GetObject(hBmp, sizeof(BM), &BM);

					int32_t bpp = (BM.bmBitsPixel / 8);

					uint8_t* data = new uint8_t[BM.bmWidth * BM.bmHeight * bpp];

					GetBitmapBits(hBmp, BM.bmWidth * BM.bmHeight * bpp, data);

					if (bpp == 4) // ARGB
					{
						for (int i = 0; i < BM.bmHeight * BM.bmWidth; i++)
						{
							int iIdxBlue = i * 4 + 0;
							int iIdxRed = i * 4 + 2;
							char iBlue = data[iIdxBlue];
							char iRed = data[iIdxRed];
							data[iIdxRed] = iBlue;
							data[iIdxBlue] = iRed;
						}
					}

					D3D11_SUBRESOURCE_DATA pixelData;
					pixelData.pSysMem = data;
					pixelData.SysMemPitch = BM.bmWidth * bpp;
					pixelData.SysMemSlicePitch = 0;

					desc.icon = g_pEnv->_graphicsDevice->CreateTexture2D(BM.bmWidth, BM.bmHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
						1, D3D11_BIND_SHADER_RESOURCE,
						0, 1, 0,
						&pixelData,
						(D3D11_CPU_ACCESS_FLAG)0,
						D3D11_RTV_DIMENSION_UNKNOWN,
						D3D11_UAV_DIMENSION_UNKNOWN,
						D3D11_SRV_DIMENSION_TEXTURE2D);

					delete[] data;

					DestroyIcon(stFileInfo.hIcon);
				}
				_assetsInView.push_back(desc);
			}
		}
	}

	void Explorer::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		renderer->PushDrawList();

		Dock::Render(renderer, w, h);		
	}

	void Explorer::PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		renderer->ListDraw(renderer->GetDrawList());
		renderer->PopDrawList();

		renderer->PushDrawList();

		if (_tab->GetCurrentTabIndex() == 0)
		{
			RenderAssetExplorer(renderer, w, h);
		}
		else if (_tab->GetCurrentTabIndex() == 1)
		{
			// render log
		}

		renderer->ListDraw(renderer->GetDrawList());
		renderer->PopDrawList();
	}

	void Explorer::RenderAssetExplorer(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const int32_t IconSize = 80;

		if (_canvas.BeginDraw(renderer, w, h))
		{
			if (_assetsInView.size() > 0)
			{
				//_drawList.Clear();

				int32_t x = _position.x + _size.y + 20;
				int32_t y = _position.y + 50;

				_hoveredAsset = nullptr;

				std::wstring fullNameToDraw;
				int32_t hoverX, hoverY;

				RECT scissor;
				scissor.left = x;
				scissor.top = y;
				scissor.right = _size.x;
				scissor.bottom = y + _size.y;
				g_pEnv->_graphicsDevice->SetScissorRect(scissor);

				y += _scrollOffset;


				for (auto& asset : _assetsInView)
				{
					bool hovering = false;
					bool drawFullName = false;

					if (y >= _position.y + 30 - IconSize && y <= _position.y + _size.y)
					{
						if (!_draggingAsset && (IsMouseOver(x, y, IconSize, IconSize) || asset.selected))
						{
							hovering = true;
							renderer->FillQuad(x - 3, y - 3, IconSize + 6, IconSize + 20, math::Color(HEX_RGBA_TO_FLOAT4(23, 23, 23, 255)));
							_hoveredAsset = &asset;

							if (!asset.selected)
							{
								if (_hoveredAsset != _lastHoveredAsset)
								{
									_hoverStartTime = g_pEnv->_timeManager->_currentTime;
									_lastHoveredAsset = _hoveredAsset;
								}

								if (_hoverStartTime != 0.0f && g_pEnv->_timeManager->_currentTime - _hoverStartTime > 0.1f)
								{
									drawFullName = true;
								}
							}
						}

						if (!asset.generatedIcon)
						{
							if (auto generatedIcon = g_pEnv->_iconService->GetIcon(asset.path); generatedIcon != nullptr)
							{
								//renderer->PushFillTexturedQuad(&_drawList, generatedIcon, x, y, IconSize, IconSize, math::Color(1, 1, 1, 1));
								asset.generatedIcon = generatedIcon;
							}
						}

						if (asset.icon || asset.generatedIcon)
						{
							renderer->FillTexturedQuad(asset.generatedIcon ? asset.generatedIcon : asset.icon, x, y, IconSize, IconSize, math::Color(1, 1, 1, 1));
						}



						// shorten the label to fit
						if (asset.assetNameFull.length() == 0)
							asset.assetNameFull = asset.path.filename().wstring();

						std::wstring assetNameToDisplay = asset.assetNameFull;

						if (_assetNameToEdit && _assetNameToEdit->path == asset.path)
						{
							drawFullName = true;
							assetNameToDisplay = _editingAssetTempName + _editingAssetExtension;
						}

						if (!drawFullName)
						{
							if (asset.assetNameShort.length() == 0)
							{
								while (true)
								{
									int32_t width, height;
									renderer->_style.font->MeasureText((int32_t)Style::FontSize::Titchy, assetNameToDisplay, width, height);

									if (width >= IconSize)
										assetNameToDisplay.pop_back();
									else
										break;
								}

								asset.assetNameShort = assetNameToDisplay;
							}

							assetNameToDisplay = asset.assetNameShort;

							renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Titchy, x + IconSize / 2, y + IconSize + 2, hovering ? renderer->_style.text_highlight : renderer->_style.text_regular, FontAlign::CentreLR, assetNameToDisplay);
						}
						else
						{
							hoverX = x + IconSize / 2;
							hoverY = y + IconSize + 2;
							fullNameToDraw = assetNameToDisplay;
						}
					}

					x += IconSize + 20;

					if (x + IconSize >= _size.x)
					{
						x = _position.x + _size.y + 20;
						y += IconSize + 30;
					}
				}

				//g_pEnv->_uiManager->GetRenderer()->ListDraw(&_drawList);
				g_pEnv->_graphicsDevice->ClearScissorRect();

				if (fullNameToDraw.length() > 0)
				{
					int32_t width, height;
					renderer->_style.font->MeasureText((int32_t)Style::FontSize::Titchy, fullNameToDraw, width, height);

					renderer->FillQuad(hoverX - ((width / 2) + 3), hoverY - 1, width + 6, height + 4, math::Color(0.1f, 0.1f, 0.1f, 1.0f));
					renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Titchy, hoverX, hoverY, renderer->_style.text_highlight, FontAlign::CentreLR, fullNameToDraw);
				}			
			}		

			_canvas.EndDraw(renderer);
		}

		_canvas.Present(renderer, 0, 0, w, h);

		if (_hoveredAsset == nullptr)
		{
			_lastHoveredAsset = nullptr;
			_hoverStartTime = 0.0f;
		}
		else
		{
			if (_hoveredAsset->icon || _hoveredAsset->generatedIcon)
			{
				renderer->FillTexturedQuad(_hoveredAsset->generatedIcon ? _hoveredAsset->generatedIcon : _hoveredAsset->icon, g_pUIManager->GetSceneView()->GetAbsolutePosition().x, _position.y - 256, 256, 256, math::Color(1, 1, 1, 1));
			}
		}

		if (_draggingAsset != nullptr)
		{
			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);

			renderer->FillTexturedQuad(_draggingAsset->icon, mx - IconSize / 2, my - IconSize / 2, IconSize, IconSize, math::Color(1.0f, 1.0f, 1.0f, 0.5f));
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Titchy, mx + IconSize / 2, my + IconSize + 2, renderer->_style.text_highlight, FontAlign::CentreLR, _draggingAsset->path.filename().wstring());
		}

		//g_pEnv->_uiManager->GetRenderer()->ListDraw(&_drawList);
	}
}