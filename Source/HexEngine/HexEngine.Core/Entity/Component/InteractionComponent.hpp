
#pragma once

#include "UpdateComponent.hpp"
#include <string>

namespace HexEngine
{
	class InteractionComponent;
	class ComponentWidget;

	using tInteractionCallback = void(InteractionComponent*, Entity*);

	// A look-at interactable. While the game is running, if the player's view
	// centre rests on this entity's static mesh within _interactionRange (and
	// it isn't occluded by nearer geometry), it becomes the "focused"
	// interactable: SceneRenderer draws an SDF outline glow around its mesh and
	// shows _interactableName on screen, and pressing _interactKey fires the
	// callback. Focus is resolved centrally - one camera raycast per frame -
	// rather than every component polling independently.
	class HEX_API InteractionComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(InteractionComponent);
		DEFINE_COMPONENT_CTOR(InteractionComponent);

		virtual void Destroy() override;
		virtual void Update(float frameTime) override;
		// Draws the on-screen name + prompt label while this is the focused
		// interactable. Called inside the active GUI frame (Scene::OnGUI), so it
		// naturally lives here rather than in the renderer.
		virtual void OnGUI() override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		void SetCallback(std::function<tInteractionCallback> callback);
		// Virtual so subclasses (e.g. DoorComponent) can react to interaction
		// directly. The interact-key listener calls this through the base pointer.
		virtual void Callback();

		const std::string& GetInteractableName() const { return _interactableName; }
		const std::string& GetPrompt() const { return _prompt; }
		float GetInteractionRange() const { return _interactionRange; }
		const math::Color& GetHighlightColour() const { return _highlightColour; }
		float GetOutlineThickness() const { return _outlineThickness; }
		bool IsEnabled() const { return _enabled; }
		int32_t GetInteractKey() const { return _interactKey; }

		// The interactable the player is currently looking at (nearest, within
		// range, unoccluded), or nullptr. Resolved once per frame; read by
		// SceneRenderer for the outline glow + on-screen name label.
		static InteractionComponent* GetFocused();

	private:
		std::string _interactableName = "Item";
		std::string _prompt           = "Press E to interact";
		float       _interactionRange = 30.0f;
		math::Color _highlightColour  = math::Color(1.0f, 0.85f, 0.3f, 1.0f);
		float       _outlineThickness = 3.0f;
		bool        _enabled          = true;
		int32_t     _interactKey      = 'E';

		std::function<tInteractionCallback> _callback;
	};
}
