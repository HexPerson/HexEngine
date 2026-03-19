#include "AssetExplorer.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace HexEditor
{
	namespace
	{
		constexpr int32_t IconSize = 120;
		constexpr int32_t IconSpacing = 20;
		constexpr int32_t IconPadding = 10;
		constexpr int32_t IconRowStep = IconSize + 30;

		bool IsWithin(const HexEngine::Point& absolutePos, const HexEngine::Point& size, int32_t x, int32_t y)
		{
			return x >= absolutePos.x && x < absolutePos.x + size.x &&
				y >= absolutePos.y && y < absolutePos.y + size.y;
		}

		RECT NormalizeRect(const HexEngine::Point& a, const HexEngine::Point& b)
		{
			RECT r = {};
			r.left = min(a.x, b.x);
			r.top = min(a.y, b.y);
			r.right = max(a.x, b.x);
			r.bottom = max(a.y, b.y);
			return r;
		}

		bool PointInRect(const RECT& r, int32_t x, int32_t y)
		{
			return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
		}

		bool Intersects(const RECT& a, const RECT& b)
		{
			return a.left < b.right && a.right > b.left &&
				a.top < b.bottom && a.bottom > b.top;
		}

		
	}

	AssetExplorer::AssetExplorer(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		ScrollView(parent, position, size)
	{
		SetScrollSpeed(20.0f);
		SetManualContentHeight(size.y);
	}

	AssetExplorer::~AssetExplorer()
	{
		CloseContextMenu();
		ClearAssetIcons();
	}

	AssetExplorer::AssetDesc* AssetExplorer::GetCurrentlyDraggedAsset() const
	{
		return _draggingAsset;
	}

	void AssetExplorer::SetSearchFilter(const std::wstring& text)
	{
		_searchFilter = text;
		std::transform(_searchFilter.begin(), _searchFilter.end(), _searchFilter.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });

		if (_currentlyBrowsedFS != nullptr)
		{
			UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);
		}
	}

	void AssetExplorer::CloseContextMenu()
	{
		if (_contextMenu)
		{
			_contextMenu->DeleteMe();
			_contextMenu = nullptr;
		}
	}

	void AssetExplorer::ClearAssetIcons()
	{
		for (auto& asset : _assetsInView)
		{
			if (asset.ownsIcon && asset.icon != nullptr)
			{
				SAFE_DELETE(asset.icon);
			}
		}
	}

	int32_t AssetExplorer::ComputeRequiredContentHeight() const
	{
		const int32_t innerWidth = max(1, _size.x - IconPadding * 2);
		const int32_t itemsPerRow = max(1, (innerWidth + IconSpacing) / (IconSize + IconSpacing));
		const int32_t rows = (int32_t)((_assetsInView.size() + itemsPerRow - 1) / itemsPerRow);
		return max(_size.y, IconPadding * 2 + rows * IconRowStep);
	}

	void AssetExplorer::ClearSelection()
	{
		for (auto& asset : _assetsInView)
		{
			asset.selected = false;
		}

		_canvas.Redraw();
	}

	AssetExplorer::AssetDesc* AssetExplorer::FindAssetAtPosition(int32_t x, int32_t y)
	{
		const auto pos = GetAbsolutePosition();
		const int32_t startX = pos.x + IconPadding;
		int32_t drawX = startX;
		int32_t drawY = pos.y + IconPadding - (int32_t)std::round(GetScrollOffset());

		for (auto& asset : _assetsInView)
		{
			RECT assetRect = { drawX, drawY, drawX + IconSize, drawY + IconSize };
			if (PointInRect(assetRect, x, y))
			{
				return &asset;
			}

			drawX += IconSize + IconSpacing;
			if (drawX + IconSize >= pos.x + _size.x - IconPadding)
			{
				drawX = startX;
				drawY += IconRowStep;
			}
		}

		return nullptr;
	}

	void AssetExplorer::ApplyMarqueeSelection()
	{
		const RECT marquee = NormalizeRect(_marqueeStart, _marqueeEnd);
		const auto pos = GetAbsolutePosition();
		const int32_t startX = pos.x + IconPadding;
		int32_t drawX = startX;
		int32_t drawY = pos.y + IconPadding - (int32_t)std::round(GetScrollOffset());

		for (auto& asset : _assetsInView)
		{
			const RECT assetRect = { drawX, drawY, drawX + IconSize, drawY + IconSize };
			asset.selected = Intersects(marquee, assetRect);

			drawX += IconSize + IconSpacing;
			if (drawX + IconSize >= pos.x + _size.x - IconPadding)
			{
				drawX = startX;
				drawY += IconRowStep;
			}
		}

		_canvas.Redraw();
	}

	void AssetExplorer::UpdateAssets(const fs::path& relativePath, HexEngine::FileSystem* fs)
	{
		_currentlyBrowsedFS = fs;
		_currentlyBrowsedFolder = relativePath;
		_hoveredAsset = nullptr;
		_lastHoveredAsset = nullptr;
		_draggingAsset = nullptr;
		_assetNameToEdit = nullptr;
		_editingAssetTempName.clear();
		_editingAssetExtension.clear();
		_isMarqueeSelecting = false;
		_marqueeStart = { -1, -1 };
		_marqueeEnd = { -1, -1 };
		SetScrollOffset(0.0f);

		ClearAssetIcons();
		_assetsInView.clear();

		if (_currentlyBrowsedFS == nullptr)
		{
			SetManualContentHeight(_size.y);
			return;
		}

		const fs::path absolutePath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(relativePath);
		if (!fs::exists(absolutePath))
		{
			SetManualContentHeight(_size.y);
			return;
		}

		for (auto& p : fs::directory_iterator(absolutePath))
		{
			if (!fs::is_regular_file(p))
				continue;

			if (!_searchFilter.empty())
			{
				auto actualPath = p.path().wstring();
				std::transform(actualPath.begin(), actualPath.end(), actualPath.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });

				if (actualPath.find(_searchFilter) == std::wstring::npos)
					continue;
			}

			AssetDesc desc;
			desc.path = p.path();
			desc.generatedIcon = HexEngine::g_pEnv->_iconService->GetIcon(desc.path);

			if (desc.generatedIcon == nullptr)
			{
				HexEngine::g_pEnv->_iconService->PushFilePathForIconGeneration(desc.path);

				SHFILEINFO stFileInfo = {};
				SHGetFileInfo(
					desc.path.wstring().c_str(),
					FILE_ATTRIBUTE_NORMAL,
					&stFileInfo,
					sizeof(stFileInfo),
					SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_LARGEICON);

				ICONINFO stIconInfo = {};
				if (stFileInfo.hIcon != nullptr && GetIconInfo(stFileInfo.hIcon, &stIconInfo))
				{
					BITMAP bm = {};
					GetObject(stIconInfo.hbmColor, sizeof(bm), &bm);

					const int32_t bpp = (bm.bmBitsPixel / 8);
					if (bpp >= 4)
					{
						auto* data = new uint8_t[bm.bmWidth * bm.bmHeight * bpp];
						GetBitmapBits(stIconInfo.hbmColor, bm.bmWidth * bm.bmHeight * bpp, data);

						for (int32_t i = 0; i < bm.bmHeight * bm.bmWidth; ++i)
						{
							const int32_t idxBlue = i * bpp;
							const int32_t idxRed = i * bpp + 2;
							std::swap(data[idxBlue], data[idxRed]);
						}

						D3D11_SUBRESOURCE_DATA pixelData = {};
						pixelData.pSysMem = data;
						pixelData.SysMemPitch = bm.bmWidth * bpp;

						desc.icon = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
							bm.bmWidth,
							bm.bmHeight,
							DXGI_FORMAT_R8G8B8A8_UNORM,
							1,
							D3D11_BIND_SHADER_RESOURCE,
							0,
							1,
							0,
							&pixelData,
							(D3D11_CPU_ACCESS_FLAG)0,
							D3D11_RTV_DIMENSION_UNKNOWN,
							D3D11_UAV_DIMENSION_UNKNOWN,
							D3D11_SRV_DIMENSION_TEXTURE2D);

						desc.ownsIcon = desc.icon != nullptr;
						delete[] data;
					}

					if (stIconInfo.hbmColor)
						DeleteObject(stIconInfo.hbmColor);
					if (stIconInfo.hbmMask)
						DeleteObject(stIconInfo.hbmMask);
				}

				if (stFileInfo.hIcon)
					DestroyIcon(stFileInfo.hIcon);
			}

			_assetsInView.push_back(desc);
		}

		SetManualContentHeight(ComputeRequiredContentHeight());
	}

	AssetExplorer::AssetDesc* AssetExplorer::FindAssetInView(const fs::path& filename)
	{
		for (auto& desc : _assetsInView)
		{
			if (desc.path == filename)
				return &desc;
		}

		return nullptr;
	}

	void AssetExplorer::EditAssetName(AssetDesc* asset)
	{
		if (asset == nullptr)
			return;

		_assetNameToEdit = asset;
		_editingAssetTempName = asset->path.stem().wstring();
		_editingAssetExtension = asset->path.extension().wstring();
	}

	void AssetExplorer::CreateNewMaterial(const fs::path& baseDir)
	{
		if (_currentlyBrowsedFS == nullptr)
			return;

		const fs::path newMaterialPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(baseDir / L"NewMaterial.hmat");

		auto* material = new HexEngine::Material;
		material->SetPaths(newMaterialPath, _currentlyBrowsedFS);
		material->CopyFrom(HexEngine::Material::GetDefaultMaterial());
		material->SetLoader(HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));
		material->Save();

		UpdateAssets(baseDir, _currentlyBrowsedFS);

		if (auto* assetToEdit = FindAssetInView(newMaterialPath); assetToEdit != nullptr)
		{
			EditAssetName(assetToEdit);
		}
	}

	void AssetExplorer::ImportAllMeshes()
	{
		if (_currentlyBrowsedFS == nullptr)
			return;

		const auto folderPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder);
		for (auto it = fs::directory_iterator(folderPath); it != fs::directory_iterator(); ++it)
		{
			auto p = *it;
			if (!fs::is_directory(p) && p.path().extension() == ".hmesh")
			{
				LoadAsset(p.path());
			}
		}
	}

	void AssetExplorer::ImportAllForeignMeshes()
	{
		std::map<fs::path, std::vector<fs::path>> pathsToLoad;
		HexEngine::IResourceLoader* resourceLoader = nullptr;

		for (auto& asset : _assetsInView)
		{
			if (!asset.selected /*|| asset.path.extension() != ".hmesh"*/)
				continue;

			const auto extension = asset.path.extension();

			resourceLoader = HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(extension.string());
			if (resourceLoader)
			{
				std::wstring relative = (_currentlyBrowsedFS->GetName() + L".") + fs::relative(asset.path, _currentlyBrowsedFS->GetDataDirectory()).wstring();

				pathsToLoad[extension].push_back(relative);			
			}
		}

		for (auto& path : pathsToLoad)
		{
			auto extension = path.first;
			auto relatives = path.second;

			if (resourceLoader->CreateEditorDialog(relatives) == nullptr)
			{
				//HexEngine::g_pEnv->GetResourceSystem().LoadResource(asset.path);
			}
		}
	}

	void AssetExplorer::SetMassMaterial()
	{
		CloseContextMenu();

		for (auto& asset : _assetsInView)
		{
			if (!asset.selected || asset.path.extension() != ".hmesh")
				continue;

			auto mesh = HexEngine::Mesh::Create(asset.path);
			mesh->SetMaterial(HexEngine::Material::Create("GameData.Materials/Main.hmat"));
			mesh->Save();

			HexEngine::g_pEnv->_iconService->RemoveIcon(asset.path);
			asset.generatedIcon = nullptr;
		}
	}

	void AssetExplorer::SelectAll()
	{
		CloseContextMenu();

		for (auto& asset : _assetsInView)
		{
			asset.selected = true;
		}
	}

	void AssetExplorer::LoadAsset(const fs::path& path)
	{
		if (path.extension() == ".mtl")
			return;

		HexEngine::IResourceLoader* resourceLoader = HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(path.extension().string());
		if (!resourceLoader)
		{
			LOG_CRIT("Unable to load resource as we don't have a loader for extension '%s'", path.extension().string().c_str());
			return;
		}

		HexEngine::ResourceLoadOptions opts;
		opts.silenceErrors = true;

		std::shared_ptr<HexEngine::IResource> resource = HexEngine::g_pEnv->GetResourceSystem().LoadResource(path, &opts);
		if (resource && resourceLoader->GetResourceDirectory() == L"Meshes")
		{
			HexEngine::Entity* entity = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(path.stem().string());
			auto meshRenderer = entity->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = dynamic_pointer_cast<HexEngine::Mesh>(resource);

			meshRenderer->SetMesh(mesh);
			entity->GetComponent<HexEngine::Transform>()->SetScale(math::Vector3(1.0f));

			if (mesh->HasAnimations())
			{
				auto sac = entity->AddComponent<HexEngine::SkeletalAnimationComponent>();
				auto animatedMesh = dynamic_pointer_cast<HexEngine::AnimatedMesh>(mesh);
				sac->SetAnimationData(animatedMesh, animatedMesh->GetAnimationData());
			}
		}
	}

	bool AssetExplorer::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (_assetNameToEdit && event == HexEngine::InputEvent::Char)
		{
			if (data->Char.ch == VK_BACK && !_editingAssetTempName.empty())
			{
				_editingAssetTempName.pop_back();
			}
			else if (data->Char.ch == VK_RETURN)
			{
				if (_currentlyBrowsedFS && _assetNameToEdit)
				{
					fs::path newName = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / (_editingAssetTempName + _editingAssetExtension));
					std::error_code ec;
					fs::rename(_assetNameToEdit->path, newName, ec);
					if (!ec)
					{
						_assetNameToEdit->assetNameFull.clear();
						_assetNameToEdit->assetNameShort.clear();
						_assetNameToEdit->path = newName;
					}
				}

				_assetNameToEdit = nullptr;
				_editingAssetTempName.clear();
				_editingAssetExtension.clear();
			}
			else if (data->Char.ch >= 32)
			{
				_editingAssetTempName.push_back(data->Char.ch);
			}

			return true;
		}

		if (ScrollView::OnInputEvent(event, data))
		{
			return true;
		}

		const auto absolutePos = GetAbsolutePosition();
		const bool mouseOverExplorer = IsMouseOver(true);

		if (event == HexEngine::InputEvent::DragAndDrop && mouseOverExplorer)
		{
			if (IsWithin(absolutePos, _size, data->DragAndDrop.x, data->DragAndDrop.y))
			{
				if (_currentlyBrowsedFS == nullptr)
					return true;

				const fs::path fileName = data->DragAndDrop.path;
				if (fileName.wstring().find(L".pkg") != std::wstring::npos)
				{
					LOG_DEBUG("Skipping drag and drop request for file '%S' because it belongs to a package file", fileName.wstring().c_str());
					return true;
				}

				HexEngine::IResourceLoader* resourceLoader = HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(fileName.extension().string());
				if (!resourceLoader)
				{
					LOG_CRIT("Unable to load resource as we don't have a loader for extension '%s'", fileName.extension().string().c_str());
					return true;
				}

				std::wstring relative = _currentlyBrowsedFS->GetRelativeResourcePath(fs::relative(fileName, _currentlyBrowsedFS->GetDataDirectory()));
				LoadAsset(relative);
				return true;
			}
		}
		else if (event == HexEngine::InputEvent::MouseDoubleClick && data->MouseDown.button == VK_LBUTTON)
		{
			if (_hoveredAsset != nullptr && mouseOverExplorer)
			{
				HexEngine::IResourceLoader* resourceLoader = HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(_hoveredAsset->path.extension().string());
				if (resourceLoader)
				{
					std::wstring relative = (_currentlyBrowsedFS->GetName() + L".") + fs::relative(_hoveredAsset->path, _currentlyBrowsedFS->GetDataDirectory()).wstring();
					if (resourceLoader->CreateEditorDialog({ relative }) == nullptr)
					{
						HexEngine::g_pEnv->GetResourceSystem().LoadResource(_hoveredAsset->path);
					}
				}

				_hoveredAsset = nullptr;
				return true;
			}
		}
		else if (event == HexEngine::InputEvent::MouseDown)
		{
			if (data->MouseDown.button == VK_LBUTTON)
			{
				if (mouseOverExplorer)
				{
					CloseContextMenu();

					AssetDesc* clickedAsset = FindAssetAtPosition(data->MouseDown.xpos, data->MouseDown.ypos);
					if (clickedAsset != nullptr)
					{
						if (HexEngine::g_pEnv->GetInputSystem().IsCtrlDown())
						{
							clickedAsset->selected = !clickedAsset->selected;
							_dragStart = { -1, -1 };
							clickedAsset->dragging = false;
							_canvas.Redraw();
							return true;
						}

						if (!clickedAsset->selected)
						{
							ClearSelection();
							clickedAsset->selected = true;
						}

						clickedAsset->dragging = true;
						_dragStart = { data->MouseDown.xpos, data->MouseDown.ypos };
					}
					else
					{
						ClearSelection();
						_isMarqueeSelecting = true;
						_marqueeStart = { data->MouseDown.xpos, data->MouseDown.ypos };
						_marqueeEnd = _marqueeStart;
						_dragStart = { -1, -1 };
					}

					return true;
				}
			}
			else if (data->MouseDown.button == VK_RBUTTON && mouseOverExplorer)
			{
				CloseContextMenu();

				AssetDesc* clickedAsset = FindAssetAtPosition(data->MouseDown.xpos, data->MouseDown.ypos);
				if (clickedAsset && !clickedAsset->selected)
				{
					ClearSelection();
					clickedAsset->selected = true;
				}

				int32_t numSelection = 0;
				bool doesHaveAtLeastOneMeshSelected = false;
				for (const auto& asset : _assetsInView)
				{
					if (asset.selected)
					{
						numSelection++;
					}
				}

				auto* menuParent = GetParent() ? GetParent() : this;
				HexEngine::Point p;
				p.x = data->MouseDown.xpos - menuParent->GetAbsolutePosition().x - 10;
				p.y = data->MouseDown.ypos - menuParent->GetAbsolutePosition().y - 10;
				_contextMenu = new HexEngine::ContextMenu(menuParent, p);

				if (numSelection >= 1)
				{
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Set material", std::bind(&AssetExplorer::SetMassMaterial, this)));
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Import", std::bind(&AssetExplorer::ImportAllForeignMeshes, this)));
				}
				else
				{
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Select all", std::bind(&AssetExplorer::SelectAll, this)));
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Import all meshes", std::bind(&AssetExplorer::ImportAllMeshes, this)));

					auto* createNewItem = new HexEngine::ContextItem(L"Create new...", nullptr);
					_contextMenu->AddItem(createNewItem);
					auto* newRoot = _contextMenu->CreateSubMenu(createNewItem);
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Material", std::bind(&AssetExplorer::CreateNewMaterial, this, _currentlyBrowsedFolder)), newRoot);
				}

				return true;
			}
		}
		else if (event == HexEngine::InputEvent::KeyDown && data->KeyDown.key == VK_RETURN)
		{
			for (auto& asset : _assetsInView)
			{
				if (asset.selected)
				{
					LoadAsset(asset.path);
					asset.selected = false;
				}
			}
			return true;
		}
		else if (event == HexEngine::InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON)
		{
			_dragStart = { -1, -1 };

			if (_isMarqueeSelecting)
			{
				_marqueeEnd = { data->MouseUp.xpos, data->MouseUp.ypos };
				ApplyMarqueeSelection();
				_isMarqueeSelecting = false;
				return true;
			}

			for (auto& asset : _assetsInView)
			{
				asset.dragging = false;
			}

			if (_draggingAsset != nullptr)
			{
				int32_t mx, my;
				HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

				if (mouseOverExplorer)
				{
					HexEngine::g_pEnv->_inputSystem->OnDragAndDropFiles({ _draggingAsset->path }, mx, my);
				}
				else if (_currentlyBrowsedFS)
				{
					const std::wstring relative = (_currentlyBrowsedFS->GetName() + L".") + fs::relative(_draggingAsset->path, _currentlyBrowsedFS->GetDataDirectory()).wstring();
					HexEngine::g_pEnv->_inputSystem->OnDragAndDropFiles({ relative }, mx, my);
				}

				_draggingAsset = nullptr;
				return true;
			}

			if (_hoveredAsset != nullptr && mouseOverExplorer)
			{
				g_pUIManager->GetInspector()->InspectResource(_hoveredAsset->path);
				return true;
			}
		}
		else if (event == HexEngine::InputEvent::MouseMove)
		{
			if (_isMarqueeSelecting)
			{
				_marqueeEnd = { (int32_t)data->MouseMove.x, (int32_t)data->MouseMove.y };
				ApplyMarqueeSelection();
				return true;
			}

			if (_hoveredAsset != nullptr &&
				_hoveredAsset->dragging &&
				_dragStart.x != -1 &&
				_dragStart.y != -1 &&
				(abs(_dragStart.x - (int32_t)data->MouseMove.x) >= 2 || abs(_dragStart.y - (int32_t)data->MouseMove.y) >= 2))
			{
				_draggingAsset = _hoveredAsset;
				return true;
			}
		}

		return false;
	}

	void AssetExplorer::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		ScrollView::Render(renderer, w, h);

		(void)w;
		(void)h;

		const auto pos = GetAbsolutePosition();

		SetManualContentHeight(ComputeRequiredContentHeight());

		_hoveredAsset = nullptr;

		std::wstring fullNameToDraw;
		int32_t hoverX = 0;
		int32_t hoverY = 0;
		const int32_t maxY = pos.y + _size.y;
		const int32_t startX = pos.x + IconPadding;
		int32_t x = startX;
		int32_t y = pos.y + IconPadding - (int32_t)std::round(GetScrollOffset());

		for (auto& asset : _assetsInView)
		{
			const bool visible = y + IconRowStep > pos.y && y < maxY;
			bool hovering = false;
			bool drawFullName = false;

			if (visible)
			{
				const bool isMouseHover = !_draggingAsset && IsMouseOver(x, y, IconSize, IconSize);
				if (isMouseHover || asset.selected)
				{
					hovering = true;
					renderer->FillQuad(x - 3, y - 3, IconSize + 6, IconSize + 20, math::Color(HEX_RGBA_TO_FLOAT4(23, 23, 23, 255)));

					if (isMouseHover)
					{
						_hoveredAsset = &asset;
					}

					if (isMouseHover && !asset.selected)
					{
						if (_hoveredAsset != _lastHoveredAsset)
						{
							_hoverStartTime = HexEngine::g_pEnv->_timeManager->_currentTime;
							_lastHoveredAsset = _hoveredAsset;
						}

						if (_hoverStartTime != 0.0f && HexEngine::g_pEnv->_timeManager->_currentTime - _hoverStartTime > 0.1f)
						{
							drawFullName = true;
						}
					}
				}

				if (asset.generatedIcon == nullptr)
				{
					asset.generatedIcon = HexEngine::g_pEnv->_iconService->GetIcon(asset.path);
				}

				if (asset.icon || asset.generatedIcon)
				{
					renderer->FillTexturedQuad(asset.generatedIcon ? asset.generatedIcon : asset.icon, x, y, IconSize, IconSize, math::Color(1, 1, 1, 1));
				}

				if (asset.assetNameFull.empty())
				{
					asset.assetNameFull = asset.path.filename().wstring();
				}

				std::wstring assetNameToDisplay = asset.assetNameFull;
				if (_assetNameToEdit && _assetNameToEdit->path == asset.path)
				{
					drawFullName = true;
					assetNameToDisplay = _editingAssetTempName + _editingAssetExtension;
				}

				if (!drawFullName)
				{
					if (asset.assetNameShort.empty())
					{
						while (!assetNameToDisplay.empty())
						{
							int32_t width = 0;
							int32_t height = 0;
							renderer->_style.font->MeasureText((int32_t)HexEngine::Style::FontSize::Titchy, assetNameToDisplay, width, height);

							if (width < IconSize)
								break;

							assetNameToDisplay.pop_back();
						}

						asset.assetNameShort = assetNameToDisplay;
					}

					assetNameToDisplay = asset.assetNameShort;
					renderer->PrintText(
						renderer->_style.font.get(),
						(uint8_t)HexEngine::Style::FontSize::Titchy,
						x + IconSize / 2,
						y + IconSize + 2,
						hovering ? renderer->_style.text_highlight : renderer->_style.text_regular,
						HexEngine::FontAlign::CentreLR,
						assetNameToDisplay);
				}
				else
				{
					hoverX = x + IconSize / 2;
					hoverY = y + IconSize + 2;
					fullNameToDraw = assetNameToDisplay;
				}
			}

			x += IconSize + IconSpacing;
			if (x + IconSize >= pos.x + _size.x - IconPadding)
			{
				x = startX;
				y += IconRowStep;
			}
		}

		if (!fullNameToDraw.empty())
		{
			int32_t width = 0;
			int32_t height = 0;
			renderer->_style.font->MeasureText((int32_t)HexEngine::Style::FontSize::Titchy, fullNameToDraw, width, height);

			renderer->FillQuad(hoverX - ((width / 2) + 3), hoverY - 1, width + 6, height + 4, math::Color(0.1f, 0.1f, 0.1f, 1.0f));
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)HexEngine::Style::FontSize::Titchy, hoverX, hoverY, renderer->_style.text_highlight, HexEngine::FontAlign::CentreLR, fullNameToDraw);
		}

		if (_isMarqueeSelecting)
		{
			const RECT marquee = NormalizeRect(_marqueeStart, _marqueeEnd);
			const int32_t width = max(0L, marquee.right - marquee.left);
			const int32_t height = max(0L, marquee.bottom - marquee.top);

			renderer->FillQuad(marquee.left, marquee.top, width, height, math::Color(0.2f, 0.4f, 1.0f, 0.2f));
			renderer->Frame(marquee.left, marquee.top, width, height, 1, math::Color(0.2f, 0.6f, 1.0f, 0.9f));
		}

		if (_hoveredAsset == nullptr)
		{
			_lastHoveredAsset = nullptr;
			_hoverStartTime = 0.0f;
		}
	}

	void AssetExplorer::PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		ScrollView::PostRenderChildren(renderer, w, h);

		if (_hoveredAsset != nullptr)
		{
			if (_hoveredAsset->icon || _hoveredAsset->generatedIcon)
			{
				renderer->FillTexturedQuad(
					_hoveredAsset->generatedIcon ? _hoveredAsset->generatedIcon : _hoveredAsset->icon,
					g_pUIManager->GetSceneView()->GetAbsolutePosition().x,
					GetAbsolutePosition().y - 256,
					256,
					256,
					math::Color(1, 1, 1, 1));
			}
		}

		if (_draggingAsset != nullptr)
		{
			int32_t mx = 0;
			int32_t my = 0;
			HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

			auto* icon = _draggingAsset->generatedIcon ? _draggingAsset->generatedIcon : _draggingAsset->icon;
			if (icon != nullptr)
			{
				renderer->FillTexturedQuad(icon, mx - IconSize / 2, my - IconSize / 2, IconSize, IconSize, math::Color(1.0f, 1.0f, 1.0f, 0.5f));
			}

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)HexEngine::Style::FontSize::Titchy,
				mx + IconSize / 2,
				my + IconSize + 2,
				renderer->_style.text_highlight,
				HexEngine::FontAlign::CentreLR,
				_draggingAsset->path.filename().wstring());
		}
	}
}
