

#include "Light.hpp"
#include "../Entity.hpp"
#include "UpdateComponent.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	extern HVar r_shadowCascadeRange;

	Light::Light(Entity* entity) :
		BaseComponent(entity),
		_diffuseColour(1, 1, 1, 1)
	{}

	Light::Light(Entity* entity, Light* clone) :
		BaseComponent(entity)
	{
		_doesCastShadows			= clone->_doesCastShadows;
		_isVolumetric				= clone->_isVolumetric;
		_injectIntoGI				= clone->_injectIntoGI;
		_effect						= clone->_effect;
		_strength					= clone->_strength;
		_originalStrength			= clone->_originalStrength;
		_diffuseColour				= clone->_diffuseColour;
		_slowRandomPulseEffect		= clone->_slowRandomPulseEffect;
		_radius						= clone->_radius;
	}

	void Light::SetLightingEffect(LightingEffect effect)
	{
		if (effect != LightingEffect::None)
		{
			_originalStrength = _strength;
			// Add an updatable component if there isn't one
			//
			if (GetEntity()->HasA<UpdateComponent>() == false)
			{
				GetEntity()->AddComponent<UpdateComponent>();
			}
		}
		else
			_strength = _originalStrength;

		_effect = effect;
	}

	void Light::SetDoesCastShadows(bool enabled)
	{
		_doesCastShadows = enabled;
	}

	/// <summary>
	/// Get the maximum number of supported shadow cascades for shadow mapping
	/// </summary>
	/// <returns></returns>
	int32_t Light::GetMaxSupportedShadowCascades() const
	{
		return 1;
	}

	float Light::GetRadius() const
	{
		return _radius;
	}

	void Light::SetRadius(float radius)
	{
		_radius = radius;
	}

	void Light::OnMessage(Message* message, MessageListener* sender)
	{
		if (message->_id == MessageId::Updated)
		{
			switch (_effect)
			{
				case LightingEffect::SlowRandomPulse:
				{
					if (fabs(_slowRandomPulseEffect.current- _slowRandomPulseEffect.target) < 0.01f)
					{
						_slowRandomPulseEffect.target = GetRandomFloat(_originalStrength * 0.25f, _originalStrength);
					}

					float dir = _slowRandomPulseEffect.target > _slowRandomPulseEffect.current ? 1.0f : -1.0f;

					_slowRandomPulseEffect.current += /*(_slowRandomPulseEffect.target - _slowRandomPulseEffect.current)*/dir * g_pEnv->_timeManager->GetFrameTime() * 1.4f;
					_strength = std::clamp(_slowRandomPulseEffect.current, 0.0f, _originalStrength);
					break;
				}
			}
		}
		else if (message->_id == MessageId::TransformChanged)
		{
			if (GetDoesCastShadows())
			{
				SetFlag(LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS);
			}
		}
	}

	LightFlags Light::GetFlags() const
	{
		return _flags;
	}

	void Light::SetFlag(LightFlags flag)
	{
		_flags |= flag;
	}

	void Light::ClearFlag(LightFlags flag)
	{
		_flags &= ~flag;
	}

	void Light::SetDiffuseColour(const math::Color& colour)
	{
		_diffuseColour = colour.ToVector4();
	}

	math::Vector4 Light::GetDiffuseColour() const
	{
		math::Vector4 diffuse = _diffuseColour;

		diffuse.w *= _strength;

		return diffuse;
	}

	void Light::SetLightMultiplier(float value)
	{
		_lightMultiplier = std::clamp(value, 0.0f, 1.0f);
	}

	float Light::GetLightMultiplier() const
	{
		return _lightMultiplier;
	}

	void Light::SetLightStength(float value)
	{
		_strength = value;
		_originalStrength = value;
	}

	float Light::GetLightStrength() const
	{
		return _strength;
	}

	void Light::SetIsVolumetric(bool volumetric)
	{
		_isVolumetric = volumetric;
	}

	bool Light::GetIsVolumetric() const
	{
		return _isVolumetric;
	}

	void Light::SetInjectIntoGI(bool injectIntoGI)
	{
		_injectIntoGI = injectIntoGI;
	}

	bool Light::GetInjectIntoGI() const
	{
		return _injectIntoGI;
	}

	PVS* Light::GetPVS(int32_t index)
	{
		return &_pvs[index];
	}

	// we just assume that the light doesn't support previous matrix for jittering by default
	const math::Matrix& Light::GetViewMatrixPrev(uint32_t index) const
	{
		return GetViewMatrix(index);
	}

	const math::Matrix& Light::GetProjectionMatrixPrev(uint32_t index ) const
	{
		return GetProjectionMatrix(index);
	}

	void Light::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_diffuseColour);
		SERIALIZE_VALUE(_effect);
		SERIALIZE_VALUE(_strength);
		SERIALIZE_VALUE(_radius);
		SERIALIZE_VALUE(_doesCastShadows);
		SERIALIZE_VALUE(_injectIntoGI);
		SERIALIZE_VALUE(_lightMultiplier);
	}

	void Light::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_diffuseColour);
		DESERIALIZE_VALUE(_effect);
		DESERIALIZE_VALUE(_strength);
		DESERIALIZE_VALUE(_radius);
		DESERIALIZE_VALUE(_doesCastShadows);
		DESERIALIZE_VALUE(_injectIntoGI);
		DESERIALIZE_VALUE(_lightMultiplier);

		_originalStrength = _strength;

		LOG_DEBUG("Loaded light params for %s, strength = %f", GetEntity()->GetName().c_str(), _strength);

		SetLightingEffect(_effect);
		SetDoesCastShadows(_doesCastShadows);
	}

	bool Light::CreateWidget(ComponentWidget* widget)
	{
		Checkbox* castsShadows = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Casts shadows", &_doesCastShadows);
		//castsShadows->SetLabelMinSize(130);
		castsShadows->SetOnCheckFn(std::bind(&Light::SetDoesCastShadows, this, std::placeholders::_2));
		castsShadows->SetPrefabOverrideBinding(GetComponentName(), "/_doesCastShadows");

		Checkbox* injectGI = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Inject Into GI", &_injectIntoGI);
		injectGI->SetOnCheckFn(std::bind(&Light::SetInjectIntoGI, this, std::placeholders::_2));
		injectGI->SetPrefabOverrideBinding(GetComponentName(), "/_injectIntoGI");

		DragFloat* strength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Strength", &_strength, 0.1f, 500.0f, 0.0f);
		//strength->SetLabelMinSize(130);
		strength->SetPrefabOverrideBinding(GetComponentName(), "/_strength");

		DragFloat* radius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Radius", &_radius, 2.0f, 1000.0f, 0.1f);
		//radius->SetLabelMinSize(130);
		radius->SetPrefabOverrideBinding(GetComponentName(), "/_radius");

		ColourPicker* picker = new ColourPicker(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Colour", &_diffuseColour);
		picker->SetPrefabOverrideBinding(GetComponentName(), "/_diffuseColour");

		//// emission strength
		//DragFloat* r = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Red", &_diffuseColour.x, 0.0f, 1.0f, 0.1f);
		//r->SetLabelMinSize(130);
		//DragFloat* g = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Green", &_diffuseColour.y, 0.0f, 1.0f, 0.1f);
		//g->SetLabelMinSize(130);
		//DragFloat* b = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Blue", &_diffuseColour.z, 0.0f, 1.0f, 0.1f);
		//b->SetLabelMinSize(130);

		DropDown* state = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Effect");
		state->SetPrefabOverrideBinding(GetComponentName(), "/_effect");

		state->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Light::SetLightingEffect, this, LightingEffect::None)));
		state->GetContextMenu()->AddItem(new ContextItem(L"Slow random pulse", std::bind(&Light::SetLightingEffect, this, LightingEffect::SlowRandomPulse)));

		return true;
	}
}
