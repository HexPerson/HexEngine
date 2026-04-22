#include "ParticleSystemComponent.hpp"

#include "../Entity.hpp"
#include "Transform.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../FileSystem/ResourceSystem.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../Scene/ParticleEffect.hpp"
#include "../../Scene/ParticleWorldSystem.hpp"
#include "../../Graphics/DebugRenderer.hpp"

namespace HexEngine
{
	ParticleSystemComponent::ParticleSystemComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		GetEntity()->SetLayer(Layer::Particle);
	}

	ParticleSystemComponent::ParticleSystemComponent(Entity* entity, ParticleSystemComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy != nullptr)
		{
			_effect = copy->_effect;
			_effectPath = copy->_effectPath;
			_autoPlay = copy->_autoPlay;
			_playing = copy->_playing;
			_paused = copy->_paused;
			_prewarmOverride = copy->_prewarmOverride;
			_localSpaceOverrideEnabled = copy->_localSpaceOverrideEnabled;
			_localSpaceOverride = copy->_localSpaceOverride;
			_receiveLighting = copy->_receiveLighting;
			_timeScale = copy->_timeScale;
		}
	}

	void ParticleSystemComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (!_playing || _paused)
			return;

		if (g_pEnv != nullptr && g_pEnv->_particleWorldSystem != nullptr)
		{
			g_pEnv->_particleWorldSystem->TickComponent(this, frameTime);
		}
	}

	void ParticleSystemComponent::Destroy()
	{
		if (g_pEnv != nullptr && g_pEnv->_particleWorldSystem != nullptr)
		{
			g_pEnv->_particleWorldSystem->OnComponentDestroyed(this);
		}
		_effect.reset();
	}

	void ParticleSystemComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_effectPath);
		SERIALIZE_VALUE(_autoPlay);
		SERIALIZE_VALUE(_playing);
		SERIALIZE_VALUE(_paused);
		SERIALIZE_VALUE(_prewarmOverride);
		SERIALIZE_VALUE(_localSpaceOverrideEnabled);
		SERIALIZE_VALUE(_localSpaceOverride);
		SERIALIZE_VALUE(_receiveLighting);
		SERIALIZE_VALUE(_timeScale);
	}

	void ParticleSystemComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		_serializationState = BaseComponent::SerializationState::Deserializing;
		DESERIALIZE_VALUE(_effectPath);
		DESERIALIZE_VALUE(_autoPlay);
		DESERIALIZE_VALUE(_playing);
		DESERIALIZE_VALUE(_paused);
		DESERIALIZE_VALUE(_prewarmOverride);
		DESERIALIZE_VALUE(_localSpaceOverrideEnabled);
		DESERIALIZE_VALUE(_localSpaceOverride);
		DESERIALIZE_VALUE(_receiveLighting);
		DESERIALIZE_VALUE(_timeScale);
		_serializationState = BaseComponent::SerializationState::Ready;

		if (!_effectPath.empty())
		{
			SetEffectPath(_effectPath);
		}
	}

	bool ParticleSystemComponent::CreateWidget(ComponentWidget* widget)
	{
		AssetSearch* effectPath = new AssetSearch(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 84),
			L"Effect",
			{ ResourceType::ParticleEffect },
			std::bind(&ParticleSystemComponent::OnPickEffect, this, std::placeholders::_1, std::placeholders::_2));
		effectPath->SetPrefabOverrideBinding(GetComponentName(), "/_effectPath");
		if (!_effectPath.empty())
			effectPath->SetValue(_effectPath.wstring());

		Checkbox* autoPlay = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Auto Play", &_autoPlay);
		autoPlay->SetPrefabOverrideBinding(GetComponentName(), "/_autoPlay");
		Checkbox* prewarm = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Prewarm Override", &_prewarmOverride);
		prewarm->SetPrefabOverrideBinding(GetComponentName(), "/_prewarmOverride");
		Checkbox* localOverrideEnabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Override Local Space", &_localSpaceOverrideEnabled);
		localOverrideEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_localSpaceOverrideEnabled");
		Checkbox* localOverrideValue = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Local Space", &_localSpaceOverride);
		localOverrideValue->SetPrefabOverrideBinding(GetComponentName(), "/_localSpaceOverride");
		Checkbox* receiveLighting = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Receive Lighting", &_receiveLighting);
		receiveLighting->SetPrefabOverrideBinding(GetComponentName(), "/_receiveLighting");

		DragFloat* timeScale = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Time Scale", &_timeScale, 0.0f, 8.0f, 0.01f);
		timeScale->SetPrefabOverrideBinding(GetComponentName(), "/_timeScale");

		const int32_t buttonWidth = (widget->GetSize().x - 30) / 3;
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Play", [this](Button*) { Play(); return true; });
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Pause", [this](Button*) { Pause(); return true; });
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Stop", [this](Button*) { Stop(); return true; });
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Restart", [this](Button*) { Restart(); return true; });
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Reset", [this](Button*) { Reset(); return true; });
		new Button(widget, widget->GetNextPos(), Point(buttonWidth, 18), L"Trigger", [this](Button*) { Trigger(16); return true; });

		return true;
	}

	void ParticleSystemComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		(void)isHovering;
		if (!isSelected || _effect == nullptr)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		if (transform == nullptr)
			return;

		const math::Vector3 center = GetEntity()->GetWorldTM().Translation();
		for (const auto& emitter : _effect->emitters)
		{
			switch (emitter.shape.type)
			{
			case ParticleShapeType::Box:
				g_pEnv->_debugRenderer->DrawAABB(dx::BoundingBox(center, emitter.shape.boxExtents), math::Color(HEX_RGBA_TO_FLOAT4(255, 140, 0, 220)));
				break;
			case ParticleShapeType::Sphere:
			case ParticleShapeType::Hemisphere:
			{
				const dx::BoundingBox sphereBounds(center, math::Vector3(emitter.shape.radius, emitter.shape.radius, emitter.shape.radius));
				g_pEnv->_debugRenderer->DrawAABB(sphereBounds, math::Color(HEX_RGBA_TO_FLOAT4(255, 140, 0, 180)));
				break;
			}
			default:
				g_pEnv->_debugRenderer->DrawLine(center, center + transform->GetForward() * 0.5f, math::Color(HEX_RGBA_TO_FLOAT4(255, 200, 0, 255)));
				break;
			}
		}
	}

	void ParticleSystemComponent::Play()
	{
		_playing = true;
		_paused = false;
	}

	void ParticleSystemComponent::Pause()
	{
		if (_playing)
			_paused = !_paused;
	}

	void ParticleSystemComponent::Stop()
	{
		_playing = false;
		_paused = false;
		_pendingTriggerCount = 0;
		if (g_pEnv != nullptr && g_pEnv->_particleWorldSystem != nullptr)
		{
			g_pEnv->_particleWorldSystem->ResetComponent(this);
		}
	}

	void ParticleSystemComponent::Restart()
	{
		Reset();
		Play();
	}

	void ParticleSystemComponent::Reset()
	{
		_pendingTriggerCount = 0;
		if (g_pEnv != nullptr && g_pEnv->_particleWorldSystem != nullptr)
		{
			g_pEnv->_particleWorldSystem->ResetComponent(this);
		}
	}

	void ParticleSystemComponent::Trigger(uint32_t count)
	{
		_pendingTriggerCount += count;
	}

	uint32_t ParticleSystemComponent::ConsumePendingTriggerCount()
	{
		const uint32_t count = _pendingTriggerCount;
		_pendingTriggerCount = 0;
		return count;
	}

	void ParticleSystemComponent::SetEffect(const std::shared_ptr<ParticleEffect>& effect)
	{
		_effect = effect;
		_effectPath = effect ? effect->GetFileSystemPath() : fs::path();
		_runtimeGeneration++;
		if (g_pEnv != nullptr && g_pEnv->_particleWorldSystem != nullptr)
		{
			g_pEnv->_particleWorldSystem->OnComponentChanged(this);
		}
	}

	void ParticleSystemComponent::SetEffectPath(const fs::path& path)
	{
		_effectPath = path;
		if (path.empty())
		{
			_effect.reset();
			return;
		}

		auto resource = g_pEnv->GetResourceSystem().LoadResource(path);
		auto effect = std::dynamic_pointer_cast<ParticleEffect>(resource);
		if (effect == nullptr)
		{
			LOG_WARN("Failed to load particle effect '%s'", path.string().c_str());
			return;
		}

		SetEffect(effect);
	}

	void ParticleSystemComponent::OnPickEffect(AssetSearch* search, const AssetSearchResult& result)
	{
		(void)search;
		if (result.assetPath.empty())
			return;
		SetEffectPath(result.assetPath);
	}
}
