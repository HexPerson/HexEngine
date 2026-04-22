#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class ParticleEffect;
	class ParticleWorldSystem;
	class AssetSearch;
	struct AssetSearchResult;

	class HEX_API ParticleSystemComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(ParticleSystemComponent);

		ParticleSystemComponent(Entity* entity);
		ParticleSystemComponent(Entity* entity, ParticleSystemComponent* copy);

		virtual void Update(float frameTime) override;
		virtual void Destroy() override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		void Play();
		void Pause();
		void Stop();
		void Restart();
		void Reset();
		void Trigger(uint32_t count = 1);

		bool IsPlaying() const { return _playing; }
		bool IsPaused() const { return _paused; }
		bool IsStopped() const { return !_playing; }

		void SetEffect(const std::shared_ptr<ParticleEffect>& effect);
		std::shared_ptr<ParticleEffect> GetEffect() const { return _effect; }

		const fs::path& GetEffectPath() const { return _effectPath; }
		void SetEffectPath(const fs::path& path);

		bool GetAutoPlay() const { return _autoPlay; }
		bool GetPrewarmOverride() const { return _prewarmOverride; }
		bool GetLocalSpaceOverrideEnabled() const { return _localSpaceOverrideEnabled; }
		bool GetLocalSpaceOverride() const { return _localSpaceOverride; }
		bool GetReceiveLighting() const { return _receiveLighting; }
		void SetReceiveLighting(bool value) { _receiveLighting = value; }
		float GetTimeScale() const { return _timeScale; }
		uint32_t ConsumePendingTriggerCount();

	private:
		void OnPickEffect(AssetSearch* search, const AssetSearchResult& result);

	private:
		std::shared_ptr<ParticleEffect> _effect;
		fs::path _effectPath;
		bool _autoPlay = true;
		bool _playing = true;
		bool _paused = false;
		bool _prewarmOverride = false;
		bool _localSpaceOverrideEnabled = false;
		bool _localSpaceOverride = true;
		bool _receiveLighting = false;
		float _timeScale = 1.0f;
		uint32_t _pendingTriggerCount = 0;
		uint64_t _runtimeGeneration = 0;
	};
}
