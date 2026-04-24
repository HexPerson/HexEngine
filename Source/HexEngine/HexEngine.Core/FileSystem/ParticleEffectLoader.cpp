#include "ParticleEffectLoader.hpp"

#include "DiskFile.hpp"
#include "FileSystem.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../FileSystem/JsonFile.hpp"
#include "../GUI/Elements/ParticleEffectDialog.hpp"
#include "../GUI/UIManager.hpp"
#include "../Scene/ParticleEffect.hpp"

namespace HexEngine
{
	namespace
	{
		template<typename T>
		void ReadValue(const json& data, const char* key, T& outValue)
		{
			if (auto it = data.find(key); it != data.end() && !it->is_null())
			{
				outValue = it->get<T>();
			}
		}

		template<typename T>
		void WriteValue(json& data, const char* key, const T& value)
		{
			data[key] = value;
		}

		void ReadVector2(const json& data, const char* key, math::Vector2& outValue)
		{
			if (auto it = data.find(key); it != data.end() && it->is_array() && it->size() >= 2)
			{
				outValue.x = (*it)[0].get<float>();
				outValue.y = (*it)[1].get<float>();
			}
		}

		void ReadVector3(const json& data, const char* key, math::Vector3& outValue)
		{
			if (auto it = data.find(key); it != data.end() && it->is_array() && it->size() >= 3)
			{
				outValue.x = (*it)[0].get<float>();
				outValue.y = (*it)[1].get<float>();
				outValue.z = (*it)[2].get<float>();
			}
		}

		void ReadVector4(const json& data, const char* key, math::Vector4& outValue)
		{
			if (auto it = data.find(key); it != data.end() && it->is_array() && it->size() >= 4)
			{
				outValue.x = (*it)[0].get<float>();
				outValue.y = (*it)[1].get<float>();
				outValue.z = (*it)[2].get<float>();
				outValue.w = (*it)[3].get<float>();
			}
		}

		void WriteVector2(json& data, const char* key, const math::Vector2& value)
		{
			data[key] = { value.x, value.y };
		}

		void WriteVector3(json& data, const char* key, const math::Vector3& value)
		{
			data[key] = { value.x, value.y, value.z };
		}

		void WriteVector4(json& data, const char* key, const math::Vector4& value)
		{
			data[key] = { value.x, value.y, value.z, value.w };
		}
	}

	ParticleEffectLoader::ParticleEffectLoader()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	ParticleEffectLoader::~ParticleEffectLoader()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> ParticleEffectLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		(void)fileSystem;
		(void)options;

		JsonFile file(absolutePath, std::ios::in);
		if (!file.DoesExist() || !file.Open())
		{
			LOG_CRIT("Particle effect file '%s' does not exist", absolutePath.string().c_str());
			return nullptr;
		}

		std::string data;
		file.ReadAll(data);
		file.Close();

		json root = json::parse(data, nullptr, false, true);
		if (root.is_discarded())
		{
			LOG_CRIT("Failed to parse particle effect json '%s'", absolutePath.string().c_str());
			return nullptr;
		}

		auto effect = std::shared_ptr<ParticleEffect>(new ParticleEffect, ResourceDeleter());
		if (!ParseEffectJson(root, *effect))
		{
			LOG_CRIT("Particle effect '%s' is invalid", absolutePath.string().c_str());
			return nullptr;
		}

		return effect;
	}

	std::shared_ptr<IResource> ParticleEffectLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		(void)relativePath;
		(void)fileSystem;
		(void)options;

		std::string content((const char*)data.data(), data.size());
		json root = json::parse(content, nullptr, false, true);
		if (root.is_discarded())
			return nullptr;

		auto effect = std::shared_ptr<ParticleEffect>(new ParticleEffect, ResourceDeleter());
		if (!ParseEffectJson(root, *effect))
			return nullptr;

		return effect;
	}

	void ParticleEffectLoader::OnResourceChanged(std::shared_ptr<IResource> resource)
	{
		auto effect = std::dynamic_pointer_cast<ParticleEffect>(resource);
		if (effect == nullptr)
			return;

		JsonFile file(effect->GetAbsolutePath(), std::ios::in);
		if (!file.DoesExist() || !file.Open())
			return;

		std::string data;
		file.ReadAll(data);
		file.Close();

		json root = json::parse(data, nullptr, false, true);
		if (root.is_discarded())
			return;

		ParseEffectJson(root, *effect);
	}

	void ParticleEffectLoader::UnloadResource(IResource* resource)
	{
		auto effect = dynamic_cast<ParticleEffect*>(resource);
		if (effect == nullptr)
			return;

		effect->Destroy();
		SAFE_DELETE(effect);
	}

	std::vector<std::string> ParticleEffectLoader::GetSupportedResourceExtensions()
	{
		return { ".hparticles" };
	}

	std::wstring ParticleEffectLoader::GetResourceDirectory() const
	{
		return L"Particles";
	}

	Dialog* ParticleEffectLoader::CreateEditorDialog(const std::vector<fs::path>& paths)
	{
		if (paths.empty())
			return nullptr;

		auto effect = std::dynamic_pointer_cast<ParticleEffect>(g_pEnv->GetResourceSystem().LoadResource(paths[0]));
		if (effect == nullptr)
			return nullptr;

		const int32_t cx = g_pEnv->GetUIManager().GetRootElement()->GetSize().x / 2;
		const int32_t cy = g_pEnv->GetUIManager().GetRootElement()->GetSize().y / 2;
		const int32_t dlgW = 920;
		const int32_t dlgH = 700;

		return new ParticleEffectDialog(
			g_pEnv->GetUIManager().GetRootElement(),
			Point(cx - dlgW / 2, cy - dlgH / 2),
			Point(dlgW, dlgH),
			std::format(L"Editing Particle Effect '{}'", paths[0].filename().wstring()),
			effect);
	}

	void ParticleEffectLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		auto effect = dynamic_cast<ParticleEffect*>(resource);
		if (effect == nullptr)
			return;

		json root;
		SerializeEffectJson(*effect, root);

		JsonFile file(path, std::ios::out | std::ios::trunc);
		if (!file.Open())
		{
			LOG_CRIT("Failed to save particle effect '%s'", path.string().c_str());
			return;
		}

		const auto jsonText = root.dump(2);
		file.Write((void*)jsonText.data(), (uint32_t)jsonText.size());
		file.Flush();
		file.Close();
	}

	bool ParticleEffectLoader::ParseEffectJson(const json& root, ParticleEffect& outEffect)
	{
		ReadValue(root, "version", outEffect.version);
		ReadValue(root, "name", outEffect.name);
		ReadValue(root, "oneShot", outEffect.oneShot);
		ReadValue(root, "seed", outEffect.seed);

		outEffect.emitters.clear();
		if (auto emittersIt = root.find("emitters"); emittersIt != root.end() && emittersIt->is_array())
		{
			for (const auto& emitterJson : *emittersIt)
			{
				if (!emitterJson.is_object())
					continue;

				ParticleEmitterDesc emitter;
				ReadValue(emitterJson, "name", emitter.name);
				ReadValue(emitterJson, "enabled", emitter.enabled);
				ReadValue(emitterJson, "simulateInLocalSpace", emitter.simulateInLocalSpace);
				ReadValue(emitterJson, "prewarm", emitter.prewarm);
				ReadValue(emitterJson, "maxParticles", emitter.maxParticles);
				ReadValue(emitterJson, "seed", emitter.seed);

				int32_t renderMode = (int32_t)emitter.renderMode;
				int32_t facingMode = (int32_t)emitter.facingMode;
				int32_t blendMode = (int32_t)emitter.blendMode;
				ReadValue(emitterJson, "renderMode", renderMode);
				ReadValue(emitterJson, "facingMode", facingMode);
				ReadValue(emitterJson, "blendMode", blendMode);
				emitter.renderMode = (ParticleRenderMode)renderMode;
				emitter.facingMode = (ParticleFacingMode)facingMode;
				emitter.blendMode = (ParticleBlendMode)blendMode;
				ReadValue(emitterJson, "softParticles", emitter.softParticles);
				ReadValue(emitterJson, "overrideReceiveLightingEnabled", emitter.overrideReceiveLightingEnabled);
				ReadValue(emitterJson, "overrideReceiveLighting", emitter.overrideReceiveLighting);

				if (auto emissionIt = emitterJson.find("emission"); emissionIt != emitterJson.end() && emissionIt->is_object())
				{
					ReadValue(*emissionIt, "enabled", emitter.emission.enabled);
					ReadValue(*emissionIt, "looping", emitter.emission.looping);
					ReadValue(*emissionIt, "duration", emitter.emission.duration);
					ReadValue(*emissionIt, "rate", emitter.emission.rate);
					ReadValue(*emissionIt, "burst", emitter.emission.burst);
					ReadValue(*emissionIt, "prewarmTime", emitter.emission.prewarmTime);
				}

				if (auto shapeIt = emitterJson.find("shape"); shapeIt != emitterJson.end() && shapeIt->is_object())
				{
					ReadValue(*shapeIt, "enabled", emitter.shape.enabled);
					int32_t shapeType = (int32_t)emitter.shape.type;
					ReadValue(*shapeIt, "type", shapeType);
					emitter.shape.type = (ParticleShapeType)shapeType;
					ReadVector3(*shapeIt, "boxExtents", emitter.shape.boxExtents);
					ReadValue(*shapeIt, "radius", emitter.shape.radius);
					ReadValue(*shapeIt, "innerRadius", emitter.shape.innerRadius);
					ReadValue(*shapeIt, "coneAngleDegrees", emitter.shape.coneAngleDegrees);
					ReadValue(*shapeIt, "lineLength", emitter.shape.lineLength);
					ReadValue(*shapeIt, "randomDirection", emitter.shape.randomDirection);
				}

				if (auto flipbookIt = emitterJson.find("flipbook"); flipbookIt != emitterJson.end() && flipbookIt->is_object())
				{
					ReadValue(*flipbookIt, "enabled", emitter.flipbook.enabled);
					ReadValue(*flipbookIt, "rows", emitter.flipbook.rows);
					ReadValue(*flipbookIt, "columns", emitter.flipbook.columns);
					ReadValue(*flipbookIt, "framesPerSecond", emitter.flipbook.framesPerSecond);
					ReadValue(*flipbookIt, "stretchToLifetime", emitter.flipbook.stretchToLifetime);
				}

				ReadVector2(emitterJson, "lifetimeRange", emitter.lifetimeRange);
				ReadVector2(emitterJson, "speedRange", emitter.speedRange);
				ReadVector2(emitterJson, "sizeRange", emitter.sizeRange);
				ReadVector2(emitterJson, "rotationSpeedRange", emitter.rotationSpeedRange);
				ReadVector3(emitterJson, "axisLock", emitter.axisLock);

				ReadVector4(emitterJson, "startColor", emitter.startColor);
				ReadVector4(emitterJson, "endColor", emitter.endColor);
				ReadVector2(emitterJson, "sizeOverLifetime", emitter.sizeOverLifetime);
				ReadVector2(emitterJson, "speedOverLifetime", emitter.speedOverLifetime);
				ReadVector2(emitterJson, "alphaOverLifetime", emitter.alphaOverLifetime);
				ReadValue(emitterJson, "useThreePointAlphaCurve", emitter.useThreePointAlphaCurve);
				ReadVector3(emitterJson, "alphaOverLifetimeCurve", emitter.alphaOverLifetimeCurve);
				ReadValue(emitterJson, "alphaOverLifetimeCurveMidpoint", emitter.alphaOverLifetimeCurveMidpoint);

				ReadVector3(emitterJson, "constantForce", emitter.constantForce);
				ReadVector3(emitterJson, "gravity", emitter.gravity);
				ReadValue(emitterJson, "drag", emitter.drag);
				ReadValue(emitterJson, "orbitalStrength", emitter.orbitalStrength);
				ReadValue(emitterJson, "noiseAmplitude", emitter.noiseAmplitude);
				ReadValue(emitterJson, "noiseFrequency", emitter.noiseFrequency);
				ReadValue(emitterJson, "velocityInheritance", emitter.velocityInheritance);

				std::string materialPath = emitter.materialPath.string();
				std::string meshPath = emitter.meshPath.string();
				std::string texturePath = emitter.texturePath.string();
				ReadValue(emitterJson, "materialPath", materialPath);
				ReadValue(emitterJson, "meshPath", meshPath);
				ReadValue(emitterJson, "texturePath", texturePath);
				emitter.materialPath = materialPath;
				emitter.meshPath = meshPath;
				emitter.texturePath = texturePath;

				outEffect.emitters.push_back(std::move(emitter));
			}
		}

		return true;
	}

	void ParticleEffectLoader::SerializeEffectJson(const ParticleEffect& effect, json& outRoot)
	{
		WriteValue(outRoot, "version", effect.version);
		WriteValue(outRoot, "name", effect.name);
		WriteValue(outRoot, "oneShot", effect.oneShot);
		WriteValue(outRoot, "seed", effect.seed);

		auto& emitters = outRoot["emitters"];
		emitters = json::array();

		for (const auto& emitter : effect.emitters)
		{
			auto& emitterJson = emitters.emplace_back(json::object());
			WriteValue(emitterJson, "name", emitter.name);
			WriteValue(emitterJson, "enabled", emitter.enabled);
			WriteValue(emitterJson, "simulateInLocalSpace", emitter.simulateInLocalSpace);
			WriteValue(emitterJson, "prewarm", emitter.prewarm);
			WriteValue(emitterJson, "maxParticles", emitter.maxParticles);
			WriteValue(emitterJson, "seed", emitter.seed);

			WriteValue(emitterJson, "renderMode", (int32_t)emitter.renderMode);
			WriteValue(emitterJson, "facingMode", (int32_t)emitter.facingMode);
			WriteValue(emitterJson, "blendMode", (int32_t)emitter.blendMode);
			WriteValue(emitterJson, "softParticles", emitter.softParticles);
			WriteValue(emitterJson, "overrideReceiveLightingEnabled", emitter.overrideReceiveLightingEnabled);
			WriteValue(emitterJson, "overrideReceiveLighting", emitter.overrideReceiveLighting);

			auto& emissionJson = emitterJson["emission"];
			WriteValue(emissionJson, "enabled", emitter.emission.enabled);
			WriteValue(emissionJson, "looping", emitter.emission.looping);
			WriteValue(emissionJson, "duration", emitter.emission.duration);
			WriteValue(emissionJson, "rate", emitter.emission.rate);
			WriteValue(emissionJson, "burst", emitter.emission.burst);
			WriteValue(emissionJson, "prewarmTime", emitter.emission.prewarmTime);

			auto& shapeJson = emitterJson["shape"];
			WriteValue(shapeJson, "enabled", emitter.shape.enabled);
			WriteValue(shapeJson, "type", (int32_t)emitter.shape.type);
			WriteVector3(shapeJson, "boxExtents", emitter.shape.boxExtents);
			WriteValue(shapeJson, "radius", emitter.shape.radius);
			WriteValue(shapeJson, "innerRadius", emitter.shape.innerRadius);
			WriteValue(shapeJson, "coneAngleDegrees", emitter.shape.coneAngleDegrees);
			WriteValue(shapeJson, "lineLength", emitter.shape.lineLength);
			WriteValue(shapeJson, "randomDirection", emitter.shape.randomDirection);

			auto& flipbookJson = emitterJson["flipbook"];
			WriteValue(flipbookJson, "enabled", emitter.flipbook.enabled);
			WriteValue(flipbookJson, "rows", emitter.flipbook.rows);
			WriteValue(flipbookJson, "columns", emitter.flipbook.columns);
			WriteValue(flipbookJson, "framesPerSecond", emitter.flipbook.framesPerSecond);
			WriteValue(flipbookJson, "stretchToLifetime", emitter.flipbook.stretchToLifetime);

			WriteVector2(emitterJson, "lifetimeRange", emitter.lifetimeRange);
			WriteVector2(emitterJson, "speedRange", emitter.speedRange);
			WriteVector2(emitterJson, "sizeRange", emitter.sizeRange);
			WriteVector2(emitterJson, "rotationSpeedRange", emitter.rotationSpeedRange);
			WriteVector3(emitterJson, "axisLock", emitter.axisLock);

			WriteVector4(emitterJson, "startColor", emitter.startColor);
			WriteVector4(emitterJson, "endColor", emitter.endColor);
			WriteVector2(emitterJson, "sizeOverLifetime", emitter.sizeOverLifetime);
			WriteVector2(emitterJson, "speedOverLifetime", emitter.speedOverLifetime);
			WriteVector2(emitterJson, "alphaOverLifetime", emitter.alphaOverLifetime);
			WriteValue(emitterJson, "useThreePointAlphaCurve", emitter.useThreePointAlphaCurve);
			WriteVector3(emitterJson, "alphaOverLifetimeCurve", emitter.alphaOverLifetimeCurve);
			WriteValue(emitterJson, "alphaOverLifetimeCurveMidpoint", emitter.alphaOverLifetimeCurveMidpoint);

			WriteVector3(emitterJson, "constantForce", emitter.constantForce);
			WriteVector3(emitterJson, "gravity", emitter.gravity);
			WriteValue(emitterJson, "drag", emitter.drag);
			WriteValue(emitterJson, "orbitalStrength", emitter.orbitalStrength);
			WriteValue(emitterJson, "noiseAmplitude", emitter.noiseAmplitude);
			WriteValue(emitterJson, "noiseFrequency", emitter.noiseFrequency);
			WriteValue(emitterJson, "velocityInheritance", emitter.velocityInheritance);

			WriteValue(emitterJson, "materialPath", emitter.materialPath.string());
			WriteValue(emitterJson, "meshPath", emitter.meshPath.string());
			WriteValue(emitterJson, "texturePath", emitter.texturePath.string());
		}
	}
}
