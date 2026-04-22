#include "MaterialGraphValidator.hpp"
#include <format>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace HexEngine
{
	namespace
	{
		bool IsCompatible(
			const MaterialGraph& graph,
			const MaterialGraphConnection& connection,
			MaterialGraphValueType from,
			MaterialGraphValueType to)
		{
			if (from == to)
				return true;

			if ((from == MaterialGraphValueType::Scalar && to == MaterialGraphValueType::Vector2) ||
				(from == MaterialGraphValueType::Scalar && to == MaterialGraphValueType::Vector3) ||
				(from == MaterialGraphValueType::Scalar && to == MaterialGraphValueType::Vector4))
			{
				return true;
			}

			if ((from == MaterialGraphValueType::Vector3 && to == MaterialGraphValueType::Vector4) ||
				(from == MaterialGraphValueType::Vector4 && to == MaterialGraphValueType::Vector3))
			{
				return true;
			}

			// Allow texture references to feed material output nodes in v1.
			// The compiler resolves texture objects into sampled values when needed.
			if (from == MaterialGraphValueType::Texture2D &&
				(to == MaterialGraphValueType::Scalar ||
					to == MaterialGraphValueType::Vector2 ||
					to == MaterialGraphValueType::Vector3 ||
					to == MaterialGraphValueType::Vector4))
			{
				return true;
			}

			return false;
		}
	}

	MaterialGraphValidationResult MaterialGraphValidator::Validate(const MaterialGraph& graph)
	{
		MaterialGraphValidationResult result;

		if (graph.nodes.empty())
		{
			result.AddError("Graph contains no nodes.");
			return result;
		}

		std::unordered_set<std::string> nodeIds;
		for (const auto& node : graph.nodes)
		{
			if (node.id.empty())
			{
				result.AddError("Graph contains a node with an empty id.");
				continue;
			}

			if (!nodeIds.insert(node.id).second)
				result.AddError(std::format("Duplicate node id '{}'.", node.id));
		}

		// Validate connections.
		std::unordered_map<std::string, std::vector<std::string>> adjacency;
		std::unordered_map<std::string, int32_t> inDegree;
		for (const auto& node : graph.nodes)
			inDegree[node.id] = 0;

		for (const auto& connection : graph.connections)
		{
			const auto* fromPin = graph.FindPin(connection.fromNodeId, connection.fromPinId, MaterialGraphPinDirection::Output);
			const auto* toPin = graph.FindPin(connection.toNodeId, connection.toPinId, MaterialGraphPinDirection::Input);
			if (fromPin == nullptr || toPin == nullptr)
			{
				result.AddError(std::format(
					"Broken connection '{}:{}' -> '{}:{}'.",
					connection.fromNodeId, connection.fromPinId, connection.toNodeId, connection.toPinId));
				continue;
			}

			if (!IsCompatible(graph, connection, fromPin->valueType, toPin->valueType))
			{
				result.AddError(std::format(
					"Type mismatch for connection '{}:{}' ({}) -> '{}:{}' ({}).",
					connection.fromNodeId,
					connection.fromPinId,
					MaterialGraph::ValueTypeToString(fromPin->valueType),
					connection.toNodeId,
					connection.toPinId,
					MaterialGraph::ValueTypeToString(toPin->valueType)));
			}

			adjacency[connection.fromNodeId].push_back(connection.toNodeId);
			inDegree[connection.toNodeId]++;
		}

		// Detect cycles using Kahn.
		std::queue<std::string> q;
		for (const auto& [nodeId, degree] : inDegree)
		{
			if (degree == 0)
				q.push(nodeId);
		}

		int32_t visited = 0;
		while (!q.empty())
		{
			const auto current = q.front();
			q.pop();
			visited++;

			if (const auto it = adjacency.find(current); it != adjacency.end())
			{
				for (const auto& next : it->second)
				{
					auto degreeIt = inDegree.find(next);
					if (degreeIt == inDegree.end())
						continue;

					degreeIt->second--;
					if (degreeIt->second == 0)
						q.push(next);
				}
			}
		}

		if (visited != static_cast<int32_t>(graph.nodes.size()))
		{
			result.AddError("Graph contains at least one cycle.");
		}

		// Required outputs.
		const auto hasConnectedOutput = [&graph](MaterialGraphOutputSemantic semantic) -> bool
		{
			for (const auto& output : graph.outputs)
			{
				if (output.semantic != semantic)
					continue;

				if (output.nodeId.empty() || output.pinId.empty())
					return false;

				return graph.FindPin(output.nodeId, output.pinId, MaterialGraphPinDirection::Output) != nullptr;
			}
			return false;
		};

		if (!hasConnectedOutput(MaterialGraphOutputSemantic::BaseColor))
			result.AddError("Missing required output connection: BaseColor.");
		if (!hasConnectedOutput(MaterialGraphOutputSemantic::Normal))
			result.AddError("Missing required output connection: Normal.");
		if (!hasConnectedOutput(MaterialGraphOutputSemantic::Roughness))
			result.AddError("Missing required output connection: Roughness.");
		if (!hasConnectedOutput(MaterialGraphOutputSemantic::Metallic))
			result.AddError("Missing required output connection: Metallic.");
		if (!hasConnectedOutput(MaterialGraphOutputSemantic::Emissive))
			result.AddError("Missing required output connection: Emissive.");

		return result;
	}
}
