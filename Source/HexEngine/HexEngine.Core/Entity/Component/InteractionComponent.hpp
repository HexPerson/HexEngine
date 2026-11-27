
#pragma once

#include "UpdateComponent.hpp"
#include <string>

namespace HexEngine
{
	class InteractionComponent;
	class ComponentWidget;
	class HVar;

	// Global interaction look + reach, shared by every interactable (doors, NPCs,
	// vehicles, lockers...). Console/scene cvars, saved with the scene:
	//   i_interactRange  - metres from the eye the player must be to use one
	//   i_glowColour     - outline glow + name label colour ("r g b", linear)
	//   i_glowThickness  - outline glow thickness in pixels
	extern HEX_API HVar i_interactRange;
	extern HEX_API HVar i_glowColour;
	extern HEX_API HVar i_glowThickness;

	using tInteractionCallback = void(InteractionComponent*, Entity*);

	// A look-at interactable. While the game is running, if the player's view
	// centre rests on this entity's static mesh within i_interactRange (and
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
		// Global (the i_* cvars above), not per interactable - except the
		// editor drag-hover override, which draws in its own colour.
		float GetInteractionRange() const;
		math::Color GetHighlightColour() const;
		float GetOutlineThickness() const;
		bool IsEnabled() const { return _enabled; }
		int32_t GetInteractKey() const { return _interactKey; }

		// The interactable the player is currently looking at (nearest, within
		// range, unoccluded), or nullptr. Resolved once per frame; read by
		// SceneRenderer for the outline glow + on-screen name label.
		static InteractionComponent* GetFocused();

		// Editor-only: force this component to be the focused interactable so the
		// outline glow draws without the game running (e.g. material drag-hover
		// highlight in the scene view). Takes precedence over look-at focus until
		// cleared with nullptr. Cleared automatically if the component is destroyed.
		static void SetEditorFocusOverride(InteractionComponent* ic);

	private:
		std::string _interactableName = "Item";
		std::string _prompt           = "Press E to interact";
		bool        _enabled          = true;
		int32_t     _interactKey      = 'E';

		std::function<tInteractionCallback> _callback;
	};
}
