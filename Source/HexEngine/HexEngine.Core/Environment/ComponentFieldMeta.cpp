
#include "ComponentFieldMeta.hpp"

#include <map>

namespace HexEngine
{
	namespace
	{
		using FieldMap = std::map<std::string, ComponentFieldInfo>;

		std::map<std::string, FieldMap>& Registry()
		{
			static std::map<std::string, FieldMap> s_registry;
			return s_registry;
		}

		ComponentFieldInfo Desc(const char* description, const char* units = "")
		{
			ComponentFieldInfo i;
			i.description = description;
			i.units = units;
			return i;
		}

		ComponentFieldInfo Range(const char* description, double minValue, double maxValue, const char* units = "")
		{
			ComponentFieldInfo i = Desc(description, units);
			i.hasRange = true;
			i.minValue = minValue;
			i.maxValue = maxValue;
			return i;
		}

		ComponentFieldInfo Enum(const char* description, std::vector<std::pair<std::string, int64_t>> values, bool flags = false)
		{
			ComponentFieldInfo i = Desc(description);
			i.enumValues = std::move(values);
			i.isFlags = flags;
			return i;
		}
	}

	void ComponentFieldMeta::Register(const std::string& component, const std::string& field, const ComponentFieldInfo& info)
	{
		Registry()[component][field] = info;
	}

	const ComponentFieldInfo* ComponentFieldMeta::Find(const std::string& component, const std::string& field)
	{
		auto& reg = Registry();
		auto c = reg.find(component);
		if (c == reg.end())
			return nullptr;
		auto f = c->second.find(field);
		return f == c->second.end() ? nullptr : &f->second;
	}

	std::vector<std::pair<std::string, ComponentFieldInfo>> ComponentFieldMeta::ForComponent(const std::string& component)
	{
		std::vector<std::pair<std::string, ComponentFieldInfo>> out;
		auto& reg = Registry();
		if (auto c = reg.find(component); c != reg.end())
			for (const auto& [field, info] : c->second)
				out.emplace_back(field, info);
		return out;
	}

	void ComponentFieldMeta::RegisterBuiltIns()
	{
		// --- Lights (Light base fields, shared by every concrete light type) ---
		for (const char* light : { "PointLight", "SpotLight", "AreaLight", "DirectionalLight" })
		{
			Register(light, "_diffuseColour", Desc("Light colour, RGBA 0..1."));
			Register(light, "_effect", Enum("Animated lighting effect.", { {"None", 0}, {"SlowRandomPulse", 1} }));
			Register(light, "_strength", Range("Intensity.", 0.0, 1000.0));
			Register(light, "_radius", Range("Attenuation range (unused by DirectionalLight).", 0.0, 10000.0, "m"));
			Register(light, "_doesCastShadows", Desc("Render a shadow map for this light (costly for point/spot lights)."));
			Register(light, "_injectIntoGI", Desc("Contribute to voxel global illumination."));
			Register(light, "_lightMultiplier", Range("Extra multiplier on strength (scripted dimming/flicker).", 0.0, 100.0));
		}
		Register("SpotLight", "_outerConeAngle", Range("Outer cone angle; light fades to zero here.", 1.0, 179.0, "degrees"));
		Register("SpotLight", "_innerConeAngle", Range("Inner cone angle; full intensity inside. Keep below the outer angle.", 0.0, 179.0, "degrees"));
		Register("AreaLight", "_shape", Enum("Emitter shape.", { {"Tube", 0}, {"Rect", 1} }));
		Register("AreaLight", "_width", Range("Tube length / rect width.", 0.0, 1000.0, "m"));
		Register("AreaLight", "_height", Range("Rect height (Rect shape only).", 0.0, 1000.0, "m"));
		Register("AreaLight", "_tubeRadius", Range("Tube radius (Tube shape only).", 0.0, 100.0, "m"));
		Register("AreaLight", "_twoSided", Desc("Rect emits from both faces."));
		Register("AreaLight", "_texturePath", Desc("Optional image projected by a Rect light (asset path)."));

		// --- Physics ---
		Register("RigidBody", "_bodyType", Enum("Simulation type.", { {"None", 0}, {"Static", 1}, {"Kinematic", 2}, {"Dynamic", 3} }));
		Register("RigidBody", "_shape", Enum("Collider shape (shapeData holds the matching box/sphere/capsule parameters).",
			{ {"None", 0}, {"Box", 1}, {"Sphere", 2}, {"Capsule", 3}, {"HeightField", 4}, {"TriangleMesh", 5} }));
		Register("RigidBody", "_mass", Range("Mass (Dynamic bodies).", 0.0, 1000000.0, "kg"));
		Register("RigidBody", "_isTrigger", Desc("Reports overlaps instead of colliding."));
		Register("RigidBody", "_isGravityApplied", Desc("Affected by gravity (Dynamic bodies)."));
		Register("RigidBody", "_exclusive", Desc("Shape is exclusive to this actor."));

		// --- Rendering ---
		Register("StaticMeshComponent", "_shadowCullingMode", Enum("Face culling when rendering into shadow maps.",
			{ {"Invalid", -1}, {"NoCulling", 0}, {"BackFace", 1}, {"FrontFace", 2} }));
		Register("StaticMeshComponent", "_uvScale", Desc("UV tiling multiplier [u, v]."));
		Register("StaticMeshComponent", "_excludeFromGI", Desc("Skip this mesh in voxel GI (use for moving/transient meshes)."));
		Register("Camera", "_fsrQuality", Enum("FSR upscaler quality mode.",
			{ {"NativeAA", 0}, {"Quality", 1}, {"Balanced", 2}, {"Performance", 3}, {"UltraPerformance", 4} }));
		Register("Camera", "_effects", Enum("Per-camera effect bits.", { {"SSR", 1} }, true));
		Register("Camera", "_cameraAngles", Desc("[pitch, yaw, roll] in degrees; pitch + looks up, yaw 0 faces -Z.", "degrees"));

		// --- Gameplay / world ---
		Register("NavMeshBlockingVolume", "_mode", Enum("What the volume does to the navmesh.", { {"Block", 0}, {"ForceWalkable", 1} }));
		Register("NetworkComponent", "_syncFlags", Enum("Replicated state bits.", { {"SyncTransform", 1} }, true));
		Register("DoorComponent", "_easing", Desc("Swing easing curve: -1 = linear, otherwise an easing_functions value (5 = EaseInOutQuad; see Math/easing.h)."));
		Register("DoorComponent", "_openAngle", Range("Open angle.", -180.0, 180.0, "degrees"));
		Register("InteractionComponent", "_interactKey", Desc("Virtual-key code of the interact key (69 = 'E')."));
		Register("InteractionComponent", "_interactionRange", Range("Max interaction distance.", 0.0, 1000.0, "m"));
		Register("DayNightCycleComponent", "_currentTimeHours", Range("Time of day.", 0.0, 24.0, "hours"));
		Register("DayNightCycleComponent", "_simulateCycle", Desc("Advance time automatically; false freezes the time of day."));
		Register("DayNightCycleComponent", "_sunYawDegrees", Range("Compass direction of the sun's path.", -360.0, 360.0, "degrees"));
		Register("SoundEffectComponent", "_volume", Range("Playback volume.", 0.0, 1.0));
		Register("SoundEffectComponent", "_radius", Range("3D audible radius.", 0.0, 10000.0, "m"));
		Register("MotorComponent", "_speed", Desc("Constant movement speed along _direction.", "m/s"));
		Register("MotorComponent", "_direction", Desc("World-space movement direction [x, y, z] (not normalised)."));
		Register("TimedLifetimeComponent", "_remainingSeconds", Range("Seconds until the entity destroys itself.", 0.0, 1.0e9, "seconds"));
		Register("HealthComponent", "_health", Desc("Current health."));
		Register("HealthComponent", "_maxHealth", Desc("Maximum health."));
	}
}
