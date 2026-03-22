#include "AssetExplorer.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>

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
			r.left = std::min(a.x, b.x);
			r.top = std::min(a.y, b.y);
			r.right = std::max(a.x, b.x);
			r.bottom = std::max(a.y, b.y);
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

		std::wstring SanitizeFileName(const std::wstring& value)
		{
			std::wstring result;
			result.reserve(value.size());

			for (const wchar_t ch : value)
			{
				if (iswalnum(ch))
				{
					result.push_back(ch);
				}
				else if (ch == L'_' || ch == L'-')
				{
					result.push_back(ch);
				}
			}

			if (result.empty())
			{
				result = L"Material";
			}

			return result;
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

	bool AssetExplorer::ConsumeRecentlyDroppedAssetPath(fs::path& outPath)
	{
		if (!_hasRecentlyDroppedAsset)
			return false;

		outPath = _recentlyDroppedAssetPath;
		_recentlyDroppedAssetPath.clear();
		_hasRecentlyDroppedAsset = false;
		return true;
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
		const int32_t innerWidth = std::max(1, _size.x - IconPadding * 2);
		const int32_t itemsPerRow = std::max(1, (innerWidth + IconSpacing) / (IconSize + IconSpacing));
		const int32_t rows = (int32_t)((_assetsInView.size() + itemsPerRow - 1) / itemsPerRow);
		return std::max(_size.y, IconPadding * 2 + rows * IconRowStep);
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
			if (!asset.selected || asset.path.extension() == ".hmesh")
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

	void AssetExplorer::ShowCreatePrefabVariantDialog()
	{
		CloseContextMenu();

		if (_currentlyBrowsedFS == nullptr)
			return;

		fs::path basePrefabPath;
		for (const auto& asset : _assetsInView)
		{
			if (!asset.selected || asset.path.extension() != ".hprefab")
				continue;

			if (!basePrefabPath.empty())
			{
				LOG_WARN("Create prefab variant requires exactly one selected prefab asset.");
				return;
			}

			basePrefabPath = asset.path;
		}

		if (basePrefabPath.empty())
		{
			LOG_WARN("Create prefab variant requires exactly one selected prefab asset.");
			return;
		}

		const int32_t dlgWidth = 420;
		const int32_t dlgHeight = 130;
		auto* dlg = new HexEngine::Dialog(
			HexEngine::g_pEnv->GetUIManager().GetRootElement(),
			HexEngine::Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
			HexEngine::Point(dlgWidth, dlgHeight),
			L"Create Prefab Variant");

		auto* outputName = new HexEngine::LineEdit(
			dlg,
			HexEngine::Point(12, 12),
			HexEngine::Point(dlgWidth - 24, 24),
			L"Variant name");
		outputName->SetValue(basePrefabPath.stem().wstring() + L"_Variant");
		outputName->SetDoesCallbackWaitForReturn(false);

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 180, dlgHeight - 38),
			HexEngine::Point(78, 24),
			L"Cancel",
			[dlg](HexEngine::Button*) {
				dlg->DeleteMe();
				return true;
			});

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 92, dlgHeight - 38),
			HexEngine::Point(78, 24),
			L"Create",
			[this, dlg, outputName, basePrefabPath](HexEngine::Button*) {
				CreatePrefabVariant(basePrefabPath, outputName->GetValue());
				dlg->DeleteMe();
				return true;
			});
	}

	void AssetExplorer::CreatePrefabVariant(const fs::path& basePrefabPath, const std::wstring& requestedName)
	{
		if (_currentlyBrowsedFS == nullptr || basePrefabPath.empty() || basePrefabPath.extension() != ".hprefab")
			return;

		std::wstring stem = requestedName;
		if (stem.empty())
		{
			stem = basePrefabPath.stem().wstring() + L"_Variant";
		}

		const fs::path requestedPath(stem);
		stem = SanitizeFileName(requestedPath.stem().wstring().empty() ? requestedPath.wstring() : requestedPath.stem().wstring());

		fs::path outputFileName = fs::path(stem).filename();
		if (outputFileName.extension() != L".hprefab")
			outputFileName.replace_extension(L".hprefab");

		fs::path outputPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / outputFileName);
		bool isSameAsBase = false;
		{
			std::error_code eqError;
			isSameAsBase = fs::equivalent(outputPath, basePrefabPath, eqError);
			if (eqError)
			{
				isSameAsBase = outputPath.lexically_normal() == basePrefabPath.lexically_normal();
			}
		}
		if (isSameAsBase)
		{
			outputPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / (outputPath.stem().wstring() + L"_Variant.hprefab"));
		}

		if (fs::exists(outputPath))
		{
			const std::wstring baseName = outputPath.stem().wstring();
			for (int32_t i = 1; i < 1024; ++i)
			{
				fs::path candidate = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / (baseName + L"_" + std::to_wstring(i) + L".hprefab"));
				if (!fs::exists(candidate))
				{
					outputPath = candidate;
					break;
				}
			}
		}

		std::error_code ec;
		fs::path baseReference = fs::relative(basePrefabPath, outputPath.parent_path(), ec);
		if (ec || baseReference.empty())
		{
			baseReference = basePrefabPath.filename();
		}

		json variantData = json::object();
		variantData["header"] = {
			{"version", 2}
		};
		variantData["variant"] = {
			{"basePrefab", baseReference.generic_string()},
			{"patches", json::array()}
		};

		std::ofstream outputFile(outputPath, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!outputFile.is_open())
		{
			LOG_WARN("Failed to create prefab variant file '%s'.", outputPath.string().c_str());
			return;
		}

		outputFile << variantData.dump(2);
		outputFile.close();

		UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);

		for (auto& asset : _assetsInView)
			asset.selected = false;

		if (auto* newAsset = FindAssetInView(outputPath); newAsset != nullptr)
		{
			newAsset->selected = true;
		}

		_canvas.Redraw();
		LOG_INFO("Created prefab variant '%s' from base '%s'.", outputPath.string().c_str(), basePrefabPath.string().c_str());
	}

	void AssetExplorer::ShowCombineMeshesDialog()
	{
		CloseContextMenu();

		int32_t numSelectedMeshes = 0;
		for (const auto& asset : _assetsInView)
		{
			if (asset.selected && asset.path.extension() == ".hmesh")
			{
				numSelectedMeshes++;
			}
		}

		if (numSelectedMeshes < 2)
			return;

		_combineDeleteOriginals = false;

		const int32_t dlgWidth = 420;
		const int32_t dlgHeight = 160;
		auto* dlg = new HexEngine::Dialog(
			HexEngine::g_pEnv->GetUIManager().GetRootElement(),
			HexEngine::Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
			HexEngine::Point(dlgWidth, dlgHeight),
			L"Combine Meshes");

		auto* outputName = new HexEngine::LineEdit(
			dlg,
			HexEngine::Point(12, 12),
			HexEngine::Point(dlgWidth - 24, 24),
			L"Output name");
		outputName->SetValue(L"CombinedMesh");
		outputName->SetDoesCallbackWaitForReturn(false);

		new HexEngine::Checkbox(
			dlg,
			HexEngine::Point(12, 46),
			HexEngine::Point(dlgWidth - 24, 22),
			L"Delete original files after combine",
			&_combineDeleteOriginals);

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 180, dlgHeight - 68),
			HexEngine::Point(78, 24),
			L"Cancel",
			[dlg](HexEngine::Button*) {
				dlg->DeleteMe();
				return true;
			});

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 92, dlgHeight - 68),
			HexEngine::Point(78, 24),
			L"Combine",
			[this, dlg, outputName](HexEngine::Button*) {
				CombineSelectedMeshes(outputName->GetValue(), _combineDeleteOriginals);
				dlg->DeleteMe();
				return true;
			});
	}

	void AssetExplorer::ShowAutoCombineMeshesDialog()
	{
		CloseContextMenu();

		int32_t numSelectedMeshes = 0;
		for (const auto& asset : _assetsInView)
		{
			if (asset.selected && asset.path.extension() == ".hmesh")
			{
				numSelectedMeshes++;
			}
		}

		if (numSelectedMeshes < 2)
			return;

		_autoCombineDeleteOriginals = false;

		const int32_t dlgWidth = 420;
		const int32_t dlgHeight = 130;
		auto* dlg = new HexEngine::Dialog(
			HexEngine::g_pEnv->GetUIManager().GetRootElement(),
			HexEngine::Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
			HexEngine::Point(dlgWidth, dlgHeight),
			L"Auto Combine Meshes By Material");

		new HexEngine::Checkbox(
			dlg,
			HexEngine::Point(12, 12),
			HexEngine::Point(dlgWidth - 24, 22),
			L"Delete original files after combine",
			&_autoCombineDeleteOriginals);

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 180, dlgHeight - 38),
			HexEngine::Point(78, 24),
			L"Cancel",
			[dlg](HexEngine::Button*) {
				dlg->DeleteMe();
				return true;
			});

		new HexEngine::Button(
			dlg,
			HexEngine::Point(dlgWidth - 92, dlgHeight - 38),
			HexEngine::Point(78, 24),
			L"Combine",
			[this, dlg](HexEngine::Button*) {
				AutoCombineSelectedMeshesByMaterial(_autoCombineDeleteOriginals);
				dlg->DeleteMe();
				return true;
			});
	}

	void AssetExplorer::CombineSelectedMeshes(fs::path outputFileName, bool removeOriginalFiles)
	{
		CloseContextMenu();

		if (_currentlyBrowsedFS == nullptr)
			return;

		// ensure correct file extension
		if (outputFileName.filename().extension() != ".hmesh")
			outputFileName.replace_extension(".hmesh");

		std::vector<fs::path> selectedMeshPaths;
		selectedMeshPaths.reserve(_assetsInView.size());

		for (const auto& asset : _assetsInView)
		{
			if (asset.selected && asset.path.extension() == ".hmesh")
			{
				selectedMeshPaths.push_back(asset.path);
			}
		}

		if (selectedMeshPaths.size() < 2)
			return;

		if (CombineMeshPathGroup(selectedMeshPaths, outputFileName, removeOriginalFiles))
		{
			UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);
		}
	}

	void AssetExplorer::AutoCombineSelectedMeshesByMaterial(bool removeOriginalFiles)
	{
		CloseContextMenu();

		if (_currentlyBrowsedFS == nullptr)
			return;

		std::map<std::wstring, std::vector<fs::path>> groupedByMaterial;
		for (const auto& asset : _assetsInView)
		{
			if (!asset.selected || asset.path.extension() != ".hmesh")
				continue;

			auto mesh = HexEngine::Mesh::Create(asset.path);
			if (!mesh)
				continue;

			if (mesh->HasAnimations())
				continue;

			std::wstring materialKey = L"Default";
			if (auto mat = mesh->GetMaterial(); mat != nullptr)
			{
				const auto& materialPath = mat->GetFileSystemPath();
				if (!materialPath.empty())
				{
					materialKey = materialPath.wstring();
				}
			}

			groupedByMaterial[materialKey].push_back(asset.path);
		}

		bool didCombine = false;
		for (const auto& group : groupedByMaterial)
		{
			if (group.second.size() < 2)
				continue;

			fs::path materialPath = fs::path(group.first);
			std::wstring baseName = materialPath.stem().wstring();
			if (baseName.empty())
			{
				baseName = L"Default";
			}

			baseName = SanitizeFileName(baseName);

			const fs::path outputFileName = L"AutoCombined_" + baseName + L".hmesh";
			if (CombineMeshPathGroup(group.second, outputFileName, removeOriginalFiles))
			{
				didCombine = true;
			}
		}

		if (didCombine)
		{
			UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);
		}
	}

	bool AssetExplorer::CombineMeshPathGroup(const std::vector<fs::path>& meshPaths, fs::path outputFileName, bool removeOriginalFiles)
	{
		if (_currentlyBrowsedFS == nullptr || meshPaths.size() < 2)
			return false;

		std::vector<HexEngine::MeshVertex> combinedVertices;
		std::vector<HexEngine::MeshIndexFormat> combinedIndices;
		combinedVertices.reserve(4096);
		combinedIndices.reserve(4096);

		std::shared_ptr<HexEngine::Material> firstMaterial;

		for (const auto& meshPath : meshPaths)
		{
			auto mesh = HexEngine::Mesh::Create(meshPath);
			if (!mesh)
			{
				LOG_WARN("Skipping mesh '%s' while combining because it failed to load", meshPath.string().c_str());
				continue;
			}

			if (mesh->HasAnimations())
			{
				LOG_WARN("Skipping mesh '%s' while combining because animated meshes are not supported", meshPath.string().c_str());
				continue;
			}

			const auto& sourceVertices = mesh->GetVertices();
			const auto& sourceIndices = mesh->GetIndices();
			if (sourceVertices.empty() || sourceIndices.empty())
				continue;

			if (!firstMaterial && mesh->GetMaterial())
			{
				firstMaterial = mesh->GetMaterial();
			}

			const HexEngine::MeshIndexFormat indexOffset = static_cast<HexEngine::MeshIndexFormat>(combinedVertices.size());
			combinedVertices.insert(combinedVertices.end(), sourceVertices.begin(), sourceVertices.end());

			for (const auto index : sourceIndices)
			{
				combinedIndices.push_back(index + indexOffset);
			}
		}

		if (combinedVertices.empty() || combinedIndices.empty())
		{
			LOG_WARN("Combine mesh operation produced no output data");
			return false;
		}

		fs::path outputFilePath = fs::path(outputFileName).filename();
		if (outputFilePath.empty() || outputFilePath == L"." || outputFilePath == L"..")
		{
			outputFilePath = L"CombinedMesh";
		}

		if (outputFilePath.extension() != L".hmesh")
		{
			outputFilePath.replace_extension(L".hmesh");
		}

		fs::path outputPath = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / outputFilePath);
		if (fs::exists(outputPath))
		{
			const std::wstring baseName = outputPath.stem().wstring();
			for (int32_t i = 1; i < 1024; ++i)
			{
				fs::path candidate = _currentlyBrowsedFS->GetLocalAbsoluteDataPath(_currentlyBrowsedFolder / (baseName + L"_" + std::to_wstring(i) + L".hmesh"));
				if (!fs::exists(candidate))
				{
					outputPath = candidate;
					break;
				}
			}
		}

		auto combinedMesh = std::shared_ptr<HexEngine::Mesh>(new HexEngine::Mesh(nullptr, outputPath.stem().string()), HexEngine::ResourceDeleter());
		combinedMesh->SetPaths(outputPath, _currentlyBrowsedFS);
		combinedMesh->SetLoader(HexEngine::g_pEnv->_meshLoader);
		combinedMesh->SetNumFaces(static_cast<uint32_t>(combinedIndices.size() / 3));
		combinedMesh->AddVertices(combinedVertices);
		combinedMesh->AddIndices(combinedIndices);

		dx::BoundingBox aabb;
		dx::BoundingBox::CreateFromPoints(
			aabb,
			static_cast<size_t>(combinedVertices.size()),
			reinterpret_cast<const math::Vector3*>(combinedVertices.data()),
			sizeof(HexEngine::MeshVertex));
		combinedMesh->SetAABB(aabb);

		dx::BoundingOrientedBox obb;
		dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
		combinedMesh->SetOBB(obb);

		if (firstMaterial)
		{
			combinedMesh->SetMaterial(firstMaterial);
		}
		else
		{
			combinedMesh->SetMaterial(HexEngine::Material::GetDefaultMaterial());
		}

		combinedMesh->Save();

		if (removeOriginalFiles)
		{
			for (const auto& meshPath : meshPaths)
			{
				if (meshPath == outputPath)
					continue;

				std::error_code ec;
				fs::remove(meshPath, ec);
				if (ec)
				{
					LOG_WARN("Failed to remove original mesh '%s': %s", meshPath.string().c_str(), ec.message().c_str());
				}
			}
		}

		return true;
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

			if (g_pUIManager != nullptr)
			{
				g_pUIManager->RecordEntityCreated(entity);
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
				if (_hoveredAsset->path.extension() == ".hprefab" && g_pUIManager != nullptr)
				{
					g_pUIManager->OpenPrefabStage(_hoveredAsset->path);
					_hoveredAsset = nullptr;
					return true;
				}

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
				int32_t numSelectedMeshes = 0;
				int32_t numSelectedPrefabs = 0;
				for (const auto& asset : _assetsInView)
				{
					if (asset.selected)
					{
						numSelection++;
						if (asset.path.extension() == ".hmesh")
						{
							numSelectedMeshes++;
						}
						else if (asset.path.extension() == ".hprefab")
						{
							numSelectedPrefabs++;
						}
					}
				}

				auto* menuParent = GetParent() ? GetParent() : this;
				HexEngine::Point p;
				p.x = data->MouseDown.xpos - menuParent->GetAbsolutePosition().x - 10;
				p.y = data->MouseDown.ypos - menuParent->GetAbsolutePosition().y - 10;
				_contextMenu = new HexEngine::ContextMenu(menuParent, p);

				if (numSelection >= 1)
				{
					if (numSelection == 1 && numSelectedPrefabs == 1)
					{
						_contextMenu->AddItem(new HexEngine::ContextItem(L"Create prefab variant...", std::bind(&AssetExplorer::ShowCreatePrefabVariantDialog, this)));
					}

					_contextMenu->AddItem(new HexEngine::ContextItem(L"Set material", std::bind(&AssetExplorer::SetMassMaterial, this)));
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Import meshes", std::bind(&AssetExplorer::ImportAllForeignMeshes, this)));
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Delete",
						[this](const std::wstring& item) {
							fs::remove(_hoveredAsset->path);
							UpdateAssets(_currentlyBrowsedFolder, _currentlyBrowsedFS);
						}));

					if (numSelectedMeshes >= 2)
					{
						_contextMenu->AddItem(new HexEngine::ContextItem(L"Combine meshes...", std::bind(&AssetExplorer::ShowCombineMeshesDialog, this)));
						_contextMenu->AddItem(new HexEngine::ContextItem(L"Auto combine by material...", std::bind(&AssetExplorer::ShowAutoCombineMeshesDialog, this)));
					}
				}
				else
				{
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Select all", std::bind(&AssetExplorer::SelectAll, this)));
					_contextMenu->AddItem(new HexEngine::ContextItem(L"Add all meshes to scene", std::bind(&AssetExplorer::ImportAllMeshes, this)));

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
				_recentlyDroppedAssetPath = _draggingAsset->path;
				_hasRecentlyDroppedAsset = true;

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
				return IsMouseOver(true);
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
			const int32_t width = std::max(0L, marquee.right - marquee.left);
			const int32_t height = std::max(0L, marquee.bottom - marquee.top);

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
