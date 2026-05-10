#include "VolumetricTerrainComponent.hpp"
#include <climits>
#include <Windows.h>

namespace HexEngine::VolumetricTerrain
{
	VolumetricTerrainComponent::VolumetricTerrainComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::MouseDown | InputEvent::MouseUp);
	}

	VolumetricTerrainComponent::VolumetricTerrainComponent(Entity* entity, VolumetricTerrainComponent* copy) :
		UpdateComponent(entity, copy)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::MouseDown | InputEvent::MouseUp);
		if (copy != nullptr)
		{
			_brush = copy->_brush;
			_runtimeStrengthScale = copy->_runtimeStrengthScale;
			_showDebugBounds = copy->_showDebugBounds;
			_sculptEnabled = copy->_sculptEnabled;
			_leftMouseDown = copy->_leftMouseDown;
			GetGenerationParams() = copy->GetGenerationParams();
		}
	}

	VolumetricTerrainComponent::~VolumetricTerrainComponent()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void VolumetricTerrainComponent::InitializeTerrain()
	{
		_terrain.Initialize(GetEntity(), GetGenerationParams());
		_brushApplyAccumulator = 0.0f;
		_hasLastBrushApplyPosition = false;
		_hasBrushPreview = false;
		_initialized = true;
	}

	void VolumetricTerrainComponent::Serialize(json& data, JsonFile* file)
	{
		if (!_initialized)
		{
			InitializeTerrain();
		}

		data["_saveVersion"] = 1;
		_terrain.Serialize(data, file);

		file->Serialize(data, "_sculptEnabled", _sculptEnabled);
		file->Serialize(data, "_showDebugBounds", _showDebugBounds);
		file->Serialize(data, "_runtimeStrengthScale", _runtimeStrengthScale);
		int32_t brushMode = static_cast<int32_t>(_brush.mode);
		file->Serialize(data, "_brushMode", brushMode);
		file->Serialize(data, "_brushRadius", _brush.radius);
		file->Serialize(data, "_brushStrength", _brush.strength);
		file->Serialize(data, "_brushFalloff", _brush.falloff);
		file->Serialize(data, "_brushHardness", _brush.hardness);
		file->Serialize(data, "_brushTargetHeight", _brush.targetHeight);
		file->Serialize(data, "_brushNoiseScale", _brush.noiseScale);
		file->Serialize(data, "_brushMaterialIndex", _brush.materialIndex);
		file->Serialize(data, "_brushUseGpu", _brush.useGpu);
	}

	void VolumetricTerrainComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;

		file->Deserialize(data, "_sculptEnabled", _sculptEnabled);
		file->Deserialize(data, "_showDebugBounds", _showDebugBounds);
		file->Deserialize(data, "_runtimeStrengthScale", _runtimeStrengthScale);

		int32_t mode = static_cast<int32_t>(_brush.mode);
		file->Deserialize(data, "_brushMode", mode);
		_brush.mode = static_cast<BrushMode>(std::clamp(mode, 0, static_cast<int32_t>(BrushMode::Noise)));
		file->Deserialize(data, "_brushRadius", _brush.radius);
		file->Deserialize(data, "_brushStrength", _brush.strength);
		file->Deserialize(data, "_brushFalloff", _brush.falloff);
		file->Deserialize(data, "_brushHardness", _brush.hardness);
		file->Deserialize(data, "_brushTargetHeight", _brush.targetHeight);
		file->Deserialize(data, "_brushNoiseScale", _brush.noiseScale);
		file->Deserialize(data, "_brushMaterialIndex", _brush.materialIndex);
		file->Deserialize(data, "_brushUseGpu", _brush.useGpu);

		_terrain.Initialize(GetEntity(), GetGenerationParams());
		_terrain.Deserialize(data, file);
		_initialized = true;
	}

	bool VolumetricTerrainComponent::CreateWidget(ComponentWidget* widget)
	{
		auto& params = GetGenerationParams();

		auto* seed = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Seed", reinterpret_cast<int32_t*>(&params.seed), 0, INT_MAX, 1);
		auto* chunkRes = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunk Resolution", &params.chunkResolution, 8, 64, 1);
		auto* collisionRes = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Collision Resolution", &params.collisionResolution, 4, 64, 1);
		auto* chunkSize = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunk Size", &params.chunkWorldSize, 32.0f, 1024.0f, 1.0f);
		auto* chunksX = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks X", &params.chunksX, 1, 32, 1);
		auto* chunksY = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks Y", &params.chunksY, 1, 16, 1);
		auto* chunksZ = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks Z", &params.chunksZ, 1, 32, 1);
		auto* caveFreq = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Cave Frequency", &params.caveFrequency, 0.0001f, 1.0f, 0.001f, 4);
		auto* caveStrength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Cave Strength", &params.caveStrength, 0.0f, 256.0f, 0.5f, 2);
		auto* seedFromHeightmap = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Seed from heightmap", &params.seedFromHeightMap);

		auto* sculpt = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enable Sculpting", &_sculptEnabled);
		auto* brushRadius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Brush Radius", &_brush.radius, 1.0f, 256.0f, 0.5f, 2);
		auto* brushStrength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Brush Strength", &_brush.strength, 0.1f, 256.0f, 0.5f, 2);
		auto* brushSpeed = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Brush Speed", &_runtimeStrengthScale, 0.1f, 64.0f, 0.25f, 2);
		auto* brushFalloff = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Brush Falloff", &_brush.falloff, 0.1f, 8.0f, 0.1f, 2);
		auto* brushTarget = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Flatten Height", &_brush.targetHeight, -2048.0f, 2048.0f, 1.0f, 2);
		auto* matIndex = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Paint Material Index", &_brush.materialIndex, 0, 255, 1);
		auto* gpuBrush = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use GPU Brush (Experimental)", &_brush.useGpu);
		auto* debugBounds = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Show Chunk Bounds", &_showDebugBounds);

		auto* mode = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Brush Mode");
		mode->SetValue(L"Subtract");
		mode->GetContextMenu()->AddItem(new ContextItem(L"Add", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Add; mode->SetValue(L"Add"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Subtract", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Subtract; mode->SetValue(L"Subtract"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Elevate", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Elevate; mode->SetValue(L"Elevate"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Flatten", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Flatten; mode->SetValue(L"Flatten"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Smooth", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Smooth; mode->SetValue(L"Smooth"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Erode", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Erode; mode->SetValue(L"Erode"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Paint Material", [this, mode](const std::wstring&) { _brush.mode = BrushMode::PaintMaterial; mode->SetValue(L"Paint Material"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Tunnel", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Tunnel; mode->SetValue(L"Tunnel"); }));
		mode->GetContextMenu()->AddItem(new ContextItem(L"Noise", [this, mode](const std::wstring&) { _brush.mode = BrushMode::Noise; mode->SetValue(L"Noise"); }));

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 20), L"Regenerate Terrain",
			[this](Button*) -> bool
			{
				InitializeTerrain();
				return true;
			});

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 20), L"Rebuild All Chunks",
			[this](Button*) -> bool
			{
				_terrain.RebuildAll(true);
				return true;
			});

		(void)seed; (void)chunkRes; (void)chunkSize; (void)chunksX; (void)chunksY; (void)chunksZ;
		(void)caveFreq; (void)caveStrength; (void)seedFromHeightmap; (void)sculpt; (void)brushRadius; (void)brushStrength; (void)brushSpeed;
		(void)brushFalloff; (void)brushTarget; (void)matIndex; (void)gpuBrush; (void)debugBounds;
		return true;
	}

	bool VolumetricTerrainComponent::RayCastBrush(RayHit& hit) const
	{
		auto* scene = g_pEnv->_sceneManager->GetCurrentScene().get();
		if (scene == nullptr)
			return false;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr)
			return false;

		int32_t mx = 0;
		int32_t my = 0;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto rayDir = g_pEnv->_inputSystem->GetScreenToWorldRay(camera, mx, my);
		math::Ray ray;
		ray.position = camera->GetEntity()->GetWorldTM().Translation();
		ray.direction = rayDir;

		if (PhysUtils::RayCast(
			ray,
			camera->GetFarZ(),
			0xFFFFFFFFu,
			&hit,
			{}))
		{
			if (_terrain.FindChunkEntityFromRayHit(hit.entity) != nullptr)
			{
				return true;
			}
		}

		math::Vector3 fallbackPosition = math::Vector3::Zero;
		if (_terrain.RaycastTerrainSurface(ray, camera->GetFarZ(), fallbackPosition))
		{
			hit.position = fallbackPosition;
			hit.entity = _terrain.GetRootEntity();
			hit.distance = (fallbackPosition - ray.position).Length();
			hit.normal = math::Vector3::Up;
			return true;
		}

		return false;
	}

	void VolumetricTerrainComponent::ApplyRealtimeBrush(float deltaTime)
	{
		if (!_sculptEnabled || !_initialized)
			return;

		if (!_leftMouseDown)
		{
			_terrain.SetSculptingActive(false);
			return;
		}

		_terrain.SetSculptingActive(true);

		RayHit hit;
		if (!RayCastBrush(hit))
		{
			return;
		}

		if (_terrain.FindChunkEntityFromRayHit(hit.entity) == nullptr && hit.entity != _terrain.GetRootEntity())
		{
			return;
		}

		_lastBrushPreviewPosition = hit.position;
		_hasBrushPreview = true;

		_terrain.SetBrush(_brush);
		// Editor frame time can be tiny/zero; keep sculpt response stable and
		// scale edits by voxel size so default settings visibly deform terrain.
		const float minBrushStep = 1.0f / 30.0f;
		const float effectiveDt = std::max(deltaTime, minBrushStep);
		const float voxelSize = _terrain.GetGenerationParams().chunkWorldSize /
			static_cast<float>(std::max(1, _terrain.GetGenerationParams().chunkResolution));
		const float voxelScale = std::max(0.25f, voxelSize * 0.4f);
		_brushApplyAccumulator += effectiveDt;
		const float minStampInterval = 1.0f / 12.0f;
		const float minMoveDistance = std::max(voxelSize * 1.0f, _brush.radius * 0.12f);
		const bool shouldStampFromTime = _brushApplyAccumulator >= minStampInterval;
		const bool shouldStampFromMove = !_hasLastBrushApplyPosition ||
			((hit.position - _lastBrushApplyPosition).LengthSquared() >= (minMoveDistance * minMoveDistance));
		if (shouldStampFromTime || shouldStampFromMove)
		{
			const float stampDt = std::max(_brushApplyAccumulator, minBrushStep);
			_terrain.ApplyBrush(hit.position, _brush, stampDt * _runtimeStrengthScale * voxelScale);
			_brushApplyAccumulator = 0.0f;
			_hasLastBrushApplyPosition = true;
			_lastBrushApplyPosition = hit.position;
		}
	}

	void VolumetricTerrainComponent::OnGUI()
	{
		// Runtime sculpt/tick happens in Update(); OnGUI is intentionally light.
		if (!_initialized)
		{
			InitializeTerrain();
		}
	}

	void VolumetricTerrainComponent::Update(float frameTime)
	{
		if (!_initialized)
		{
			InitializeTerrain();
		}

		ApplyRealtimeBrush(frameTime);
		_terrain.Tick(frameTime);
	}

	void VolumetricTerrainComponent::OnDebugRender()
	{
		if (!_showDebugBounds || !_initialized)
			return;

		RayHit hit;
		math::Vector3 previewPosition = _lastBrushPreviewPosition;
		bool canDraw = _hasBrushPreview;

		if (RayCastBrush(hit))
		{
			previewPosition = hit.position;
			_lastBrushPreviewPosition = hit.position;
			_hasBrushPreview = true;
			canDraw = true;
		}

		if (!canDraw)
		{
			return;
		}

		dx::BoundingBox brushBox;
		brushBox.Center = previewPosition;
		brushBox.Extents = math::Vector3(_brush.radius, _brush.radius, _brush.radius);
		g_pEnv->_debugRenderer->DrawAABB(brushBox, math::Color(0.1f, 0.8f, 0.2f, 1.0f));
	}

	bool VolumetricTerrainComponent::OnInputEvent(InputEvent event, InputData* data)
	{
		if (data == nullptr)
		{
			return false;
		}

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			_leftMouseDown = true;
			_terrain.SetSculptingActive(true);
			_brushApplyAccumulator = 0.0f;
			_hasLastBrushApplyPosition = false;
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON)
		{
			_leftMouseDown = false;
			_terrain.SetSculptingActive(false);
			_brushApplyAccumulator = 0.0f;
			_hasLastBrushApplyPosition = false;
		}

		return false;
	}
}




