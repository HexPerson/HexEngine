
#include "InteractionComponent.hpp"

#include "../Entity.hpp"
#include "Transform.hpp"
#include "Camera.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Input/InputSystem.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Graphics/IFontResource.hpp"
#include "../../GUI/UIManager.hpp"
#include "../../GUI/GuiRenderer.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ColourPicker.hpp"

namespace HexEngine
{
	namespace
	{
		// Centralised focus state - shared by every InteractionComponent. The
		// look-at raycast runs once per frame (on whichever component ticks
		// first) and writes the result here; SceneRenderer reads GetFocused().
		InteractionComponent* s_focused = nullptr;
		uint64_t              s_focusFrame = static_cast<uint64_t>(-1);

		// One app-lifetime input listener fires the focused interactable's
		// callback on its interact key - a single listener rather than one per
		// component.
		struct InteractionKeyListener : public IInputListener
		{
			bool registered = false;
			virtual bool OnInputEvent(InputEvent event, InputData* data) override
			{
				if (event != InputEvent::KeyDown || data == nullptr)
					return false;
				if (s_focused != nullptr && data->KeyDown.key == s_focused->GetInteractKey())
					s_focused->Callback();
				return false; // don't consume - other listeners still see the key
			}
		};
		InteractionKeyListener s_keyListener;

		void EnsureKeyListener()
		{
			if (s_keyListener.registered)
				return;
			if (g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr)
			{
				g_pEnv->_inputSystem->AddInputListener(&s_keyListener, InputEvent::KeyDown);
				s_keyListener.registered = true;
			}
		}

		// Single forward raycast from the player camera; returns the nearest
		// interactable being looked at, within its range and not occluded.
		InteractionComponent* ResolveFocused()
		{
			if (g_pEnv == nullptr || !g_pEnv->IsGameRunning() || g_pEnv->_sceneManager == nullptr)
				return nullptr;

			auto scene = g_pEnv->_sceneManager->GetCurrentScene();
			if (!scene)
				return nullptr;

			Camera* cam = scene->GetMainCamera();
			if (cam == nullptr || cam->GetEntity() == nullptr)
				return nullptr;

			auto* tf = cam->GetEntity()->GetComponent<Transform>();
			if (tf == nullptr)
				return nullptr;

			const math::Vector3 origin  = tf->GetPosition() + cam->GetViewOffset();
			const math::Vector3 forward = tf->GetForward();

			// One ray serves all interactables; per-component range is enforced
			// on the hit. The cap is generous - the range check rejects far hits.
			const float maxDist = 1000.0f;
			math::Ray ray(origin, forward);
			RayHit hit;
			if (!PhysUtils::RayCast(ray, maxDist,
				LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry), &hit))
				return nullptr;

			if (hit.entity == nullptr)
				return nullptr;

			// GetComponentDerived so subclasses (DoorComponent, ...) are focusable
			// too - GetComponent<> matches the exact id and would miss them.
			auto* ic = hit.entity->GetComponentDerived<InteractionComponent>();
			if (ic == nullptr || !ic->IsEnabled())
				return nullptr;
			if (hit.distance > ic->GetInteractionRange())
				return nullptr;

			return ic;
		}

		std::wstring ToWide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
		std::string  ToNarrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }
	}

	InteractionComponent::InteractionComponent(Entity* entity) :
		UpdateComponent(entity)
	{}

	InteractionComponent::InteractionComponent(Entity* entity, InteractionComponent* clone) :
		UpdateComponent(entity)
	{
		if (clone == nullptr)
			return;
		_interactableName = clone->_interactableName;
		_prompt           = clone->_prompt;
		_interactionRange = clone->_interactionRange;
		_highlightColour  = clone->_highlightColour;
		_outlineThickness = clone->_outlineThickness;
		_enabled          = clone->_enabled;
		_interactKey      = clone->_interactKey;
		_callback         = clone->_callback;
	}

	void InteractionComponent::Destroy()
	{
		if (s_focused == this)
			s_focused = nullptr;
		UpdateComponent::Destroy();
	}

	void InteractionComponent::Update(float frameTime)
	{
		(void)frameTime;
		// Resolve focus exactly once per frame, on whichever InteractionComponent
		// ticks first. ResolveFocused() is global (one camera raycast), so other
		// components' Updates this frame are no-ops.
		const uint64_t frame = (g_pEnv != nullptr && g_pEnv->_timeManager != nullptr)
			? g_pEnv->_timeManager->_frameCount : 0;
		if (frame != s_focusFrame)
		{
			s_focusFrame = frame;
			EnsureKeyListener();
			s_focused = ResolveFocused();
		}
	}

	InteractionComponent* InteractionComponent::GetFocused()
	{
		return s_focused;
	}

	void InteractionComponent::OnGUI()
	{
		// Only the interactable the player is currently looking at draws its
		// label. GetFocused() is gated on IsGameRunning, so this is inert in the
		// editor's free-look camera. Scene::OnGUI calls this inside the active
		// GUI frame, so PrintText lands on screen.
		if (GetFocused() != this)
			return;

		auto* renderer = g_pEnv != nullptr ? g_pEnv->GetUIManager().GetRenderer() : nullptr;
		if (renderer == nullptr)
			return;
		auto* font = renderer->_style.font.get();
		if (font == nullptr)
			return;

		const auto bbvp = g_pEnv->_graphicsDevice->GetBackBufferViewport();
		const int32_t cx = (int32_t)(bbvp.width * 0.5f);
		const int32_t cy = (int32_t)(bbvp.height * 0.60f);

		std::wstring name = ToWide(_interactableName);
		renderer->PrintText(font, (uint8_t)Style::FontSize::Large, cx, cy, _highlightColour,
			FontAlign::CentreLR | FontAlign::CentreUD, name);

		if (!_prompt.empty())
		{
			std::wstring prompt = ToWide(_prompt);
			renderer->PrintText(font, (uint8_t)Style::FontSize::Regular, cx, cy + 24,
				math::Color(0.85f, 0.85f, 0.85f, 1.0f),
				FontAlign::CentreLR | FontAlign::CentreUD, prompt);
		}
	}

	void InteractionComponent::SetCallback(std::function<tInteractionCallback> callback)
	{
		_callback = callback;
	}

	void InteractionComponent::Callback()
	{
		if (_callback)
			_callback(this, GetEntity());
	}

	void InteractionComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_interactableName);
		SERIALIZE_VALUE(_prompt);
		SERIALIZE_VALUE(_interactionRange);
		SERIALIZE_VALUE(_highlightColour);
		SERIALIZE_VALUE(_outlineThickness);
		SERIALIZE_VALUE(_enabled);
		SERIALIZE_VALUE(_interactKey);
	}

	void InteractionComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_interactableName);
		DESERIALIZE_VALUE(_prompt);
		DESERIALIZE_VALUE(_interactionRange);
		DESERIALIZE_VALUE(_highlightColour);
		DESERIALIZE_VALUE(_outlineThickness);
		DESERIALIZE_VALUE(_enabled);
		DESERIALIZE_VALUE(_interactKey);
	}

	bool InteractionComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		LineEdit* nameEdit = new LineEdit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Name");
		nameEdit->SetValue(ToWide(_interactableName));
		nameEdit->SetDoesCallbackWaitForReturn(true);
		nameEdit->SetOnInputFn([this](LineEdit*, const std::wstring& text) { _interactableName = ToNarrow(text); });
		nameEdit->SetPrefabOverrideBinding(GetComponentName(), "/_interactableName");

		LineEdit* promptEdit = new LineEdit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Prompt");
		promptEdit->SetValue(ToWide(_prompt));
		promptEdit->SetDoesCallbackWaitForReturn(true);
		promptEdit->SetOnInputFn([this](LineEdit*, const std::wstring& text) { _prompt = ToNarrow(text); });
		promptEdit->SetPrefabOverrideBinding(GetComponentName(), "/_prompt");

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Range",
			&_interactionRange, 0.0f, 1000.0f, 0.5f);

		ColourPicker* picker = new ColourPicker(widget, widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18), L"Glow Colour", &_highlightColour);
		picker->SetPrefabOverrideBinding(GetComponentName(), "/_highlightColour");

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Outline Thickness",
			&_outlineThickness, 0.0f, 16.0f, 0.1f);

		Checkbox* enabled = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Enabled", &_enabled);
		enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");

		return true;
	}
}
