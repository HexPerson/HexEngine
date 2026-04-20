#pragma once

#include "../Required.hpp"
#include "MaterialGraph.hpp"

namespace HexEngine
{
	struct MaterialGraphValidationMessage
	{
		enum class Severity : uint8_t
		{
			Error = 0,
			Warning
		};

		Severity severity = Severity::Error;
		std::string message;
	};

	struct MaterialGraphValidationResult
	{
		std::vector<MaterialGraphValidationMessage> messages;

		bool HasErrors() const
		{
			for (const auto& message : messages)
			{
				if (message.severity == MaterialGraphValidationMessage::Severity::Error)
					return true;
			}
			return false;
		}

		void AddError(const std::string& message)
		{
			messages.push_back({ MaterialGraphValidationMessage::Severity::Error, message });
		}

		void AddWarning(const std::string& message)
		{
			messages.push_back({ MaterialGraphValidationMessage::Severity::Warning, message });
		}
	};

	class HEX_API MaterialGraphValidator
	{
	public:
		static MaterialGraphValidationResult Validate(const MaterialGraph& graph);
	};
}
