#include "Explorer.hpp"
#include <algorithm>
#include <cmath>

namespace HexEditor
{
	namespace
	{
		constexpr int32_t LogPaddingX = 8;
		constexpr int32_t LogPaddingY = 6;
		constexpr int32_t LogLineHeight = 18;
		constexpr size_t MaxLogEntries = 5000;

		std::wstring FormatLogLine(const HexEngine::LogMessage& message)
		{
			std::wstring text;
			switch (message.level)
			{
			case HexEngine::LogLevel::Debug:
				text = L"[Debug] ";
				break;
			case HexEngine::LogLevel::Info:
				text = L"[Info] ";
				break;
			case HexEngine::LogLevel::Warn:
				text = L"[Warn] ";
				break;
			case HexEngine::LogLevel::Crit:
				text = L"[Critical] ";
				break;
			default:
				break;
			}

			text += s2ws(message.text);

			if (!message.file.empty() && message.line > 0)
			{
				text += L" (";
				text += s2ws(message.file);
				text += L":";
				text += std::to_wstring(message.line);
				text += L")";
			}

			return text;
		}

		math::Color GetLogLineColor(HexEngine::LogLevel level, HexEngine::GuiRenderer* renderer)
		{
			switch (level)
			{
			case HexEngine::LogLevel::Debug:
				return math::Color(HEX_RGBA_TO_FLOAT4(150, 150, 155, 255));
			case HexEngine::LogLevel::Info:
				return renderer->_style.text_regular;
			case HexEngine::LogLevel::Warn:
				return math::Color(HEX_RGBA_TO_FLOAT4(245, 190, 82, 255));
			case HexEngine::LogLevel::Crit:
				return math::Color(HEX_RGBA_TO_FLOAT4(232, 92, 92, 255));
			}

			return renderer->_style.text_regular;
		}
	}

	class LogScrollView final : public HexEngine::ScrollView
	{
	public:
		struct LineEntry
		{
			HexEngine::LogLevel level = HexEngine::LogLevel::Info;
			std::wstring text;
		};

		LogScrollView(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
			HexEngine::ScrollView(parent, position, size)
		{
			SetScrollSpeed(20.0f);
			SetManualContentHeight(size.y);
		}

		void Append(const std::vector<HexEngine::LogMessage>& messages)
		{
			if (messages.empty())
				return;

			const float oldMaxScroll = std::max(0.0f, (float)GetManualContentHeight() - (float)GetSize().y);
			const bool keepPinnedToBottom = GetScrollOffset() >= (oldMaxScroll - 2.0f);

			for (const auto& message : messages)
			{
				_lines.push_back({ message.level, FormatLogLine(message) });
			}

			if (_lines.size() > MaxLogEntries)
			{
				const size_t overflow = _lines.size() - MaxLogEntries;
				_lines.erase(_lines.begin(), _lines.begin() + overflow);
			}

			RefreshLayout(keepPinnedToBottom);
		}

		void SetVerbosityFilter(bool showDebug, bool showInfo, bool showWarn, bool showCrit)
		{
			if (_showDebug == showDebug &&
				_showInfo == showInfo &&
				_showWarn == showWarn &&
				_showCrit == showCrit)
			{
				return;
			}

			const float oldMaxScroll = std::max(0.0f, (float)GetManualContentHeight() - (float)GetSize().y);
			const bool keepPinnedToBottom = GetScrollOffset() >= (oldMaxScroll - 2.0f);

			_showDebug = showDebug;
			_showInfo = showInfo;
			_showWarn = showWarn;
			_showCrit = showCrit;

			RefreshLayout(keepPinnedToBottom);
		}

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override
		{
			HexEngine::ScrollView::Render(renderer, w, h);

			(void)w;
			(void)h;

			const auto pos = GetAbsolutePosition();
			const int32_t viewportTop = pos.y;
			const int32_t viewportBottom = pos.y + _size.y;
			int32_t drawY = pos.y + LogPaddingY - (int32_t)std::round(GetScrollOffset());

			for (const auto& line : _lines)
			{
				if (!IsLevelVisible(line.level))
					continue;

				if (drawY + LogLineHeight > viewportTop && drawY < viewportBottom)
				{
					renderer->PrintText(
						renderer->_style.font.get(),
						(uint8_t)HexEngine::Style::FontSize::Tiny,
						pos.x + LogPaddingX,
						drawY,
						GetLogLineColor(line.level, renderer),
						HexEngine::FontAlign::None,
						line.text);
				}

				drawY += LogLineHeight;
				if (drawY > viewportBottom + LogLineHeight)
					break;
			}
		}

	private:
		bool IsLevelVisible(HexEngine::LogLevel level) const
		{
			switch (level)
			{
			case HexEngine::LogLevel::Debug:
				return _showDebug;
			case HexEngine::LogLevel::Info:
				return _showInfo;
			case HexEngine::LogLevel::Warn:
				return _showWarn;
			case HexEngine::LogLevel::Crit:
				return _showCrit;
			}

			return true;
		}

		int32_t ComputeVisibleContentHeight() const
		{
			int32_t visibleLines = 0;
			for (const auto& line : _lines)
			{
				if (IsLevelVisible(line.level))
					visibleLines++;
			}

			const int32_t minHeight = GetSize().y;
			const int32_t contentHeight = (visibleLines * LogLineHeight) + (LogPaddingY * 2);
			return std::max(minHeight, contentHeight);
		}

		void RefreshLayout(bool keepPinnedToBottom)
		{
			const int32_t newContentHeight = ComputeVisibleContentHeight();
			SetManualContentHeight(newContentHeight);

			const float newMaxScroll = std::max(0.0f, (float)newContentHeight - (float)GetSize().y);
			if (keepPinnedToBottom)
			{
				SetScrollOffset(newMaxScroll);
			}
			else
			{
				SetScrollOffset(std::min(GetScrollOffset(), newMaxScroll));
			}

			_canvas.Redraw();
		}

	private:
		std::vector<LineEntry> _lines;
		bool _showDebug = true;
		bool _showInfo = true;
		bool _showWarn = true;
		bool _showCrit = true;
	};

	Explorer::Explorer(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Element(parent, position, size)
	{
		_folderExplorer = new FolderExplorer(this, HexEngine::Point(10, 10), HexEngine::Point(size.y - 20, size.y - 20));
		_folderExplorer->SetOnFolderSelected(std::bind(&Explorer::OnFolderSelected, this, std::placeholders::_1, std::placeholders::_2));

		_tab = new HexEngine::TabView(this, HexEngine::Point(size.y, 10), HexEngine::Point(HexEngine::g_pEnv->GetUIManager().GetWidth() - (size.y + 10), size.y - 20));
		_assetsTab = _tab->AddTab(L"Assets");
		_logTab = _tab->AddTab(L"Log");

		_fileSearchBar = new HexEngine::LineEdit(_tab, HexEngine::Point(10, 20), HexEngine::Point(_tab->GetSize().x - 20, 20), L"");
		_fileSearchBar->SetIcon(HexEngine::ITexture2D::Create(L"EngineData.Textures/UI/magnifying_glass.png"), math::Color(1, 1, 1, 1));
		_fileSearchBar->SetOnInputFn(std::bind(&Explorer::OnEnterSearchText, this, std::placeholders::_2));
		_fileSearchBar->SetDoesCallbackWaitForReturn(false);

		_assetExplorer = new AssetExplorer(_tab, HexEngine::Point(10, 50), HexEngine::Point(_tab->GetSize().x - 20, _tab->GetSize().y - 60));

		_showDebugCheckbox = new HexEngine::Checkbox(_tab, HexEngine::Point(10, 20), HexEngine::Point(95, 20), L"Debug", &_showDebugLogs);
		_showInfoCheckbox = new HexEngine::Checkbox(_tab, HexEngine::Point(110, 20), HexEngine::Point(95, 20), L"Info", &_showInfoLogs);
		_showWarnCheckbox = new HexEngine::Checkbox(_tab, HexEngine::Point(210, 20), HexEngine::Point(95, 20), L"Warn", &_showWarnLogs);
		_showCritCheckbox = new HexEngine::Checkbox(_tab, HexEngine::Point(310, 20), HexEngine::Point(110, 20), L"Critical", &_showCritLogs);

		_logView = new LogScrollView(_tab, HexEngine::Point(10, 50), HexEngine::Point(_tab->GetSize().x - 20, _tab->GetSize().y - 60));
		_logView->SetVerbosityFilter(_showDebugLogs, _showInfoLogs, _showWarnLogs, _showCritLogs);

		const auto onFilterChanged = [this](HexEngine::Checkbox*, bool) {
			if (_logView != nullptr)
			{
				_logView->SetVerbosityFilter(_showDebugLogs, _showInfoLogs, _showWarnLogs, _showCritLogs);
			}
		};

		_showDebugCheckbox->SetOnCheckFn(onFilterChanged);
		_showInfoCheckbox->SetOnCheckFn(onFilterChanged);
		_showWarnCheckbox->SetOnCheckFn(onFilterChanged);
		_showCritCheckbox->SetOnCheckFn(onFilterChanged);

		_showDebugCheckbox->DisableRecursive();
		_showInfoCheckbox->DisableRecursive();
		_showWarnCheckbox->DisableRecursive();
		_showCritCheckbox->DisableRecursive();
		_logView->DisableRecursive();

		if (HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile)
		{
			HexEngine::g_pEnv->GetLogFile().AddListener(this);
		}
	}

	Explorer::~Explorer()
	{
		if (HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile)
		{
			HexEngine::g_pEnv->GetLogFile().RemoveListener(this);
		}
	}

	bool Explorer::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		(void)event;
		(void)data;
		return false;
	}

	void Explorer::OnEnterSearchText(const std::wstring& text)
	{
		if (_assetExplorer != nullptr)
		{
			_assetExplorer->SetSearchFilter(text);
		}
	}

	void Explorer::OnFolderSelected(const fs::path& relativePath, HexEngine::FileSystem* fs)
	{
		if (_assetExplorer != nullptr)
		{
			_assetExplorer->UpdateAssets(relativePath, fs);
		}
	}

	Explorer::AssetDesc* Explorer::GetCurrentlyDraggedAsset() const
	{
		return _assetExplorer ? _assetExplorer->GetCurrentlyDraggedAsset() : nullptr;
	}

	bool Explorer::ConsumeRecentlyDroppedAssetPath(fs::path& outPath)
	{
		return _assetExplorer ? _assetExplorer->ConsumeRecentlyDroppedAssetPath(outPath) : false;
	}

	void Explorer::SetProjectPath(const fs::path& path)
	{
		_projectPath = path;
		UpdateFolderView();
	}

	void Explorer::UpdateFolderView()
	{
		if (_folderExplorer != nullptr)
		{
			_folderExplorer->UpdateFolderView();
		}
	}

	void Explorer::InvalidateAssetPreview(const fs::path& assetPath)
	{
		if (_assetExplorer != nullptr)
		{
			_assetExplorer->InvalidateAssetPreview(assetPath);
		}
	}

	void Explorer::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		std::vector<HexEngine::LogMessage> pendingMessages;
		{
			std::lock_guard<std::recursive_mutex> lock(_pendingLogLock);
			if (!_pendingMessages.empty())
			{
				pendingMessages.swap(_pendingMessages);
			}
		}

		if (_logView != nullptr && !pendingMessages.empty())
		{
			_logView->Append(pendingMessages);
		}

		const bool showAssetsTab = _tab->GetCurrentTabIndex() == 0;
		const bool showLogTab = !showAssetsTab;

		if (showAssetsTab)
		{
			_fileSearchBar->EnableRecursive();
			_assetExplorer->EnableRecursive();
		}
		else
		{
			_fileSearchBar->DisableRecursive();
			_assetExplorer->DisableRecursive();
		}

		if (showLogTab)
		{
			_showDebugCheckbox->EnableRecursive();
			_showInfoCheckbox->EnableRecursive();
			_showWarnCheckbox->EnableRecursive();
			_showCritCheckbox->EnableRecursive();
			_logView->EnableRecursive();
		}
		else
		{
			_showDebugCheckbox->DisableRecursive();
			_showInfoCheckbox->DisableRecursive();
			_showWarnCheckbox->DisableRecursive();
			_showCritCheckbox->DisableRecursive();
			_logView->DisableRecursive();
		}

		Element::Render(renderer, w, h);
	}

	void Explorer::OnLogMessage(const HexEngine::LogMessage& message)
	{
		std::lock_guard<std::recursive_mutex> lock(_pendingLogLock);
		_pendingMessages.push_back(message);
	}
}
