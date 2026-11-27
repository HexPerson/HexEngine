#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// A walk-through link for the player between two places the navmesh/terrain
// can't join - a subway entrance kiosk on the street and the top of the
// station stairs underground. Step within _radius of this entity and you are
// moved to _targetEntityName (facing along the target's rotation). The link is
// re-armed only once the player has walked out of the radius again, so arriving
// on a portal never bounces you straight back.
class TransitPortalComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TransitPortalComponent);
	DEFINE_COMPONENT_CTOR(TransitPortalComponent);

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	void Disarm() { _armed = false; }

private:
	std::string _targetEntityName;
	float _radius = 1.3f;
	float _heightTolerance = 2.0f;

	bool _armed = true;	// runtime
};
