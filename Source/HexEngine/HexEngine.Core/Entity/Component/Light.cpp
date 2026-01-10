

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
		_effect						= clone->_effect;
		_strengthMultiplier			= clone->_strengthMultiplier;
		_originalStrengthMultiplier = clone->_originalStrengthMultiplier;
		_diffuseColour				= clone->_diffuseColour;
		_slowRandomPulseEffect		= clone->_slowRandomPulseEffect;
		_radius						= clone->_radius;
	}

	void Light::SetLightingEffect(LightingEffect effect)
	{
		if (effect != LightingEffect::None)
		{
			_originalStrengthMultiplier = _strengthMultiplier;
			// Add an updatable component if there isn't one
			//
			if (GetEntity()->HasA<UpdateComponent>() == false)
			{
				GetEntity()->AddComponent<UpdateComponent>();
			}
		}
		else
			_strengthMultiplier = _originalStrengthMultiplier;

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
						_slowRandomPulseEffect.target = GetRandomFloat(_originalStrengthMultiplier * 0.25f, _originalStrengthMultiplier);
					}

					float dir = _slowRandomPulseEffect.target > _slowRandomPulseEffect.current ? 1.0f : -1.0f;

					_slowRandomPulseEffect.current += /*(_slowRandomPulseEffect.target - _slowRandomPulseEffect.current)*/dir * g_pEnv->_timeManager->GetFrameTime() * 1.4f;
					_strengthMultiplier = std::clamp(_slowRandomPulseEffect.current, 0.0f, _originalStrengthMultiplier);
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

		diffuse.w *= _strengthMultiplier;

		return diffuse;
	}

	/*void Light::SetLightMultiplier(float value)
	{
		_lightMultiplier = std::clamp(value, 0.0f, 1.0f);
	}

	float Light::GetLightMultiplier()
	{
		return _lightMultiplier;
	}*/

	void Light::SetLightStength(float value)
	{
		_strengthMultiplier = value;
		_originalStrengthMultiplier = value;
	}

	float Light::GetLightStrength() const
	{
		return _strengthMultiplier;
	}

	void Light::SetIsVolumetric(bool volumetric)
	{
		_isVolumetric = volumetric;
	}

	bool Light::GetIsVolumetric() const
	{
		return _isVolumetric;
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
		SERIALIZE_VALUE(_strengthMultiplier);
		SERIALIZE_VALUE(_radius);
		SERIALIZE_VALUE(_doesCastShadows);
	}

	void Light::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_diffuseColour);
		DESERIALIZE_VALUE(_effect);
		DESERIALIZE_VALUE(_strengthMultiplier);
		DESERIALIZE_VALUE(_radius);
		DESERIALIZE_VALUE(_doesCastShadows);

		_originalStrengthMultiplier = _strengthMultiplier;

		LOG_DEBUG("Loaded light params for %s, strength = %f", GetEntity()->GetName().c_str(), _strengthMultiplier);

		SetLightingEffect(_effect);
		SetDoesCastShadows(_doesCastShadows);
	}

	bool Light::CreateWidget(ComponentWidget* widget)
	{
		Checkbox* castsShadows = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Casts shadows", &_doesCastShadows);
		//castsShadows->SetLabelMinSize(130);
		castsShadows->SetOnCheckFn(std::bind(&Light::SetDoesCastShadows, this, std::placeholders::_2));

		DragFloat* strength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Strength", &_strengthMultiplier, 0.1f, 500.0f, 0.1f);
		//strength->SetLabelMinSize(130);

		DragFloat* radius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"Radius", &_radius, 2.0f, 1000.0f, 0.1f);
		//radius->SetLabelMinSize(130);

		//ColourPicker* picker = new ColourPicker(widget, Point(0, 0), Point(0, 0), L"");

		// emission strength
		DragFloat* r = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Red", &_diffuseColour.x, 0.0f, 1.0f, 0.1f);
		r->SetLabelMinSize(130);
		DragFloat* g = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Green", &_diffuseColour.y, 0.0f, 1.0f, 0.1f);
		g->SetLabelMinSize(130);
		DragFloat* b = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Blue", &_diffuseColour.z, 0.0f, 1.0f, 0.1f);
		b->SetLabelMinSize(130);

		DropDown* state = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 40, 18), L"Effect");

		state->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&Light::SetLightingEffect, this, LightingEffect::None)));
		state->GetContextMenu()->AddItem(new ContextItem(L"Slow random pulse", std::bind(&Light::SetLightingEffect, this, LightingEffect::SlowRandomPulse)));

		return true;
	}
}