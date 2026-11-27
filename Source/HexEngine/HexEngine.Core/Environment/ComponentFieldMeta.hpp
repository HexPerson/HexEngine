
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	// Optional, human/agent-facing metadata for a component's SERIALISED fields
	// (the keys its Serialize() writes). Serialize/Deserialize stay the source of
	// truth for what a field is and how it round-trips; this only adds what the
	// JSON can't say on its own: enum value names, bit-flag names, sensible
	// ranges, units and a one-line description.
	//
	// Consumers: the editor bridge (describe_component_type / inspect_component)
	// so tools and agents can write `"_bodyType": "Dynamic"` instead of guessing
	// magic numbers. Purely additive - a field with no metadata still works.
	//
	// Keyed by the registered component class name and field key, so a plugin can
	// describe its own components the same way (call Register from its
	// registration hook, after REG_CLASS). Not thread-safe for writes: register
	// at startup on the main thread.
	struct ComponentFieldInfo
	{
		std::string description;
		std::string units;                                    // e.g. "m", "degrees", "seconds"
		std::vector<std::pair<std::string, int64_t>> enumValues; // name -> value
		bool isFlags = false;                                 // enumValues are OR-able bits
		bool hasRange = false;
		double minValue = 0.0;
		double maxValue = 0.0;
	};

	class HEX_API ComponentFieldMeta
	{
	public:
		static void Register(const std::string& component, const std::string& field, const ComponentFieldInfo& info);

		// nullptr when the field has no metadata.
		static const ComponentFieldInfo* Find(const std::string& component, const std::string& field);

		// All described fields of a component (field key -> info), copied.
		static std::vector<std::pair<std::string, ComponentFieldInfo>> ForComponent(const std::string& component);

		// Describe the built-in Core components. Called once from
		// ClassRegistry::RegisterAllClasses.
		static void RegisterBuiltIns();
	};
}
