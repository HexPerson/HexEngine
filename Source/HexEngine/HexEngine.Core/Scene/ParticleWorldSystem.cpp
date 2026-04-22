#include "ParticleWorldSystem.hpp"

#include "ParticleEffect.hpp"
#include "Scene.hpp"
#include "../Entity/Component/ParticleSystemComponent.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/TimeManager.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../Graphics/Material.hpp"
#include "../Scene/Mesh.hpp"
#include "../Scene/MeshInstance.hpp"
#include "../Input/HVar.hpp"
#include <cstddef>

namespace HexEngine
{
	HVar r_particleEnable("r_particleEnable", "Enable GPU particle simulation/rendering", true, false, true);
	HVar r_particleMaxParticles("r_particleMaxParticles", "Global max number of GPU particles", 262144, 1024, 1048576);
	HVar r_particleMaxEmitters("r_particleMaxEmitters", "Global max number of active GPU emitters", 1024, 1, 4096);
	HVar r_particleDeltaClamp("r_particleDeltaClamp", "Clamp particle simulation delta time", 0.033f, 0.001f, 0.2f);
	HVar r_particleDebugStats("r_particleDebugStats", "Log periodic GPU particle stats", false, false, true);

	namespace
	{
		constexpr uint32_t ParticleFlags_LocalSpace = HEX_BITSET(0);
		constexpr uint32_t ParticleFlags_ShapePoint = 0;
		constexpr uint32_t ParticleFlags_ShapeSphere = 1;
		constexpr uint32_t ParticleFlags_ShapeHemisphere = 2;
		constexpr uint32_t ParticleFlags_ShapeBox = 3;
		constexpr uint32_t ParticleFlags_ShapeCone = 4;
		constexpr uint32_t ParticleFlags_ShapeDisc = 5;
		constexpr uint32_t ParticleFlags_ShapeLine = 6;

		constexpr uint32_t ParticleFlags_FacingShift = 8;
		constexpr uint32_t ParticleFlags_AlphaCurve3 = HEX_BITSET(16);

		uint32_t ToShapeFlag(ParticleShapeType type)
		{
			switch (type)
			{
			default: return ParticleFlags_ShapePoint;
			case ParticleShapeType::Sphere: return ParticleFlags_ShapeSphere;
			case ParticleShapeType::Hemisphere: return ParticleFlags_ShapeHemisphere;
			case ParticleShapeType::Box: return ParticleFlags_ShapeBox;
			case ParticleShapeType::Cone: return ParticleFlags_ShapeCone;
			case ParticleShapeType::Disc: return ParticleFlags_ShapeDisc;
			case ParticleShapeType::Line: return ParticleFlags_ShapeLine;
			}
		}

		bool IsEmitterEnabledForPlay(const ParticleSystemComponent* component, const ParticleEmitterDesc& emitter)
		{
			if (!emitter.enabled)
				return false;
			if (!component->IsPlaying() || component->IsPaused())
				return false;
			return true;
		}
	}

	void ParticleWorldSystem::Create()
	{
		Destroy();

		_simulateShader = IShader::Create("EngineData.Shaders/ParticleSimulate.hcs");
		_litSpriteShader = IShader::Create("EngineData.Shaders/ParticleBillboardLit.hcs");
		_simConstants = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(ParticleSimConstants));

		_spriteMesh = Mesh::Create("EngineData.Models/Primitives/billboard.hmesh");
		if (_spriteMesh == nullptr)
		{
			_spriteMesh = Mesh::Create("EngineData.Models/Primitives/quad.hmesh");
		}
		_defaultSpriteMaterial = Material::Create("EngineData.Materials/Billboard.hmat");
		_activeSpriteMaterial = _defaultSpriteMaterial;
		_activeSpriteTexture.reset();
		_activeSpriteMaterialPath.clear();
		_activeSpriteTexturePath.clear();
		_activeSpriteReceiveLighting = false;
	}

	void ParticleWorldSystem::Destroy()
	{
		_componentState.clear();
		_stableSpawnIndexByEmitter.clear();
		_frameEmitters.clear();
		_frameEmitterGpu.clear();

		SAFE_RELEASE(_particleStateUav);
		SAFE_RELEASE(_particleStateSrv);
		SAFE_RELEASE(_particleStateBuffer);

		SAFE_RELEASE(_emitterUav);
		SAFE_RELEASE(_emitterSrv);
		SAFE_RELEASE(_emitterBuffer);

		SAFE_RELEASE(_instanceAppendUav);
		SAFE_RELEASE(_instanceAppendSrv);
		SAFE_RELEASE(_instanceAppendBuffer);
		SAFE_RELEASE(_instanceVertexBuffer);

		SAFE_RELEASE(_drawArgsBuffer);

		SAFE_DELETE(_simConstants);
		_litSpriteShader.reset();
		_activeSpriteMaterial.reset();
		_activeSpriteTexture.reset();
		_activeSpriteMaterialPath.clear();
		_activeSpriteTexturePath.clear();
		_activeSpriteReceiveLighting = false;
		_maxParticles = 0;
		_maxEmitters = 0;
		_activeParticleCapacity = 0;
	}

	void ParticleWorldSystem::TickComponent(ParticleSystemComponent* component, float frameTime)
	{
		if (component == nullptr)
			return;

		auto& state = _componentState[component];
		state.accumulatedAge += frameTime;
	}

	void ParticleWorldSystem::OnComponentChanged(ParticleSystemComponent* component)
	{
		if (component == nullptr)
			return;

		auto& state = _componentState[component];
		state.needsReset = true;
		state.accumulatedAge = 0.0f;
		state.emissionRemainder.clear();
	}

	void ParticleWorldSystem::OnComponentDestroyed(ParticleSystemComponent* component)
	{
		if (component == nullptr)
			return;

		_componentState.erase(component);
		ClearGpuBuffers();
	}

	void ParticleWorldSystem::ResetComponent(ParticleSystemComponent* component)
	{
		if (component == nullptr)
			return;

		auto it = _componentState.find(component);
		if (it != _componentState.end())
		{
			it->second.needsReset = true;
			it->second.accumulatedAge = 0.0f;
			std::fill(it->second.emissionRemainder.begin(), it->second.emissionRemainder.end(), 0.0f);
		}

		ClearGpuBuffers();
	}

	void ParticleWorldSystem::Render(Scene* scene, Camera* camera, ITexture2D* target, ITexture2D* depthStencil)
	{
		if (!r_particleEnable._val.b || scene == nullptr || camera == nullptr || target == nullptr)
			return;

		if (_simulateShader == nullptr || _spriteMesh == nullptr || _defaultSpriteMaterial == nullptr)
			return;

		float dt = g_pEnv && g_pEnv->_timeManager ? (float)g_pEnv->_timeManager->_frameTime : _frameTime;
		dt = std::clamp(dt, 0.0f, r_particleDeltaClamp._val.f32);

		if (!BuildEmitters(scene, camera, dt))
			return;

		RunSimulationCompute(dt, camera);
		RenderSprites(camera, target, depthStencil);

		if (r_particleDebugStats._val.b && g_pEnv && g_pEnv->_timeManager && (g_pEnv->_timeManager->_frameCount % 120ull) == 0ull)
		{
			LOG_INFO("Particles: emitters=%u capacity=%u", (uint32_t)_frameEmitters.size(), _activeParticleCapacity);
		}
	}

	bool ParticleWorldSystem::BuildEmitters(Scene* scene, Camera* camera, float dt)
	{
		std::vector<ParticleSystemComponent*> components;
		scene->GetComponents<ParticleSystemComponent>(components);
		if (components.empty())
			return false;

		_frameEmitters.clear();
		_frameEmitterGpu.clear();
		fs::path requestedMaterialPath;
		fs::path requestedTexturePath;
		bool requestedReceiveLighting = false;
		bool hasRequestedRenderResource = false;

		const uint32_t maxParticlesBudget = (uint32_t)std::max(1024, r_particleMaxParticles._val.i32);
		const uint32_t maxEmittersBudget = (uint32_t)std::max(1, r_particleMaxEmitters._val.i32);

		uint32_t particleOffset = 0;
		for (auto* component : components)
		{
			if (component == nullptr || component->GetEntity() == nullptr || component->GetEntity()->IsPendingDeletion())
				continue;

			auto effect = component->GetEffect();
			if (effect == nullptr)
				continue;

			auto& runtimeState = _componentState[component];
			if (runtimeState.emissionRemainder.size() != effect->emitters.size())
				runtimeState.emissionRemainder.resize(effect->emitters.size(), 0.0f);

			const auto triggerCount = component->ConsumePendingTriggerCount();
			auto* transform = component->GetEntity()->GetComponent<Transform>();
			const auto entityPos = component->GetEntity()->GetWorldTM().Translation();
			const auto entityForward = transform ? transform->GetForward() : math::Vector3::Forward;

			for (uint32_t emitterIdx = 0; emitterIdx < effect->emitters.size(); ++emitterIdx)
			{
				if (_frameEmitters.size() >= maxEmittersBudget)
					break;

				const auto& emitter = effect->emitters[emitterIdx];
				if (!emitter.enabled)
					continue;

				if (!hasRequestedRenderResource)
				{
					requestedMaterialPath = emitter.materialPath;
					requestedTexturePath = emitter.texturePath;
					requestedReceiveLighting = component->GetReceiveLighting();
					hasRequestedRenderResource = true;
				}

				const uint32_t emitterMaxParticles = std::max(1u, emitter.maxParticles);
				if (particleOffset + emitterMaxParticles > maxParticlesBudget)
					continue;

				EmitterBuildData build;
				build.component = component;
				build.effect = effect.get();
				build.emitter = &emitter;
				build.emitterGlobalIndex = (uint32_t)_frameEmitters.size();
				build.poolStart = particleOffset;
				build.poolCount = emitterMaxParticles;
				build.emitterPosition = entityPos;
				build.emitterForward = entityForward;

				uint32_t spawnCount = 0;
				if (IsEmitterEnabledForPlay(component, emitter))
				{
					const float effectiveDt = dt * component->GetTimeScale();
					runtimeState.emissionRemainder[emitterIdx] += emitter.emission.rate * std::max(effectiveDt, 0.0f);
					const uint32_t rateSpawn = (uint32_t)std::floor(runtimeState.emissionRemainder[emitterIdx]);
					runtimeState.emissionRemainder[emitterIdx] -= (float)rateSpawn;
					spawnCount += rateSpawn;

					if (runtimeState.needsReset && emitter.emission.burst > 0)
						spawnCount += emitter.emission.burst;

					if (triggerCount > 0 && emitterIdx == 0)
						spawnCount += triggerCount;

					if (runtimeState.needsReset && emitter.prewarm)
					{
						spawnCount += (uint32_t)std::min<float>(emitterMaxParticles, emitter.emission.rate * emitter.emission.prewarmTime);
					}
				}

				build.spawnThisFrame = std::min(spawnCount, emitterMaxParticles);

				EmitterGpu gpu = {};
				gpu.emitterPosition = math::Vector4(build.emitterPosition.x, build.emitterPosition.y, build.emitterPosition.z, 1.0f);
				gpu.emitterVelocity = math::Vector4(build.emitterVelocity.x, build.emitterVelocity.y, build.emitterVelocity.z, 0.0f);
				gpu.emitterForward = math::Vector4(build.emitterForward.x, build.emitterForward.y, build.emitterForward.z, 0.0f);
				gpu.shapeParams0 = math::Vector4(
					emitter.shape.radius,
					emitter.shape.innerRadius,
					emitter.shape.coneAngleDegrees,
					emitter.shape.lineLength);
				gpu.shapeParams1 = math::Vector4(
					emitter.shape.boxExtents.x,
					emitter.shape.boxExtents.y,
					emitter.shape.boxExtents.z,
					emitter.shape.randomDirection ? 1.0f : 0.0f);
				gpu.simulation0 = math::Vector4(emitter.constantForce.x, emitter.constantForce.y, emitter.constantForce.z, emitter.drag);
				gpu.simulation1 = math::Vector4(emitter.gravity.x, emitter.gravity.y, emitter.gravity.z, emitter.sizeRange.y);
				gpu.simulation2 = math::Vector4(emitter.noiseAmplitude, emitter.noiseFrequency, emitter.velocityInheritance, emitter.sizeRange.x);
				gpu.simulation3 = math::Vector4(emitter.alphaOverLifetime.x, emitter.alphaOverLifetime.y, emitter.speedOverLifetime.x, emitter.speedOverLifetime.y);
				gpu.simulation4 = math::Vector4(
					emitter.alphaOverLifetimeCurve.x,
					emitter.alphaOverLifetimeCurve.y,
					emitter.alphaOverLifetimeCurve.z,
					std::clamp(emitter.alphaOverLifetimeCurveMidpoint, 0.001f, 0.999f));
				gpu.colorStart = emitter.startColor;
				gpu.colorEnd = emitter.endColor;
				gpu.lifeParams = math::Vector4(
					emitter.lifetimeRange.x,
					emitter.lifetimeRange.y,
					emitter.speedRange.x,
					emitter.speedRange.y);
				gpu.poolStart = build.poolStart;
				gpu.poolCount = build.poolCount;
				gpu.spawnRemaining = (int32_t)build.spawnThisFrame;
				gpu.flags = ToShapeFlag(emitter.shape.type) | ((uint32_t)emitter.facingMode << ParticleFlags_FacingShift);
				if ((component->GetLocalSpaceOverrideEnabled() && component->GetLocalSpaceOverride()) || (!component->GetLocalSpaceOverrideEnabled() && emitter.simulateInLocalSpace))
					gpu.flags |= ParticleFlags_LocalSpace;
				if (emitter.useThreePointAlphaCurve)
					gpu.flags |= ParticleFlags_AlphaCurve3;

				_frameEmitters.push_back(build);
				_frameEmitterGpu.push_back(gpu);

				particleOffset += emitterMaxParticles;
			}

			runtimeState.needsReset = false;
		}

		if (_frameEmitters.empty())
			return false;

		if (hasRequestedRenderResource)
		{
			if (requestedMaterialPath != _activeSpriteMaterialPath)
			{
				_activeSpriteMaterialPath = requestedMaterialPath;
				if (_activeSpriteMaterialPath.empty())
				{
					_activeSpriteMaterial = _defaultSpriteMaterial;
				}
				else
				{
					auto material = Material::Create(_activeSpriteMaterialPath);
					if (material != nullptr)
						_activeSpriteMaterial = material;
					else
						_activeSpriteMaterial = _defaultSpriteMaterial;
				}
			}

			if (requestedTexturePath != _activeSpriteTexturePath)
			{
				_activeSpriteTexturePath = requestedTexturePath;
				if (_activeSpriteTexturePath.empty())
				{
					_activeSpriteTexture.reset();
				}
				else
				{
					_activeSpriteTexture = ITexture2D::Create(_activeSpriteTexturePath);
				}
			}

			_activeSpriteReceiveLighting = requestedReceiveLighting;
		}

		_activeParticleCapacity = particleOffset;
		EnsureBuffers(std::max(1u, particleOffset), std::max(1u, (uint32_t)_frameEmitters.size()));
		return _particleStateBuffer != nullptr && _emitterBuffer != nullptr && _instanceAppendBuffer != nullptr && _instanceVertexBuffer != nullptr;
	}

	void ParticleWorldSystem::EnsureBuffers(uint32_t maxParticles, uint32_t maxEmitters)
	{
		if (_maxParticles >= maxParticles && _maxEmitters >= maxEmitters)
			return;

		SAFE_RELEASE(_particleStateUav);
		SAFE_RELEASE(_particleStateSrv);
		SAFE_RELEASE(_particleStateBuffer);
		SAFE_RELEASE(_emitterUav);
		SAFE_RELEASE(_emitterSrv);
		SAFE_RELEASE(_emitterBuffer);
		SAFE_RELEASE(_instanceAppendUav);
		SAFE_RELEASE(_instanceAppendSrv);
		SAFE_RELEASE(_instanceAppendBuffer);
		SAFE_RELEASE(_instanceVertexBuffer);
		SAFE_RELEASE(_drawArgsBuffer);

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return;

		_maxParticles = maxParticles;
		_maxEmitters = maxEmitters;

		D3D11_BUFFER_DESC particleDesc = {};
		particleDesc.ByteWidth = sizeof(ParticleStateGpu) * _maxParticles;
		particleDesc.Usage = D3D11_USAGE_DEFAULT;
		particleDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		particleDesc.CPUAccessFlags = 0;
		particleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		particleDesc.StructureByteStride = sizeof(ParticleStateGpu);
		CHECK_HR(device->CreateBuffer(&particleDesc, nullptr, &_particleStateBuffer));

		D3D11_SHADER_RESOURCE_VIEW_DESC particleSrvDesc = {};
		particleSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		particleSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		particleSrvDesc.Buffer.FirstElement = 0;
		particleSrvDesc.Buffer.NumElements = _maxParticles;
		CHECK_HR(device->CreateShaderResourceView(_particleStateBuffer, &particleSrvDesc, &_particleStateSrv));

		D3D11_UNORDERED_ACCESS_VIEW_DESC particleUavDesc = {};
		particleUavDesc.Format = DXGI_FORMAT_UNKNOWN;
		particleUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		particleUavDesc.Buffer.FirstElement = 0;
		particleUavDesc.Buffer.NumElements = _maxParticles;
		CHECK_HR(device->CreateUnorderedAccessView(_particleStateBuffer, &particleUavDesc, &_particleStateUav));

		D3D11_BUFFER_DESC emitterDesc = {};
		emitterDesc.ByteWidth = sizeof(EmitterGpu) * _maxEmitters;
		emitterDesc.Usage = D3D11_USAGE_DEFAULT;
		emitterDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		emitterDesc.CPUAccessFlags = 0;
		emitterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		emitterDesc.StructureByteStride = sizeof(EmitterGpu);
		CHECK_HR(device->CreateBuffer(&emitterDesc, nullptr, &_emitterBuffer));

		D3D11_SHADER_RESOURCE_VIEW_DESC emitterSrvDesc = {};
		emitterSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		emitterSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		emitterSrvDesc.Buffer.FirstElement = 0;
		emitterSrvDesc.Buffer.NumElements = _maxEmitters;
		CHECK_HR(device->CreateShaderResourceView(_emitterBuffer, &emitterSrvDesc, &_emitterSrv));

		D3D11_UNORDERED_ACCESS_VIEW_DESC emitterUavDesc = {};
		emitterUavDesc.Format = DXGI_FORMAT_UNKNOWN;
		emitterUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		emitterUavDesc.Buffer.FirstElement = 0;
		emitterUavDesc.Buffer.NumElements = _maxEmitters;
		CHECK_HR(device->CreateUnorderedAccessView(_emitterBuffer, &emitterUavDesc, &_emitterUav));

		D3D11_BUFFER_DESC instanceAppendDesc = {};
		instanceAppendDesc.ByteWidth = sizeof(MeshInstanceData) * _maxParticles;
		instanceAppendDesc.Usage = D3D11_USAGE_DEFAULT;
		instanceAppendDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		instanceAppendDesc.CPUAccessFlags = 0;
		instanceAppendDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		instanceAppendDesc.StructureByteStride = sizeof(MeshInstanceData);
		CHECK_HR(device->CreateBuffer(&instanceAppendDesc, nullptr, &_instanceAppendBuffer));

		D3D11_SHADER_RESOURCE_VIEW_DESC instanceAppendSrvDesc = {};
		instanceAppendSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		instanceAppendSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		instanceAppendSrvDesc.Buffer.FirstElement = 0;
		instanceAppendSrvDesc.Buffer.NumElements = _maxParticles;
		CHECK_HR(device->CreateShaderResourceView(_instanceAppendBuffer, &instanceAppendSrvDesc, &_instanceAppendSrv));

		D3D11_UNORDERED_ACCESS_VIEW_DESC instanceAppendUavDesc = {};
		instanceAppendUavDesc.Format = DXGI_FORMAT_UNKNOWN;
		instanceAppendUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		instanceAppendUavDesc.Buffer.FirstElement = 0;
		instanceAppendUavDesc.Buffer.NumElements = _maxParticles;
		instanceAppendUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
		CHECK_HR(device->CreateUnorderedAccessView(_instanceAppendBuffer, &instanceAppendUavDesc, &_instanceAppendUav));

		D3D11_BUFFER_DESC instanceVertexDesc = {};
		instanceVertexDesc.ByteWidth = sizeof(MeshInstanceData) * _maxParticles;
		instanceVertexDesc.Usage = D3D11_USAGE_DEFAULT;
		instanceVertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		instanceVertexDesc.CPUAccessFlags = 0;
		instanceVertexDesc.MiscFlags = 0;
		instanceVertexDesc.StructureByteStride = 0;
		CHECK_HR(device->CreateBuffer(&instanceVertexDesc, nullptr, &_instanceVertexBuffer));

		D3D11_BUFFER_DESC argsDesc = {};
		argsDesc.ByteWidth = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS);
		argsDesc.Usage = D3D11_USAGE_DEFAULT;
		argsDesc.BindFlags = 0;
		argsDesc.CPUAccessFlags = 0;
		argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		CHECK_HR(device->CreateBuffer(&argsDesc, nullptr, &_drawArgsBuffer));

		ClearGpuBuffers();
	}

	void ParticleWorldSystem::ClearGpuBuffers()
	{
		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;

		UINT clearData[4] = { 0, 0, 0, 0 };
		if (_particleStateUav)
			context->ClearUnorderedAccessViewUint(_particleStateUav, clearData);
		if (_instanceAppendUav)
			context->ClearUnorderedAccessViewUint(_instanceAppendUav, clearData);
	}

	void ParticleWorldSystem::RunSimulationCompute(float dt, Camera* camera)
	{
		if (_frameEmitters.empty() || _simulateShader == nullptr)
			return;

		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;

		context->UpdateSubresource(_emitterBuffer, 0, nullptr, _frameEmitterGpu.data(), 0, 0);

		ParticleSimConstants simConstants = {};
		auto* camTransform = camera->GetEntity() ? camera->GetEntity()->GetComponent<Transform>() : nullptr;
		const math::Vector3 camPos = camera->GetEntity() ? camera->GetEntity()->GetPosition() : math::Vector3::Zero;
		const math::Vector3 camRight = camTransform ? camTransform->GetRight() : math::Vector3::Right;
		const math::Vector3 camUp = camTransform ? camTransform->GetUp() : math::Vector3::Up;
		const math::Vector3 camForward = camTransform ? camTransform->GetForward() : math::Vector3::Forward;
		simConstants.cameraPos = math::Vector4(camPos.x, camPos.y, camPos.z, 1.0f);
		simConstants.cameraRight = math::Vector4(camRight.x, camRight.y, camRight.z, 0.0f);
		simConstants.cameraUp = math::Vector4(camUp.x, camUp.y, camUp.z, 0.0f);
		simConstants.cameraForward = math::Vector4(camForward.x, camForward.y, camForward.z, 0.0f);
		simConstants.dtTime = math::Vector4(dt, g_pEnv->_timeManager ? (float)g_pEnv->_timeManager->GetTime() : 0.0f, 0.0f, 0.0f);
		simConstants.globalParams = math::Vector4((float)_activeParticleCapacity, (float)_frameEmitters.size(), (float)_frameIndex, 0.0f);
		if (_simConstants)
			_simConstants->Write(&simConstants, sizeof(simConstants));

		D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS args = {};
		args.IndexCountPerInstance = _spriteMesh ? _spriteMesh->GetNumIndices() : 0;
		args.InstanceCount = 0;
		args.StartIndexLocation = 0;
		args.BaseVertexLocation = 0;
		args.StartInstanceLocation = 0;
		context->UpdateSubresource(_drawArgsBuffer, 0, nullptr, &args, 0, 0);

		auto* computeStage = _simulateShader->GetShaderStage(ShaderStage::ComputeShader);
		if (computeStage == nullptr)
			return;

		ID3D11Buffer* cbs[1] = { _simConstants ? reinterpret_cast<ID3D11Buffer*>(_simConstants->GetNativePtr()) : nullptr };
		ID3D11UnorderedAccessView* uavs[3] = { _particleStateUav, _emitterUav, _instanceAppendUav };
		UINT initialCounts[3] = { (UINT)-1, (UINT)-1, 0 };

		context->CSSetConstantBuffers(6, 1, cbs);
		context->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);
		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(computeStage->GetNativePtr()), nullptr, 0);
		context->Dispatch(std::max(1u, (_activeParticleCapacity + 63u) / 64u), 1, 1);

		ID3D11UnorderedAccessView* nullUavs[3] = {};
		ID3D11Buffer* nullCbs[1] = {};
		context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
		context->CSSetConstantBuffers(6, 1, nullCbs);
		context->CSSetShader(nullptr, nullptr, 0);

		context->CopyStructureCount(_drawArgsBuffer, offsetof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS, InstanceCount), _instanceAppendUav);
		if (_instanceVertexBuffer && _instanceAppendBuffer)
			context->CopyResource(_instanceVertexBuffer, _instanceAppendBuffer);
		++_frameIndex;
	}

	void ParticleWorldSystem::RenderSprites(Camera* camera, ITexture2D* target, ITexture2D* depthStencil)
	{
		if (_spriteMesh == nullptr || _defaultSpriteMaterial == nullptr || _drawArgsBuffer == nullptr)
			return;

		auto material = _activeSpriteMaterial ? _activeSpriteMaterial : _defaultSpriteMaterial;
		auto shader = (_activeSpriteReceiveLighting && _litSpriteShader) ? _litSpriteShader : material->GetStandardShader();
		if (shader == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;
		graphics->SetRenderTarget(target, depthStencil);
		graphics->SetBlendState(BlendState::Transparency);
		graphics->SetDepthBufferState(DepthBufferState::DepthRead);
		graphics->SetCullingMode(CullingMode::NoCulling);

		graphics->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
		graphics->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));
		graphics->SetInputLayout(shader->GetInputLayout());

		material->SaveRenderState();
		_spriteMesh->UpdateConstantBuffer(nullptr, math::Matrix::Identity, material.get(), 0, true);

		const uint32_t slotIdx = graphics->GetBoundResourceIndex();
		std::vector<ITexture2D*> textures = {
			material->GetTexture(MaterialTexture::Albedo).get(),
			material->GetTexture(MaterialTexture::Normal).get(),
			material->GetTexture(MaterialTexture::Roughness).get(),
			material->GetTexture(MaterialTexture::Metallic).get(),
			material->GetTexture(MaterialTexture::Height).get(),
			material->GetTexture(MaterialTexture::Emission).get(),
			material->GetTexture(MaterialTexture::Opacity).get(),
			material->GetTexture(MaterialTexture::AmbientOcclusion).get(),
		};
		if (_activeSpriteTexture != nullptr)
		{
			textures[0] = _activeSpriteTexture.get();
		}
		graphics->SetTexture2DArray(slotIdx, textures);

		_spriteMesh->SetBuffers(false);

		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(graphics->GetNativeDeviceContext());
		if (context != nullptr)
		{
			UINT stride = sizeof(MeshInstanceData);
			UINT offset = 0;
			ID3D11Buffer* instanceBuffer = _instanceVertexBuffer;
			context->IASetVertexBuffers(1, 1, &instanceBuffer, &stride, &offset);
		}

		graphics->DrawIndexedInstancedIndirect(_drawArgsBuffer, 0);
		material->RestoreRenderState();
	}

	uint64_t ParticleWorldSystem::MakeComponentKey(ParticleSystemComponent* component, uint32_t emitterIndex) const
	{
		return ((uint64_t)(uintptr_t)component << 32ull) ^ (uint64_t)emitterIndex;
	}
}
