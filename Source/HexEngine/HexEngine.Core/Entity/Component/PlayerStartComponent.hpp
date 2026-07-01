

#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	// Marks where the player spawns when the game is run. Place one in the scene
	// (Editor: Scene -> Player Start); on game start the engine moves the main
	// camera to this entity's transform - position AND facing - before the game
	// extension's OnCreateGame runs, so a game that just adds a camera controller
	// to the main camera begins play here. A game that wants a bespoke spawn can
	// still override it in OnCreateGame (which runs afterwards).
	//
	// Pure marker: no per-frame logic, no mesh. Its only runtime job is to exist
	// so the spawn code can find it via Scene::GetComponents<PlayerStartComponent>.
	// The editor draws a gizmo (box + forward arrow) so the facing is visible.
	class HEX_API PlayerStartComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(PlayerStartComponent);
		DEFINE_COMPONENT_CTOR(PlayerStartComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		// When more than one Player Start exists in a scene, the spawn picks the
		// first one flagged primary; if none is flagged it falls back to the
		// first found. Lets a level keep several candidate spawns and choose
		// between them without deleting the others.
		bool IsPrimary() const { return _primary; }
		void SetPrimary(bool v) { _primary = v; }

	private:
		bool _primary = true;
	};
}
