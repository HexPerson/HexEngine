#include "EntitySearch.hpp"
#include "../UIManager.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace HexEngine
{
	namespace
	{
		constexpr int32_t kRowHeight = 24;
		constexpr int32_t kPopupVerticalOffset = 2;
		constexpr int32_t kSearchBarHeight = 20;
		constexpr int32_t kPickButtonWidth = 44;
	}

	class EntitySearch::EntitySearchRow final : public Element
	{
	public:
		EntitySearchRow(EntitySearch* owner, Element* parent, const Point& position, const Point& size, size_t rowIndex) :
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

			if (_canvas.BeginDraw(renderer, size.x, size.y))
			{
				const bool hovered = IsMouseOver(true);
				const auto& result = _owner->_results[_rowIndex];

				if (hovered || highlighted)
				{
					renderer->FillQuad(
						0,
						0,
						_size.x,
						_size.y,
						hovered ? renderer->_style.button_hover : math::Color(HEX_RGBA_TO_FLOAT4(70, 80, 95, 255)));
				}

				renderer->PrintText(
					renderer->_style.font.get(),
					(uint8_t)Style::FontSize::Tiny,
					6,
					4,
					renderer->_style.text_regular,
					FontAlign::None,
					result.displayName);

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
			}
			else if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && IsMouseOver(true))
			{
				_owner->OnPickResult(_rowIndex);
				return true;
			}

			return false;
		}

	private:
		EntitySearch* _owner = nullptr;
		size_t _rowIndex = 0;
	};

	EntitySearch::EntitySearch(
		Element* parent,
		const Point& position,
		const Point& size,
		const std::wstring& label,
		OnSelectFn onSelect) :
		Element(parent, position, size),
		_onSelect(onSelect)
	{
		const int32_t editHeight = std::min(kSearchBarHeight, std::max(16, size.y));
		_edit = new LineEdit(this, Point(0, 0), Point(size.x - kPickButtonWidth - 4, editHeight), label);
		_edit->SetIcon(ITexture2D::Create("EngineData.Textures/UI/magnifying_glass.png"), math::Color(HEX_RGBA_TO_FLOAT4(180, 180, 180, 255)));
		_edit->SetOnInputFn(std::bind(&EntitySearch::OnSearchTextChanged, this, std::placeholders::_1, std::placeholders::_2));
		_edit->SetDoesCallbackWaitForReturn(false);

		_pickButton = new Button(this, Point(size.x - kPickButtonWidth, 0), Point(kPickButtonWidth, editHeight), L"Pick",
			[this](Button* button) -> bool
			{
				SetPickMode(!_pickMode);
				return true;
			});
	}

	EntitySearch::~EntitySearch()
	{
		ClosePopup();
	}

	void EntitySearch::SetOnSelectFn(OnSelectFn fn)
	{
		_onSelect = fn;
	}

	void EntitySearch::SetOnInputFn(OnInputFn fn)
	{
		_onInput = fn;
	}

	void EntitySearch::SetValue(const std::wstring& value)
	{
		if (_edit != nullptr)
		{
			_edit->SetValue(value);
		}

		_selectedResult = {};
		_hasSelection = false;

		auto scene = g_pEnv->_sceneManager != nullptr ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (scene == nullptr)
			return;

		const std::string name = ws2s(value);
		auto* entity = scene->GetEntityByName(name);
		if (entity != nullptr && !entity->IsPendingDeletion())
		{
			_selectedResult.entity = entity;
			_selectedResult.entityName = name;
			_selectedResult.displayName = value;
			_hasSelection = true;
		}
	}

	const std::wstring& EntitySearch::GetValue() const
	{
		static const std::wstring kEmpty;
		return _edit != nullptr ? _edit->GetValue() : kEmpty;
	}

	void EntitySearch::SetPrefabOverrideBinding(const std::string& componentName, const std::string& jsonPointer)
	{
		if (_edit != nullptr)
		{
			_edit->SetPrefabOverrideBinding(componentName, jsonPointer);
		}
	}

	bool EntitySearch::GetSelectedResult(EntitySearchResult& outResult) const
	{
		if (!_hasSelection)
			return false;

		outResult = _selectedResult;
		return true;
	}

	void EntitySearch::ClearSelection()
	{
		_hasSelection = false;
		_selectedResult = {};
	}

	void EntitySearch::RefreshResults()
	{
		OnSearchTextChanged(_edit, _edit != nullptr ? _edit->GetValue() : L"");
	}

	bool EntitySearch::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_pickMode && event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			const bool clickedThisControl = (_edit != nullptr && _edit->IsMouseOver(true))
				|| (_pickButton != nullptr && _pickButton->IsMouseOver(true))
				|| (_popup != nullptr && _popup->IsMouseOver(true));

			if (!clickedThisControl)
			{
				EntitySearchResult picked;
				if (TryPickEntityFromScene(picked))
				{
					_selectedResult = picked;
					_hasSelection = true;
					if (_edit != nullptr)
					{
						_edit->SetValue(_selectedResult.displayName);
					}

					SetPickMode(false);
					ClosePopup();

					if (_onInput)
					{
						_onInput(this, _selectedResult.displayName);
					}
					if (_onSelect)
					{
						_onSelect(this, _selectedResult);
					}
					return true;
				}
			}
		}
		else if (_pickMode && event == InputEvent::KeyDown && data->KeyDown.key == VK_ESCAPE)
		{
			SetPickMode(false);
			return true;
		}

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

		return Element::OnInputEvent(event, data);
	}

	int32_t EntitySearch::GetLabelWidth() const
	{
		return _edit != nullptr ? _edit->GetLabelWidth() : 0;
	}

	void EntitySearch::SetLabelMinSize(int32_t minSize)
	{
		if (_edit != nullptr)
		{
			_edit->SetLabelMinSize(minSize);
		}
	}

	void EntitySearch::OnSearchTextChanged(LineEdit* edit, const std::wstring& value)
	{
		(void)edit;
		if (_onInput)
		{
			_onInput(this, value);
		}

		_results.clear();
		RunDefaultQuery(value, _results);

		std::sort(_results.begin(), _results.end(),
			[](const EntitySearchResult& a, const EntitySearchResult& b)
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

	void EntitySearch::OnPickResult(size_t index)
	{
		if (index >= _results.size() || _edit == nullptr)
			return;

		_highlightedIndex = index;
		_selectedResult = _results[index];
		_hasSelection = true;
		_edit->SetValue(_selectedResult.displayName);
		ClosePopup();

		if (_onSelect)
		{
			_onSelect(this, _selectedResult);
		}
	}

	void EntitySearch::OpenPopup()
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

	void EntitySearch::ClosePopup()
	{
		if (_popup != nullptr)
		{
			_popup->DeleteMe();
			_popup = nullptr;
		}
	}

	void EntitySearch::RebuildPopupRows()
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

	bool EntitySearch::IsPopupOpen() const
	{
		return _popup != nullptr;
	}

	void EntitySearch::SetPickMode(bool enabled)
	{
		_pickMode = enabled;
		if (_pickButton == nullptr)
			return;

		if (_pickMode)
		{
			_pickButton->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(76, 175, 80, 255)));
		}
		else
		{
			_pickButton->RemoveHighlightOverride();
		}
	}

	bool EntitySearch::TryPickEntityFromScene(EntitySearchResult& outResult) const
	{
		auto scene = g_pEnv->_sceneManager != nullptr ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (scene == nullptr)
			return false;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr)
			return false;

		int32_t mx = 0;
		int32_t my = 0;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);
		const math::Vector3 rayDir = g_pEnv->_inputSystem->GetScreenToWorldRay(camera, mx, my);
		if (rayDir.LengthSquared() <= 0.000001f)
			return false;

		math::Ray ray;
		ray.position = camera->GetEntity()->GetPosition();
		ray.direction = rayDir;

		RayHit hit;
		if (!PhysUtils::RayCast(
			ray,
			camera->GetFarZ(),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry),
			&hit))
		{
			return false;
		}

		if (hit.entity == nullptr || hit.entity->IsPendingDeletion())
			return false;

		outResult = {};
		outResult.entity = hit.entity;
		outResult.entityName = hit.entity->GetName();
		outResult.displayName = std::wstring(outResult.entityName.begin(), outResult.entityName.end());
		return true;
	}

	void EntitySearch::RunDefaultQuery(const std::wstring& filter, std::vector<EntitySearchResult>& outResults) const
	{
		auto scene = g_pEnv->_sceneManager != nullptr ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (scene == nullptr)
			return;

		const std::wstring loweredFilter = ToLowerCopy(filter);
		std::unordered_set<Entity*> uniqueEntities;
		for (const auto& group : scene->GetEntities())
		{
			for (const auto& entityRef : group.second)
			{
				auto entity = entityRef;
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				if (!uniqueEntities.insert(entity).second)
					continue;

				const std::wstring entityName(entity->GetName().begin(), entity->GetName().end());
				const std::wstring searchable = ToLowerCopy(entityName);
				if (!loweredFilter.empty() && searchable.find(loweredFilter) == std::wstring::npos)
					continue;

				EntitySearchResult result;
				result.entity = entity;
				result.entityName = entity->GetName();
				result.displayName = entityName;
				outResults.push_back(std::move(result));

				if (outResults.size() >= _maxResults)
					return;
			}
		}
	}

	std::wstring EntitySearch::ToLowerCopy(const std::wstring& value)
	{
		std::wstring lowered = value;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](wchar_t c) { return (wchar_t)towlower(c); });
		return lowered;
	}

	EntitySearch::EntitySearchRow* EntitySearch::CreateRow(Element* parent, int32_t y, size_t index)
	{
		return new EntitySearchRow(this, parent, Point(0, y), Point(_popup->GetSize().x - 12, kRowHeight), index);
	}
}
