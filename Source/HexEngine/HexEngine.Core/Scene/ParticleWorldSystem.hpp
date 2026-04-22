#pragma once

#include "../Required.hpp"
#include "ParticleEffect.hpp"
#include "../Graphics/IShader.hpp"

namespace HexEngine
{
	class ParticleSystemComponent;
	class ParticleEffect;
	class Scene;
	class Camera;
	class Mesh;
	class Material;
	class ITexture2D;
	class IConstantBuffer;

	class HEX_API ParticleWorldSystem
	{
	public:
		void Create();
		void Destroy();

		void TickComponent(ParticleSystemComponent* component, float frameTime);
		void OnComponentChanged(ParticleSystemComponent* component);
		void OnComponentDestroyed(ParticleSystemComponent* component);
		void ResetComponent(ParticleSystemComponent* component);

		void Render(Scene* scene, Camera* camera, ITexture2D* target, ITexture2D* depthStencil);

	private:
		struct ComponentRuntimeState
		{
			bool needsReset = true;
			float accumulatedAge = 0.0f;
			std::vector<float> emissionRemainder;
		};

		struct EmitterBuildData
		{
			ParticleSystemComponent* component = nullptr;
			const ParticleEffect* effect = nullptr;
			const ParticleEmitterDesc* emitter = nullptr;
			uint32_t emitterGlobalIndex = 0;
			uint32_t poolStart = 0;
			uint32_t poolCount = 0;
			uint32_t spawnThisFrame = 0;
			math::Vector3 emitterPosition = math::Vector3::Zero;
			math::Vector3 emitterVelocity = math::Vector3::Zero;
			math::Vector3 emitterForward = math::Vector3::Forward;
		};

		struct ParticleStateGpu
		{
			math::Vector4 position;
			math::Vector4 velocity;
			math::Vector4 color;
			math::Vector4 misc0; // x=size y=rotation z=age w=lifetime
			math::Vector4 misc1; // x=alive y=seed z=spawnIndex w=emitterIndex
		};

		struct EmitterGpu
		{
			math::Vector4 emitterPosition;
			math::Vector4 emitterVelocity;
			math::Vector4 emitterForward;
			math::Vector4 shapeParams0;
			math::Vector4 shapeParams1;
			math::Vector4 simulation0;
			math::Vector4 simulation1;
			math::Vector4 simulation2;
			math::Vector4 simulation3;
			math::Vector4 simulation4;
			math::Vector4 colorStart;
			math::Vector4 colorEnd;
			math::Vector4 lifeParams;
			uint32_t poolStart = 0;
			uint32_t poolCount = 0;
			int32_t spawnRemaining = 0;
			uint32_t flags = 0;
		};

		struct ParticleSimConstants
		{
			math::Vector4 cameraPos;
			math::Vector4 cameraRight;
			math::Vector4 cameraUp;
			math::Vector4 cameraForward;
			math::Vector4 dtTime;
			math::Vector4 globalParams;
		};

		bool BuildEmitters(Scene* scene, Camera* camera, float dt);
		void EnsureBuffers(uint32_t maxParticles, uint32_t maxEmitters);
		void ClearGpuBuffers();
		void RunSimulationCompute(float dt, Camera* camera);
		void RenderSprites(Camera* camera, ITexture2D* target, ITexture2D* depthStencil);
		uint64_t MakeComponentKey(ParticleSystemComponent* component, uint32_t emitterIndex) const;

	private:
		std::unordered_map<ParticleSystemComponent*, ComponentRuntimeState> _componentState;
		std::unordered_map<uint64_t, uint32_t> _stableSpawnIndexByEmitter;
		std::vector<EmitterBuildData> _frameEmitters;
		std::vector<EmitterGpu> _frameEmitterGpu;

		std::shared_ptr<IShader> _simulateShader;
		std::shared_ptr<IShader> _litSpriteShader;
		IConstantBuffer* _simConstants = nullptr;

		ID3D11Buffer* _particleStateBuffer = nullptr;
		ID3D11ShaderResourceView* _particleStateSrv = nullptr;
		ID3D11UnorderedAccessView* _particleStateUav = nullptr;

		ID3D11Buffer* _emitterBuffer = nullptr;
		ID3D11ShaderResourceView* _emitterSrv = nullptr;
		ID3D11UnorderedAccessView* _emitterUav = nullptr;

		ID3D11Buffer* _instanceAppendBuffer = nullptr;
		ID3D11ShaderResourceView* _instanceAppendSrv = nullptr;
		ID3D11UnorderedAccessView* _instanceAppendUav = nullptr;
		ID3D11Buffer* _instanceVertexBuffer = nullptr;

		ID3D11Buffer* _drawArgsBuffer = nullptr;
		uint32_t _maxParticles = 0;
		uint32_t _maxEmitters = 0;
		uint32_t _activeParticleCapacity = 0;

		std::shared_ptr<Mesh> _spriteMesh;
		std::shared_ptr<Material> _defaultSpriteMaterial;
		std::shared_ptr<Material> _activeSpriteMaterial;
		std::shared_ptr<ITexture2D> _activeSpriteTexture;
		fs::path _activeSpriteMaterialPath;
		fs::path _activeSpriteTexturePath;
		bool _activeSpriteReceiveLighting = false;

		float _frameTime = 0.0f;
		uint64_t _frameIndex = 0;
	};
}
