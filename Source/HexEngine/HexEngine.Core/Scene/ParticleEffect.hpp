#pragma once

#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	enum class ParticleRenderMode
	{
		Billboard = 0,
		StretchedBillboard,
		Mesh,
	};

	enum class ParticleFacingMode
	{
		CameraFacing = 0,
		VelocityAligned,
		AxisLocked,
	};

	enum class ParticleBlendMode
	{
		Opaque = 0,
		AlphaBlended,
		Additive,
		PremultipliedAlpha,
	};

	enum class ParticleShapeType
	{
		Point = 0,
		Sphere,
		Hemisphere,
		Box,
		Cone,
		Disc,
		Line,
	};

	struct ParticleEmissionModule
	{
		bool enabled = true;
		bool looping = true;
		float duration = 2.0f;
		float rate = 16.0f;
		uint32_t burst = 0;
		float prewarmTime = 0.0f;
	};

	struct ParticleShapeModule
	{
		bool enabled = true;
		ParticleShapeType type = ParticleShapeType::Point;
		math::Vector3 boxExtents = math::Vector3(0.5f, 0.5f, 0.5f);
		float radius = 0.5f;
		float innerRadius = 0.0f;
		float coneAngleDegrees = 25.0f;
		float lineLength = 1.0f;
		bool randomDirection = true;
	};

	struct ParticleFlipbookModule
	{
		bool enabled = false;
		uint32_t rows = 1;
		uint32_t columns = 1;
		float framesPerSecond = 12.0f;
		bool stretchToLifetime = true;
	};

	struct ParticleEmitterDesc
	{
		std::string name = "Emitter";
		bool enabled = true;
		bool simulateInLocalSpace = true;
		bool prewarm = false;
		uint32_t maxParticles = 1024;
		uint32_t seed = 1337;

		ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
		ParticleFacingMode facingMode = ParticleFacingMode::CameraFacing;
		ParticleBlendMode blendMode = ParticleBlendMode::AlphaBlended;
		bool softParticles = false;

		ParticleEmissionModule emission;
		ParticleShapeModule shape;
		ParticleFlipbookModule flipbook;

		math::Vector2 lifetimeRange = math::Vector2(0.75f, 1.25f);
		math::Vector2 speedRange = math::Vector2(1.0f, 2.0f);
		math::Vector2 sizeRange = math::Vector2(0.08f, 0.25f);
		math::Vector2 rotationSpeedRange = math::Vector2(-30.0f, 30.0f);
		math::Vector3 axisLock = math::Vector3::Up;

		math::Vector4 startColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		math::Vector4 endColor = math::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
		math::Vector2 sizeOverLifetime = math::Vector2(1.0f, 0.1f);
		math::Vector2 speedOverLifetime = math::Vector2(1.0f, 1.0f);
		math::Vector2 alphaOverLifetime = math::Vector2(1.0f, 0.0f);
		bool useThreePointAlphaCurve = false;
		math::Vector3 alphaOverLifetimeCurve = math::Vector3(1.0f, 1.0f, 0.0f);
		float alphaOverLifetimeCurveMidpoint = 0.5f;

		math::Vector3 constantForce = math::Vector3::Zero;
		math::Vector3 gravity = math::Vector3(0.0f, -9.81f, 0.0f);
		float drag = 0.0f;
		float orbitalStrength = 0.0f;
		float noiseAmplitude = 0.0f;
		float noiseFrequency = 1.0f;
		float velocityInheritance = 0.0f;

		fs::path materialPath = "EngineData.Materials/Billboard.hmat";
		fs::path meshPath;
		fs::path texturePath;
	};

	class HEX_API ParticleEffect : public IResource
	{
	public:
		uint32_t version = 1;
		std::string name = "ParticleEffect";
		bool oneShot = false;
		uint32_t seed = 777;
		std::vector<ParticleEmitterDesc> emitters;

		virtual void Destroy() override;
		virtual ResourceType GetResourceType() const override;
	};
}
