#pragma once

#include "VolumetricTerrain.hpp"

namespace HexEngine::VolumetricTerrain
{
	class VolumetricTerrainComponent : public UpdateComponent, public IInputListener
	{
public:
		CREATE_COMPONENT_ID(VolumetricTerrainComponent);
		DEFINE_COMPONENT_CTOR(VolumetricTerrainComponent);
		virtual ~VolumetricTerrainComponent();

		void InitializeTerrain();

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void Update(float frameTime) override;
		virtual void OnGUI() override;
		virtual void OnDebugRender() override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		SdfTerrainGenerationParams& GetGenerationParams() { return _terrain.GetGenerationParams(); }
		BrushSettings& GetBrushSettings() { return _brush; }

		void SetSculptEnabled(bool enabled) { _sculptEnabled = enabled; }
		bool IsSculptEnabled() const { return _sculptEnabled; }

	private:
		bool RayCastBrush(RayHit& hit) const;
		void ApplyRealtimeBrush(float deltaTime);

	private:
		VolumetricTerrain _terrain;
		BrushSettings _brush;
		bool _initialized = false;
		bool _sculptEnabled = false;
		bool _showDebugBounds = true;
		float _runtimeStrengthScale = 8.0f;
		float _brushApplyAccumulator = 0.0f;
		bool _hasBrushPreview = false;
		bool _leftMouseDown = false;
		bool _hasLastBrushApplyPosition = false;
		math::Vector3 _lastBrushApplyPosition = math::Vector3::Zero;
		math::Vector3 _lastBrushPreviewPosition = math::Vector3::Zero;
	};
}
