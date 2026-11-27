

#include "AreaLight.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include <algorithm>

namespace HexEngine
{
	AreaLight::AreaLight(Entity* entity) :
		Light(entity)
	{
		// A tube's shape already spreads the light, so a smaller range than the
		// point-light default reads right for strip lighting.
		_radius = 6.0f;
	}

	AreaLight::AreaLight(Entity* entity, AreaLight* copy) :
		Light(entity, copy)
	{
		// Light(entity, copy) copies only the base state; prefab spawn and
		// CloneEntity go through here, so the shape must be copied too.
		if (copy != nullptr)
		{
			_shape = copy->_shape;
			_width = copy->_width;
			_height = copy->_height;
			_tubeRadius = copy->_tubeRadius;
			_twoSided = copy->_twoSided;
			_texture = copy->_texture;
			_texturePath = copy->_texturePath;
			_textureIsSRGB = copy->_textureIsSRGB;
			_textureIsLive = copy->_textureIsLive;
		}
		_doesCastShadows = false;
	}

	void AreaLight::SetDoesCastShadows(bool enabled)
	{
		// No shadow support yet: never register as a shadow caster.
		Light::SetDoesCastShadows(false);
	}

	void AreaLight::SetWidth(float width)
	{
		_width = std::max(width, 0.01f);
	}

	void AreaLight::SetHeight(float height)
	{
		_height = std::max(height, 0.01f);
	}

	void AreaLight::SetTexture(const std::shared_ptr<ITexture2D>& texture)
	{
		_texture = texture;
		_texturePath = texture ? texture->GetFileSystemPath() : fs::path();
	}

	void AreaLight::SetTubeRadius(float radius)
	{
		_tubeRadius = std::max(radius, 0.0f);
	}

	void AreaLight::GetWorldAxes(math::Vector3& centre, math::Vector3& halfWidthAxis, math::Vector3& halfHeightAxis,
		math::Vector3& facing) const
	{
		const math::Matrix world = GetEntity()->GetWorldTM();
		centre = world.Translation();

		// Directions only - the authored size is in metres regardless of the
		// entity's scale, matching how Radius ignores scale on every light.
		math::Vector3 right = world.Right();
		math::Vector3 up = world.Up();
		math::Vector3 forward = world.Forward();
		right.Normalize();
		up.Normalize();
		forward.Normalize();

		halfWidthAxis = right * (std::max(_width, 0.01f) * 0.5f);
		halfHeightAxis = _shape == Shape::Rect ? up * (std::max(_height, 0.01f) * 0.5f) : math::Vector3::Zero;
		facing = forward;
	}

	float AreaLight::GetBoundingRadius() const
	{
		const float halfW = std::max(_width, 0.01f) * 0.5f;
		const float extent = _shape == Shape::Rect
			? std::sqrt(halfW * halfW + 0.25f * _height * _height)
			: halfW + _tubeRadius;
		return std::max(0.05f, GetRadius()) + extent;
	}

	void AreaLight::OnRenderEditorGizmo(bool isSelected, bool& /*isHovering*/)
	{
		if (GetEntity() == nullptr || g_pEnv == nullptr || g_pEnv->_debugRenderer == nullptr)
			return;

		math::Vector3 centre, halfW, halfH, facing;
		GetWorldAxes(centre, halfW, halfH, facing);

		const auto diffuse = GetDiffuseColour();
		const float a = isSelected ? 0.95f : 0.5f;
		const math::Color colour(diffuse.x, diffuse.y, diffuse.z, a);
		auto* dbg = g_pEnv->_debugRenderer;

		if (_shape == Shape::Tube)
		{
			// The segment, plus ticks showing the tube's thickness at each end.
			dbg->DrawLine(centre - halfW, centre + halfW, colour);
			math::Vector3 up = GetEntity()->GetWorldTM().Up();
			up.Normalize();
			const math::Vector3 tick = up * std::max(_tubeRadius, 0.02f);
			dbg->DrawLine(centre - halfW - tick, centre - halfW + tick, colour);
			dbg->DrawLine(centre + halfW - tick, centre + halfW + tick, colour);
			return;
		}

		const math::Vector3 c0 = centre - halfW - halfH;
		const math::Vector3 c1 = centre + halfW - halfH;
		const math::Vector3 c2 = centre + halfW + halfH;
		const math::Vector3 c3 = centre - halfW + halfH;
		dbg->DrawLine(c0, c1, colour);
		dbg->DrawLine(c1, c2, colour);
		dbg->DrawLine(c2, c3, colour);
		dbg->DrawLine(c3, c0, colour);

		// Emission direction(s).
		const float arrow = std::max(0.15f, 0.5f * std::min(halfW.Length(), halfH.Length()));
		dbg->DrawLine(centre, centre + facing * arrow, colour);
		if (_twoSided)
			dbg->DrawLine(centre, centre - facing * arrow, colour);
	}

	bool AreaLight::CreateWidget(ComponentWidget* widget)
	{
		// Light::CreateWidget minus "Casts shadows" (unsupported) and "Inject Into
		// GI" (area lights don't feed the voxel GI yet).
		const HVar* physUnits = g_pEnv->_commandManager->FindHVar("r_physicalLightUnits");
		const bool physical = physUnits != nullptr && physUnits->_val.b;

		DragFloat* strength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18),
			physical ? L"Luminous flux (lm)" : L"Strength", &_strength,
			physical ? 25.0f : 0.1f, physical ? 100000.0f : 500.0f, 0.0f);
		strength->SetPrefabOverrideBinding(GetComponentName(), "/_strength");

		DragFloat* radius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18),
			L"Range", &_radius, 0.1f, 1000.0f, 0.1f);
		radius->SetPrefabOverrideBinding(GetComponentName(), "/_radius");

		ColourPicker* picker = new ColourPicker(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Colour", &_diffuseColour);
		picker->SetPrefabOverrideBinding(GetComponentName(), "/_diffuseColour");

		DropDown* shape = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Shape");
		shape->SetValue(_shape == Shape::Rect ? L"Rectangle" : L"Tube");
		shape->SetPrefabOverrideBinding(GetComponentName(), "/_shape");
		shape->GetContextMenu()->AddItem(new ContextItem(L"Tube", [this](const std::wstring&) { SetShape(Shape::Tube); }));
		shape->GetContextMenu()->AddItem(new ContextItem(L"Rectangle", [this](const std::wstring&) { SetShape(Shape::Rect); }));

		DragFloat* width = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18),
			L"Length / width (m)", &_width, 0.01f, 100.0f, 0.01f);
		width->SetOnDrag([this](float v, float, float) { SetWidth(v); });
		width->SetPrefabOverrideBinding(GetComponentName(), "/_width");

		DragFloat* height = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18),
			L"Rect height (m)", &_height, 0.01f, 100.0f, 0.01f);
		height->SetOnDrag([this](float v, float, float) { SetHeight(v); });
		height->SetPrefabOverrideBinding(GetComponentName(), "/_height");

		DragFloat* tubeRadius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18),
			L"Tube radius (m)", &_tubeRadius, 0.0f, 2.0f, 0.005f, 3);
		tubeRadius->SetOnDrag([this](float v, float, float) { SetTubeRadius(v); });
		tubeRadius->SetPrefabOverrideBinding(GetComponentName(), "/_tubeRadius");

		Checkbox* twoSided = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rect two-sided", &_twoSided);
		twoSided->SetPrefabOverrideBinding(GetComponentName(), "/_twoSided");

		// Rect image - same AssetSearch picker as DecalComponent's texture slots.
		auto* image = new AssetSearch(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 84),
			L"Rect image",
			{ ResourceType::Image },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				if (chosen.empty())
				{
					SetTexture(nullptr);
					return;
				}
				if (auto texture = ITexture2D::Create(chosen); texture)
				{
					_texture = texture;
					_texturePath = chosen;
				}
			});
		if (_texture && !_texturePath.empty())
			image->SetValue(_texturePath.wstring());

		Checkbox* imageSrgb = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Image is sRGB", &_textureIsSRGB);
		imageSrgb->SetPrefabOverrideBinding(GetComponentName(), "/_textureIsSRGB");

		Checkbox* imageLive = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Live image (re-sample every frame)", &_textureIsLive);
		imageLive->SetPrefabOverrideBinding(GetComponentName(), "/_textureIsLive");

		DropDown* effect = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Effect");
		effect->SetPrefabOverrideBinding(GetComponentName(), "/_effect");
		effect->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Light::SetLightingEffect, this, LightingEffect::None)));
		effect->GetContextMenu()->AddItem(new ContextItem(L"Slow random pulse", std::bind(&Light::SetLightingEffect, this, LightingEffect::SlowRandomPulse)));

		return true;
	}

	void AreaLight::Serialize(json& data, JsonFile* file)
	{
		Light::Serialize(data, file);

		SERIALIZE_VALUE(_shape);
		SERIALIZE_VALUE(_width);
		SERIALIZE_VALUE(_height);
		SERIALIZE_VALUE(_tubeRadius);
		SERIALIZE_VALUE(_twoSided);
		SERIALIZE_VALUE(_textureIsSRGB);
		SERIALIZE_VALUE(_textureIsLive);
		std::string texturePath = _texturePath.string();
		file->Serialize(data, "_texturePath", texturePath);
	}

	void AreaLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Light::Deserialize(data, file, mask);

		DESERIALIZE_VALUE(_shape);
		DESERIALIZE_VALUE(_width);
		DESERIALIZE_VALUE(_height);
		DESERIALIZE_VALUE(_tubeRadius);
		DESERIALIZE_VALUE(_twoSided);
		DESERIALIZE_VALUE(_textureIsSRGB);
		DESERIALIZE_VALUE(_textureIsLive);

		std::string texturePath;
		file->Deserialize(data, "_texturePath", texturePath);
		_texture.reset();
		_texturePath = texturePath;
		if (!texturePath.empty())
		{
			_texture = ITexture2D::Create(_texturePath);
			if (_texture == nullptr)
				LOG_WARN("AreaLight: couldn't load image '%s'", texturePath.c_str());
		}

		_width = std::max(_width, 0.01f);
		_height = std::max(_height, 0.01f);
		_tubeRadius = std::max(_tubeRadius, 0.0f);
		_doesCastShadows = false;
	}
}
