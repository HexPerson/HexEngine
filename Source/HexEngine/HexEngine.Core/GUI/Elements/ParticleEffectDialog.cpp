#include "ParticleEffectDialog.hpp"

#include "AssetSearch.hpp"
#include "Button.hpp"
#include "Checkbox.hpp"
#include "ColourPicker.hpp"
#include "ComponentWidget.hpp"
#include "DragFloat.hpp"
#include "DragInt.hpp"
#include "LineEdit.hpp"
#include "ScrollView.hpp"
#include "Vector2Edit.hpp"
#include "Vector3Edit.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Scene/ParticleEffect.hpp"

namespace HexEngine
{
	ParticleEffectDialog::ParticleEffectDialog(
		Element* parent,
		const Point& position,
		const Point& size,
		const std::wstring& title,
		const std::shared_ptr<ParticleEffect>& effect) :
		Dialog(parent, position, size, title),
		_effect(effect)
	{
		if (_effect == nullptr)
			return;

		if (_effect->emitters.empty())
			_effect->emitters.emplace_back();

		auto& emitter = _effect->emitters[0];

		_rootView = new ScrollView(this, Point(10, 10), Point(GetSize().x - 20, GetSize().y - 20));
		_layout = new ComponentWidget(_rootView, Point(10, 10), Point(size.x - 40, size.y), L"Particle Effect (Emitter 0)");

		LineEdit* effectName = new LineEdit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Effect Name");
		effectName->SetValue(s2ws(_effect->name));
		effectName->SetOnInputFn(std::bind(&ParticleEffectDialog::OnSetName, this, std::placeholders::_1, std::placeholders::_2));

		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"One Shot", &_effect->oneShot);
		new DragInt(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Effect Seed", (int32_t*)&_effect->seed, 0, 2147483647, 1);

		LineEdit* emitterName = new LineEdit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Emitter Name");
		emitterName->SetValue(s2ws(emitter.name));
		emitterName->SetOnInputFn([&emitter](LineEdit*, const std::wstring& value) { emitter.name = ws2s(value); });

		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Emitter Enabled", &emitter.enabled);
		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Simulate In Local Space", &emitter.simulateInLocalSpace);
		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Soft Particles", &emitter.softParticles);
		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Override Receive Lighting", &emitter.overrideReceiveLightingEnabled);
		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Receive Lighting (Override)", &emitter.overrideReceiveLighting);

		new DragInt(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Max Particles", (int32_t*)&emitter.maxParticles, 1, 1000000, 1);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Emission Rate", &emitter.emission.rate, 0.0f, 50000.0f, 0.1f, 2);
		new DragInt(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Burst Count", (int32_t*)&emitter.emission.burst, 0, 1000000, 1);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Drag", &emitter.drag, 0.0f, 10.0f, 0.01f, 2);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Noise Amplitude", &emitter.noiseAmplitude, 0.0f, 20.0f, 0.01f, 2);

		new Vector2Edit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Lifetime Range", &emitter.lifetimeRange, nullptr);
		new Vector2Edit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Speed Range", &emitter.speedRange, nullptr);
		new Vector2Edit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Size Range", &emitter.sizeRange, nullptr);
		new Vector2Edit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Alpha Over Lifetime", &emitter.alphaOverLifetime, nullptr);
		new Checkbox(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Use 3-Point Alpha Curve", &emitter.useThreePointAlphaCurve);
		new Vector3Edit(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Alpha Curve (Start/Mid/End)", &emitter.alphaOverLifetimeCurve, nullptr);
		new DragFloat(_layout, _layout->GetNextPos(), Point(size.x - 60, 18), L"Alpha Curve Midpoint", &emitter.alphaOverLifetimeCurveMidpoint, 0.01f, 0.99f, 0.01f, 2);

		new ColourPicker(_layout, _layout->GetNextPos(), Point(size.x - 60, 20), L"Start Color", (math::Color*)&emitter.startColor.x);
		new ColourPicker(_layout, _layout->GetNextPos(), Point(size.x - 60, 20), L"End Color", (math::Color*)&emitter.endColor.x);

		_materialSearch = new AssetSearch(
			_layout,
			_layout->GetNextPos(),
			Point(size.x - 60, 84),
			L"Material",
			{ ResourceType::Material },
			std::bind(&ParticleEffectDialog::OnPickMaterial, this, std::placeholders::_1, std::placeholders::_2));
		_materialSearch->SetValue(emitter.materialPath.wstring());

		_textureSearch = new AssetSearch(
			_layout,
			_layout->GetNextPos(),
			Point(size.x - 60, 84),
			L"Texture",
			{ ResourceType::Image },
			std::bind(&ParticleEffectDialog::OnPickTexture, this, std::placeholders::_1, std::placeholders::_2));
		_textureSearch->SetValue(emitter.texturePath.wstring());

		new Button(_layout, _layout->GetNextPos(), Point(110, 20), L"Save", std::bind(&ParticleEffectDialog::SaveAndClose, this));
	}

	bool ParticleEffectDialog::SaveAndClose()
	{
		if (_effect == nullptr)
			return true;

		_effect->Save();
		DeleteMe();
		return true;
	}

	void ParticleEffectDialog::OnPickMaterial(AssetSearch* search, const AssetSearchResult& result)
	{
		(void)search;
		if (_effect == nullptr || _effect->emitters.empty())
			return;

		auto& emitter = _effect->emitters[0];
		emitter.materialPath = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
		if (_materialSearch != nullptr)
			_materialSearch->SetValue(emitter.materialPath.wstring());
	}

	void ParticleEffectDialog::OnPickTexture(AssetSearch* search, const AssetSearchResult& result)
	{
		(void)search;
		if (_effect == nullptr || _effect->emitters.empty())
			return;

		auto& emitter = _effect->emitters[0];
		emitter.texturePath = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
		if (_textureSearch != nullptr)
			_textureSearch->SetValue(emitter.texturePath.wstring());
	}

	void ParticleEffectDialog::OnSetName(LineEdit* edit, const std::wstring& value)
	{
		(void)edit;
		if (_effect == nullptr)
			return;
		_effect->name = ws2s(value);
	}
}
