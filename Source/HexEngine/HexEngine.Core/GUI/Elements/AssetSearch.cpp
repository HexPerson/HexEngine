#include "AssetSearch.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../FileSystem/ResourceSystem.hpp"
#include "../../Graphics/IconService.hpp"
#include <algorithm>
#include <cwctype>

namespace HexEngine
{
	namespace
	{
		constexpr int32_t kRowHeight = 46;
		constexpr int32_t kRowIconSize = 42;
		constexpr int32_t kRowPaddingX = 6;
		constexpr int32_t kPopupVerticalOffset = 2;
		constexpr int32_t kSearchBarHeight = 20;
		constexpr int32_t kPreviewPadding = 2;
	}

	class AssetSearch::AssetSearchRow final : public Element
	{
	public:
		AssetSearchRow(AssetSearch* owner, Element* parent, const Point& position, const Point& size, size_t rowIndex) :
			Element(parent, position, size),
			_owner(owner),
			_rowIndex(rowIndex)
		{
		}

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override
		{
			if (_owner == nullptr || _rowIndex >= _owner->_results.size())
				return;

			const auto pos = GetAbsolutePosition();
			const auto size = GetSize();
			const bool highlighted = _owner->_highlightedIndex == _rowIndex;

			if (highlighted != _wasHighlighted)
			{
				_canvas.Redraw();
				_wasHighlighted = highlighted;
			}

			if (_canvas.BeginDraw(renderer, size.x, size.y))
			{
				const bool hovered = IsMouseOver(true);
				
				auto& result = _owner->_results[_rowIndex];

				bool isVisibleInPopup = true;
				if (_owner->_popup != nullptr)
				{
					const Point popupAbs = _owner->_popup->GetAbsolutePosition();
					const Point popupSize = _owner->_popup->GetSize();
					const int32_t viewTop = popupAbs.y;
					const int32_t viewBottom = popupAbs.y + popupSize.y;
					const int32_t rowTop = pos.y;
					const int32_t rowBottom = pos.y + size.y;
					isVisibleInPopup = rowBottom > viewTop && rowTop < viewBottom;
				}

				if (isVisibleInPopup && !result.absolutePath.empty() && g_pEnv->_iconService != nullptr)
				{
					// Refresh every frame so we do not hold stale raw icon pointers after icon regeneration/removal.
					result.preview = g_pEnv->_iconService->GetIcon(result.absolutePath);
					if (result.preview == nullptr && !result.previewRequested)
					{
						g_pEnv->_iconService->PushFilePathForIconGeneration(result.absolutePath);
						result.previewRequested = true;
					}
				}

				if (hovered || highlighted)
				{
					renderer->FillQuad(
						0,
						0,
						_size.x,
						_size.y,
						hovered ? renderer->_style.button_hover : math::Color(HEX_RGBA_TO_FLOAT4(70, 80, 95, 255)));
				}

				if (result.preview != nullptr)
				{
					renderer->FillTexturedQuad(
						result.preview,
						kRowPaddingX,
						(_size.y - kRowIconSize) / 2,
						kRowIconSize,
						kRowIconSize,
						math::Color(1, 1, 1, 1));
				}
				else
				{
					renderer->FillQuad(
						kRowPaddingX,
						(_size.y - kRowIconSize) / 2,
						kRowIconSize,
						kRowIconSize,
						math::Color(HEX_RGBA_TO_FLOAT4(60, 60, 60, 255)));
				}

				const int32_t textX = kRowPaddingX + kRowIconSize + 8;
				const int32_t textTop = 5;

				const std::wstring title = result.displayName.empty()
					? result.assetPath.filename().wstring()
					: result.displayName;
				renderer->PrintText(
					renderer->_style.font.get(),
					(uint8_t)Style::FontSize::Tiny,
					textX,
					textTop,
					renderer->_style.text_regular,
					FontAlign::None,
					title);

				renderer->PrintText(
					renderer->_style.font.get(),
					(uint8_t)Style::FontSize::Tiny,
					textX,
					textTop + 14,
					math::Color(HEX_RGBA_TO_FLOAT4(150, 150, 150, 255)),
					FontAlign::None,
					result.assetPath.wstring());

				_canvas.EndDraw(renderer);
			}

			_canvas.Present(renderer, pos.x, pos.y, size.x, size.y);
		}

		virtual bool OnInputEvent(InputEvent event, InputData* data) override
		{
			if (_owner == nullptr)
				return false;

			if (event == InputEvent::MouseMove && IsMouseOver(true))
			{
				_owner->_highlightedIndex = _rowIndex;
				_canvas.Redraw();
			}
			else if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && IsMouseOver(true))
			{
				_owner->OnPickResult(_rowIndex);
				return true;
			}

			return false;
		}

	private:
		AssetSearch* _owner = nullptr;
		size_t _rowIndex = 0;
		bool _wasHighlighted = false;
	};

	AssetSearch::AssetSearch(
		Element* parent,
		const Point& position,
		const Point& size,
		const std::wstring& label,
		const std::vector<ResourceType>& allowedTypes,
		OnSelectFn onSelect) :
		Element(parent, position, size),
		_allowedTypes(allowedTypes),
		_onSelect(onSelect)
	{
		const int32_t editHeight = std::min(kSearchBarHeight, std::max(16, size.y));
		_edit = new LineEdit(this, Point(0, 0), Point(size.x, editHeight), label);
		_edit->SetIcon(ITexture2D::Create("EngineData.Textures/UI/magnifying_glass.png"), math::Color(HEX_RGBA_TO_FLOAT4(180, 180, 180, 255)));
		_edit->SetOnInputFn(std::bind(&AssetSearch::OnSearchTextChanged, this, std::placeholders::_1, std::placeholders::_2));
		_edit->SetOnDoubleClickFn([this](LineEdit*, const std::wstring&)
			{
				HandleDoubleClick();
			});
		_edit->SetDoesCallbackWaitForReturn(false);
	}

	AssetSearch::~AssetSearch()
	{
		ClosePopup();
	}

	void AssetSearch::SetQueryFn(QueryFn fn)
	{
		_queryFn = fn;
	}

	void AssetSearch::SetOnSelectFn(OnSelectFn fn)
	{
		_onSelect = fn;
	}

	void AssetSearch::SetAllowedTypes(const std::vector<ResourceType>& allowedTypes)
	{
		_allowedTypes = allowedTypes;
		RefreshResults();
	}

	void AssetSearch::SetOnDoubleClickFn(OnDoubleClickFn fn)
	{
		_onDoubleClickFn = fn;
	}

	void AssetSearch::SetOnDragAndDropFn(OnDragAndDropFn fn)
	{
		_onDragAndDropFn = fn;
	}

	const std::vector<ResourceType>& AssetSearch::GetAllowedTypes() const
	{
		return _allowedTypes;
	}

	void AssetSearch::SetValue(const std::wstring& value)
	{
		if (_edit != nullptr)
		{
			_edit->SetValue(value);
		}

		AssetSearchResult resolved;
		if (BuildResultFromPath(fs::path(value), resolved) && IsFilterTypeAllowed(resolved.type))
		{
			_selectedResult = std::move(resolved);
			_hasSelection = true;
		}
		else
		{
			_hasSelection = false;
			_selectedResult = {};
		}
	}

	const std::wstring& AssetSearch::GetValue() const
	{
		static const std::wstring kEmpty;
		return _edit != nullptr ? _edit->GetValue() : kEmpty;
	}

	bool AssetSearch::GetSelectedResult(AssetSearchResult& outResult) const
	{
		if (!_hasSelection)
			return false;

		outResult = _selectedResult;
		return true;
	}

	void AssetSearch::ClearSelection()
	{
		_hasSelection = false;
		_selectedResult = {};
	}

	void AssetSearch::RefreshResults()
	{
		OnSearchTextChanged(_edit, _edit != nullptr ? _edit->GetValue() : L"");
	}

	void AssetSearch::PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		if (_edit == nullptr)
			return;

		if (_hasSelection == false)
			return;

		if (!_selectedResult.absolutePath.empty() && g_pEnv->_iconService != nullptr)
		{
			// Refresh every frame so we do not hold stale raw icon pointers after icon regeneration/removal.
			_selectedResult.preview = g_pEnv->_iconService->GetIcon(_selectedResult.absolutePath);
			if (_selectedResult.preview == nullptr && !_selectedResult.previewRequested)
			{
				g_pEnv->_iconService->PushFilePathForIconGeneration(_selectedResult.absolutePath);
				_selectedResult.previewRequested = true;
			}
		}

		if (_selectedResult.preview == nullptr)
			return;

		const Point abs = _edit->GetAbsolutePosition();
		const int32_t previewAreaY = abs.y + _edit->GetSize().y + kPreviewPadding;
		const int32_t previewAreaH = _size.y - _edit->GetSize().y - (kPreviewPadding * 2);
		const int32_t previewAreaW = _size.x - (kPreviewPadding * 2);
		if (previewAreaH <= 8 || previewAreaW <= 8)
			return;

		const int32_t previewSize = std::max(8, std::min(previewAreaW, previewAreaH));
		const int32_t previewX = abs.x + (_size.x - previewSize) / 2;
		const int32_t previewY = previewAreaY + (previewAreaH - previewSize) / 2;

		//renderer->FillQuad(abs.x, previewAreaY, _size.x, previewAreaH, math::Color(HEX_RGBA_TO_FLOAT4(24, 24, 24, 255)));
		renderer->FillQuad(previewX - 1, previewY - 1, previewSize + 2, previewSize + 2, math::Color(HEX_RGBA_TO_FLOAT4(40, 40, 40, 255)));
		renderer->FillTexturedQuad(_selectedResult.preview, previewX, previewY, previewSize, previewSize, math::Color(1, 1, 1, 1));
	}

	bool AssetSearch::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _edit != nullptr && _edit->IsMouseOver(true))
		{
			if (!IsPopupOpen())
			{
				RefreshResults();
			}
		}

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			const bool inEdit = (_edit != nullptr) && _edit->IsMouseOver(true);
			const bool inPopup = (_popup != nullptr) && _popup->IsMouseOver(true);
			if (!inEdit && !inPopup)
			{
				ClosePopup();
			}
		}
		else if (event == InputEvent::KeyDown && IsPopupOpen() && _edit != nullptr && _edit->IsInputFocus())
		{
			if (data->KeyDown.key == VK_DOWN)
			{
				if (_highlightedIndex + 1 < _results.size())
					++_highlightedIndex;
				RebuildPopupRows();
				return true;
			}
			if (data->KeyDown.key == VK_UP)
			{
				if (_highlightedIndex > 0)
					--_highlightedIndex;
				RebuildPopupRows();
				return true;
			}
			if (data->KeyDown.key == VK_RETURN)
			{
				if (!_results.empty() && _highlightedIndex < _results.size())
				{
					OnPickResult(_highlightedIndex);
					return true;
				}
			}
			if (data->KeyDown.key == VK_ESCAPE)
			{
				ClosePopup();
				return true;
			}
		}
		else if (event == InputEvent::DragAndDrop)
		{
			if (IsMouseOver(true))
			{
				HandleDroppedPath(data->DragAndDrop.path);
				return true;
			}
		}

		return Element::OnInputEvent(event, data);
	}

	int32_t AssetSearch::GetLabelWidth() const
	{
		return _edit != nullptr ? _edit->GetLabelWidth() : 0;
	}

	void AssetSearch::SetLabelMinSize(int32_t minSize)
	{
		if (_edit != nullptr)
		{
			_edit->SetLabelMinSize(minSize);
		}
	}

	void AssetSearch::OnSearchTextChanged(LineEdit* edit, const std::wstring& value)
	{
		(void)edit;

		_results.clear();
		if (_queryFn)
		{
			_queryFn(value, _allowedTypes, _results);
		}
		else
		{
			RunDefaultQuery(value, _results);
		}

		std::sort(_results.begin(), _results.end(),
			[](const AssetSearchResult& a, const AssetSearchResult& b)
			{
				return a.displayName < b.displayName;
			});
		if (_results.size() > _maxResults)
		{
			_results.resize(_maxResults);
		}

		if (_results.empty())
		{
			ClosePopup();
			return;
		}

		if (_highlightedIndex >= _results.size())
		{
			_highlightedIndex = 0;
		}

		OpenPopup();
		RebuildPopupRows();
	}

	void AssetSearch::OnPickResult(size_t index)
	{
		if (index >= _results.size() || _edit == nullptr)
			return;

		_highlightedIndex = index;
		_selectedResult = _results[index];
		_hasSelection = true;
		_edit->SetValue(_selectedResult.assetPath.wstring());
		ClosePopup();

		if (_onSelect)
		{
			_onSelect(this, _selectedResult);
		}
	}

	void AssetSearch::OpenPopup()
	{
		if (_popup != nullptr || _edit == nullptr)
			return;

		Element* root = g_pEnv->GetUIManager().GetRootElement();
		if (root == nullptr)
			return;

		const Point abs = _edit->GetAbsolutePosition();
		_popup = new ScrollView(
			root,
			Point(abs.x, abs.y + _edit->GetSize().y + kPopupVerticalOffset),
			Point(_size.x, _popupMaxHeight));
		_popup->BringToFront();
	}

	void AssetSearch::ClosePopup()
	{
		if (_popup != nullptr)
		{
			_popup->DeleteMe();
			_popup = nullptr;
		}
	}

	void AssetSearch::RebuildPopupRows()
	{
		if (_popup == nullptr)
			return;

		auto* contentRoot = _popup->GetContentRoot();
		if (contentRoot == nullptr)
			return;

		std::vector<Element*> oldChildren = contentRoot->GetChildren();
		for (auto* child : oldChildren)
		{
			if (child != nullptr)
			{
				child->DeleteMe();
			}
		}

		int32_t y = 0;
		for (size_t i = 0; i < _results.size(); ++i)
		{
			(void)CreateRow(contentRoot, y, i);
			y += kRowHeight;
		}

		_popup->SetManualContentHeight(std::max(_popup->GetSize().y, y));
	}

	bool AssetSearch::IsPopupOpen() const
	{
		return _popup != nullptr;
	}

	bool AssetSearch::IsFilterTypeAllowed(ResourceType type) const
	{
		if (_allowedTypes.empty())
			return true;

		return std::find(_allowedTypes.begin(), _allowedTypes.end(), type) != _allowedTypes.end();
	}

	void AssetSearch::RunDefaultQuery(const std::wstring& filter, std::vector<AssetSearchResult>& outResults) const
	{
		const std::wstring loweredFilter = ToLowerCopy(filter);
		auto& rs = g_pEnv->GetResourceSystem();

		for (auto* fileSystem : rs.GetFileSystems())
		{
			if (fileSystem == nullptr)
				continue;

			const fs::path dataDir = fileSystem->GetDataDirectory();
			if (dataDir.empty() || !fs::exists(dataDir))
				continue;

			for (const auto& entry : fs::recursive_directory_iterator(dataDir))
			{
				if (!entry.is_regular_file())
					continue;

				const fs::path absPath = entry.path();
				const ResourceType type = ResourceTypeFromPath(absPath);
				if (type == ResourceType::None)
					continue;
				if (!IsFilterTypeAllowed(type))
					continue;

				std::error_code ec;
				const fs::path rel = fs::relative(absPath, dataDir, ec);
				if (ec)
					continue;
				const fs::path assetPath = fileSystem->GetRelativeResourcePath(rel);

				const std::wstring searchable = ToLowerCopy((assetPath.wstring() + L" " + absPath.filename().wstring()));
				if (!loweredFilter.empty() && searchable.find(loweredFilter) == std::wstring::npos)
					continue;

				AssetSearchResult result;
				result.absolutePath = absPath;
				result.assetPath = assetPath;
				result.displayName = absPath.filename().wstring();
				result.type = type;

				outResults.push_back(std::move(result));
				if (outResults.size() >= _maxResults)
					return;
			}
		}
	}

	void AssetSearch::HandleDroppedPath(const fs::path& droppedPath)
	{
		if (_edit == nullptr || droppedPath.empty())
			return;

		AssetSearchResult resolved;
		if (!BuildResultFromPath(droppedPath, resolved))
			return;

		if (!IsFilterTypeAllowed(resolved.type))
		{
			LOG_WARN("Rejected dropped asset '%s' for AssetSearch: resource type is not allowed.",
				droppedPath.string().c_str());
			return;
		}

		_selectedResult = resolved;
		_hasSelection = true;
		_edit->SetValue(!_selectedResult.assetPath.empty()
			? _selectedResult.assetPath.wstring()
			: _selectedResult.absolutePath.filename().wstring());
		ClosePopup();

		if (_onSelect)
		{
			_onSelect(this, _selectedResult);
		}

		if (_onDragAndDropFn)
		{
			_onDragAndDropFn(this, _selectedResult);
		}
	}

	void AssetSearch::HandleDoubleClick()
	{
		if (_edit == nullptr)
			return;

		AssetSearchResult resolved;
		if (_hasSelection)
		{
			resolved = _selectedResult;
		}
		else if (!BuildResultFromPath(fs::path(_edit->GetValue()), resolved))
		{
			return;
		}

		if (_onDoubleClickFn)
		{
			_onDoubleClickFn(this, resolved);
			return;
		}

		fs::path editorPath = !resolved.assetPath.empty() ? resolved.assetPath : resolved.absolutePath;
		if (editorPath.empty())
			return;

		auto* loader = g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(editorPath.extension().string());
		if (loader == nullptr)
			return;

		loader->CreateEditorDialog({ editorPath });
	}

	bool AssetSearch::BuildResultFromPath(const fs::path& inputPath, AssetSearchResult& outResult) const
	{
		if (inputPath.empty())
			return false;

		auto& resourceSystem = g_pEnv->GetResourceSystem();
		fs::path absolutePath = inputPath;
		fs::path assetPath;

		if (!fs::exists(absolutePath))
		{
			if (auto* fsFromAsset = resourceSystem.FindFileSystemByPath(inputPath); fsFromAsset != nullptr)
			{
				absolutePath = fsFromAsset->GetLocalAbsoluteDataPath(inputPath);
				if (!fs::exists(absolutePath))
					return false;

				std::error_code ec;
				const fs::path rel = fs::relative(absolutePath, fsFromAsset->GetDataDirectory(), ec);
				if (!ec)
				{
					assetPath = fsFromAsset->GetRelativeResourcePath(rel);
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			if (auto* fsFromAsset = resourceSystem.FindAssetFileSystemForAsset(absolutePath); fsFromAsset != nullptr)
			{
				std::error_code ec;
				const fs::path rel = fs::relative(absolutePath, fsFromAsset->GetDataDirectory(), ec);
				if (!ec)
				{
					assetPath = fsFromAsset->GetRelativeResourcePath(rel);
				}
			}
		}

		const ResourceType type = ResourceTypeFromPath(absolutePath);
		if (type == ResourceType::None)
			return false;

		outResult = {};
		outResult.absolutePath = absolutePath;
		outResult.assetPath = assetPath;
		outResult.displayName = absolutePath.filename().wstring();
		outResult.type = type;
		outResult.preview = g_pEnv->_iconService ? g_pEnv->_iconService->GetIcon(absolutePath) : nullptr;

		if (outResult.preview == nullptr && g_pEnv->_iconService != nullptr)
		{
			g_pEnv->_iconService->PushFilePathForIconGeneration(absolutePath);
		}

		return true;
	}

	ResourceType AssetSearch::ResourceTypeFromPath(const fs::path& path)
	{
		const std::wstring ext = ToLowerCopy(path.extension().wstring());

		if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".dds" || ext == L".tga" || ext == L".bmp")
			return ResourceType::Image;

		if (ext == L".hmesh")
			return ResourceType::Mesh;

		if (ext == L".wav" || ext == L".mp3" || ext == L".ogg" || ext == L".flac")
			return ResourceType::Audio;

		if (ext == L".ttf" || ext == L".otf")
			return ResourceType::Font;

		if (ext == L".hprefab")
			return ResourceType::Prefab;

		if (ext == L".hmat")
			return ResourceType::Material;

		return ResourceType::None;
	}

	std::wstring AssetSearch::ToLowerCopy(const std::wstring& value)
	{
		std::wstring lowered = value;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](wchar_t c) { return (wchar_t)towlower(c); });
		return lowered;
	}

	AssetSearch::AssetSearchRow* AssetSearch::CreateRow(Element* parent, int32_t y, size_t index)
	{
		return new AssetSearchRow(this, parent, Point(0, y), Point(_popup->GetSize().x - 12, kRowHeight), index);
	}
}
