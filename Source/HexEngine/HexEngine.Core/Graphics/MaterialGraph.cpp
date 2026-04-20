#include "MaterialGraph.hpp"
#include <algorithm>
#include <format>

namespace HexEngine
{
	namespace
	{
		constexpr const char* kTypeScalar = "Scalar";
		constexpr const char* kTypeVec2 = "Vector2";
		constexpr const char* kTypeVec3 = "Vector3";
		constexpr const char* kTypeVec4 = "Vector4";
		constexpr const char* kTypeTex2D = "Texture2D";
		constexpr const char* kTypeUv = "UV";
	}

	MaterialGraphNode* MaterialGraph::FindNode(const std::string& id)
	{
		for (auto& node : nodes)
		{
			if (node.id == id)
				return &node;
		}

		return nullptr;
	}

	const MaterialGraphNode* MaterialGraph::FindNode(const std::string& id) const
	{
		for (const auto& node : nodes)
		{
			if (node.id == id)
				return &node;
		}

		return nullptr;
	}

	const MaterialGraphPin* MaterialGraph::FindPin(const std::string& nodeId, const std::string& pinId, MaterialGraphPinDirection direction) const
	{
		const auto* node = FindNode(nodeId);
		if (node == nullptr)
			return nullptr;

		const auto& pins = direction == MaterialGraphPinDirection::Input ? node->inputPins : node->outputPins;
		for (const auto& pin : pins)
		{
			if (pin.id == pinId)
				return &pin;
		}

		return nullptr;
	}

	void MaterialGraph::EnsureDefaultOutputBindings()
	{
		static const MaterialGraphOutputSemantic semantics[] =
		{
			MaterialGraphOutputSemantic::BaseColor,
			MaterialGraphOutputSemantic::Normal,
			MaterialGraphOutputSemantic::Roughness,
			MaterialGraphOutputSemantic::Metallic,
			MaterialGraphOutputSemantic::Emissive,
			MaterialGraphOutputSemantic::Opacity
		};

		for (const auto semantic : semantics)
		{
			const auto it = std::find_if(outputs.begin(), outputs.end(),
				[semantic](const MaterialGraphOutputBinding& binding)
				{
					return binding.semantic == semantic;
				});

			if (it == outputs.end())
			{
				MaterialGraphOutputBinding b;
				b.semantic = semantic;
				outputs.push_back(std::move(b));
			}
		}
	}

	MaterialGraph MaterialGraph::CreateDefaultPbrGraph()
	{
		MaterialGraph graph;
		graph.version = kVersion;

		MaterialGraphNode albedo;
		albedo.id = "node_albedo";
		albedo.nodeType = MaterialGraphNodeType::VectorConstant;
		albedo.displayName = "Base Color";
		albedo.position = math::Vector2(80.0f, 80.0f);
		albedo.vectorValue = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		albedo.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(albedo);

		MaterialGraphNode normal;
		normal.id = "node_normal";
		normal.nodeType = MaterialGraphNodeType::VectorConstant;
		normal.displayName = "Normal";
		normal.position = math::Vector2(80.0f, 160.0f);
		normal.vectorValue = math::Vector4(0.5f, 0.5f, 1.0f, 1.0f);
		normal.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(normal);

		MaterialGraphNode roughness;
		roughness.id = "node_roughness";
		roughness.nodeType = MaterialGraphNodeType::ScalarConstant;
		roughness.displayName = "Roughness";
		roughness.position = math::Vector2(80.0f, 240.0f);
		roughness.scalarValue = 0.5f;
		roughness.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(roughness);

		MaterialGraphNode metallic;
		metallic.id = "node_metallic";
		metallic.nodeType = MaterialGraphNodeType::ScalarConstant;
		metallic.displayName = "Metallic";
		metallic.position = math::Vector2(80.0f, 320.0f);
		metallic.scalarValue = 0.0f;
		metallic.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(metallic);

		MaterialGraphNode emissive;
		emissive.id = "node_emissive";
		emissive.nodeType = MaterialGraphNodeType::VectorConstant;
		emissive.displayName = "Emissive";
		emissive.position = math::Vector2(80.0f, 400.0f);
		emissive.vectorValue = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		emissive.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(emissive);

		MaterialGraphNode opacity;
		opacity.id = "node_opacity";
		opacity.nodeType = MaterialGraphNodeType::ScalarConstant;
		opacity.displayName = "Opacity";
		opacity.position = math::Vector2(80.0f, 480.0f);
		opacity.scalarValue = 1.0f;
		opacity.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(opacity);

		auto makeOutputNode = [](const char* id, const char* label, const math::Vector2& pos, MaterialGraphValueType inType)
		{
			MaterialGraphNode node;
			node.id = id;
			node.nodeType = MaterialGraphNodeType::Output;
			node.displayName = label;
			node.position = pos;
			node.inputPins.push_back({ "In", "In", inType, MaterialGraphPinDirection::Input });
			return node;
		};

		graph.nodes.push_back(makeOutputNode("output_basecolor", "BaseColor", math::Vector2(620.0f, 80.0f), MaterialGraphValueType::Vector4));
		graph.nodes.push_back(makeOutputNode("output_normal", "Normal", math::Vector2(620.0f, 160.0f), MaterialGraphValueType::Vector4));
		graph.nodes.push_back(makeOutputNode("output_roughness", "Roughness", math::Vector2(620.0f, 240.0f), MaterialGraphValueType::Scalar));
		graph.nodes.push_back(makeOutputNode("output_metallic", "Metallic", math::Vector2(620.0f, 320.0f), MaterialGraphValueType::Scalar));
		graph.nodes.push_back(makeOutputNode("output_emissive", "Emissive", math::Vector2(620.0f, 400.0f), MaterialGraphValueType::Vector4));
		graph.nodes.push_back(makeOutputNode("output_opacity", "Opacity", math::Vector2(620.0f, 480.0f), MaterialGraphValueType::Scalar));

		graph.connections =
		{
			{ "node_albedo", "out", "output_basecolor", "In" },
			{ "node_normal", "out", "output_normal", "In" },
			{ "node_roughness", "out", "output_roughness", "In" },
			{ "node_metallic", "out", "output_metallic", "In" },
			{ "node_emissive", "out", "output_emissive", "In" },
			{ "node_opacity", "out", "output_opacity", "In" }
		};

		graph.outputs =
		{
			{ MaterialGraphOutputSemantic::BaseColor, "node_albedo", "out" },
			{ MaterialGraphOutputSemantic::Normal, "node_normal", "out" },
			{ MaterialGraphOutputSemantic::Roughness, "node_roughness", "out" },
			{ MaterialGraphOutputSemantic::Metallic, "node_metallic", "out" },
			{ MaterialGraphOutputSemantic::Emissive, "node_emissive", "out" },
			{ MaterialGraphOutputSemantic::Opacity, "node_opacity", "out" }
		};

		return graph;
	}

	bool MaterialGraph::ParseValueType(const std::string& text, MaterialGraphValueType& outType)
	{
		if (text == kTypeScalar) { outType = MaterialGraphValueType::Scalar; return true; }
		if (text == kTypeVec2) { outType = MaterialGraphValueType::Vector2; return true; }
		if (text == kTypeVec3) { outType = MaterialGraphValueType::Vector3; return true; }
		if (text == kTypeVec4) { outType = MaterialGraphValueType::Vector4; return true; }
		if (text == kTypeTex2D) { outType = MaterialGraphValueType::Texture2D; return true; }
		if (text == kTypeUv) { outType = MaterialGraphValueType::UV; return true; }
		return false;
	}

	const char* MaterialGraph::ValueTypeToString(MaterialGraphValueType type)
	{
		switch (type)
		{
		default:
		case MaterialGraphValueType::Scalar: return kTypeScalar;
		case MaterialGraphValueType::Vector2: return kTypeVec2;
		case MaterialGraphValueType::Vector3: return kTypeVec3;
		case MaterialGraphValueType::Vector4: return kTypeVec4;
		case MaterialGraphValueType::Texture2D: return kTypeTex2D;
		case MaterialGraphValueType::UV: return kTypeUv;
		}
	}

	bool MaterialGraph::ParseNodeType(const std::string& text, MaterialGraphNodeType& outType)
	{
		if (text == "Output") { outType = MaterialGraphNodeType::Output; return true; }
		if (text == "ScalarConstant") { outType = MaterialGraphNodeType::ScalarConstant; return true; }
		if (text == "VectorConstant") { outType = MaterialGraphNodeType::VectorConstant; return true; }
		if (text == "TextureSample") { outType = MaterialGraphNodeType::TextureSample; return true; }
		if (text == "TexCoord") { outType = MaterialGraphNodeType::TexCoord; return true; }
		if (text == "Add") { outType = MaterialGraphNodeType::Add; return true; }
		if (text == "Multiply") { outType = MaterialGraphNodeType::Multiply; return true; }
		if (text == "Lerp") { outType = MaterialGraphNodeType::Lerp; return true; }
		if (text == "OneMinus") { outType = MaterialGraphNodeType::OneMinus; return true; }
		if (text == "NormalMap") { outType = MaterialGraphNodeType::NormalMap; return true; }
		if (text == "ScalarParameter") { outType = MaterialGraphNodeType::ScalarParameter; return true; }
		if (text == "VectorParameter") { outType = MaterialGraphNodeType::VectorParameter; return true; }
		if (text == "TextureParameter") { outType = MaterialGraphNodeType::TextureParameter; return true; }
		return false;
	}

	const char* MaterialGraph::NodeTypeToString(MaterialGraphNodeType type)
	{
		switch (type)
		{
		default:
		case MaterialGraphNodeType::Output: return "Output";
		case MaterialGraphNodeType::ScalarConstant: return "ScalarConstant";
		case MaterialGraphNodeType::VectorConstant: return "VectorConstant";
		case MaterialGraphNodeType::TextureSample: return "TextureSample";
		case MaterialGraphNodeType::TexCoord: return "TexCoord";
		case MaterialGraphNodeType::Add: return "Add";
		case MaterialGraphNodeType::Multiply: return "Multiply";
		case MaterialGraphNodeType::Lerp: return "Lerp";
		case MaterialGraphNodeType::OneMinus: return "OneMinus";
		case MaterialGraphNodeType::NormalMap: return "NormalMap";
		case MaterialGraphNodeType::ScalarParameter: return "ScalarParameter";
		case MaterialGraphNodeType::VectorParameter: return "VectorParameter";
		case MaterialGraphNodeType::TextureParameter: return "TextureParameter";
		}
	}

	bool MaterialGraph::ParseOutputSemantic(const std::string& text, MaterialGraphOutputSemantic& outSemantic)
	{
		if (text == "BaseColor") { outSemantic = MaterialGraphOutputSemantic::BaseColor; return true; }
		if (text == "Normal") { outSemantic = MaterialGraphOutputSemantic::Normal; return true; }
		if (text == "Roughness") { outSemantic = MaterialGraphOutputSemantic::Roughness; return true; }
		if (text == "Metallic") { outSemantic = MaterialGraphOutputSemantic::Metallic; return true; }
		if (text == "Emissive") { outSemantic = MaterialGraphOutputSemantic::Emissive; return true; }
		if (text == "Opacity") { outSemantic = MaterialGraphOutputSemantic::Opacity; return true; }
		return false;
	}

	const char* MaterialGraph::OutputSemanticToString(MaterialGraphOutputSemantic semantic)
	{
		switch (semantic)
		{
		default:
		case MaterialGraphOutputSemantic::BaseColor: return "BaseColor";
		case MaterialGraphOutputSemantic::Normal: return "Normal";
		case MaterialGraphOutputSemantic::Roughness: return "Roughness";
		case MaterialGraphOutputSemantic::Metallic: return "Metallic";
		case MaterialGraphOutputSemantic::Emissive: return "Emissive";
		case MaterialGraphOutputSemantic::Opacity: return "Opacity";
		}
	}

	void MaterialGraph::Serialize(json& graphJson, const MaterialGraph& graph)
	{
		graphJson["version"] = graph.version;
		graphJson["nodes"] = json::array();
		graphJson["connections"] = json::array();
		graphJson["outputs"] = json::array();
		graphJson["parameters"] = json::array();

		for (const auto& node : graph.nodes)
		{
			auto& n = graphJson["nodes"].emplace_back();
			n["id"] = node.id;
			n["nodeType"] = NodeTypeToString(node.nodeType);
			n["displayName"] = node.displayName;
			n["position"] = { node.position.x, node.position.y };
			n["scalarValue"] = node.scalarValue;
			n["vectorValue"] = { node.vectorValue.x, node.vectorValue.y, node.vectorValue.z, node.vectorValue.w };
			n["texturePath"] = node.texturePath.generic_string();
			n["parameterName"] = node.parameterName;
			n["isExposedParameter"] = node.isExposedParameter;

			n["inputPins"] = json::array();
			for (const auto& pin : node.inputPins)
			{
				n["inputPins"].push_back(
					{
						{ "id", pin.id },
						{ "name", pin.name },
						{ "valueType", ValueTypeToString(pin.valueType) }
					});
			}

			n["outputPins"] = json::array();
			for (const auto& pin : node.outputPins)
			{
				n["outputPins"].push_back(
					{
						{ "id", pin.id },
						{ "name", pin.name },
						{ "valueType", ValueTypeToString(pin.valueType) }
					});
			}
		}

		for (const auto& connection : graph.connections)
		{
			graphJson["connections"].push_back(
				{
					{ "fromNodeId", connection.fromNodeId },
					{ "fromPinId", connection.fromPinId },
					{ "toNodeId", connection.toNodeId },
					{ "toPinId", connection.toPinId }
				});
		}

		for (const auto& output : graph.outputs)
		{
			graphJson["outputs"].push_back(
				{
					{ "semantic", OutputSemanticToString(output.semantic) },
					{ "nodeId", output.nodeId },
					{ "pinId", output.pinId }
				});
		}

		for (const auto& parameter : graph.parameters)
		{
			graphJson["parameters"].push_back(
				{
					{ "name", parameter.name },
					{ "valueType", ValueTypeToString(parameter.valueType) },
					{ "scalarValue", parameter.scalarValue },
					{ "vectorValue", { parameter.vectorValue.x, parameter.vectorValue.y, parameter.vectorValue.z, parameter.vectorValue.w } },
					{ "texturePath", parameter.texturePath.generic_string() },
					{ "isExposed", parameter.isExposed }
				});
		}
	}

	bool MaterialGraph::Deserialize(const json& graphJson, MaterialGraph& outGraph, std::vector<std::string>* errors)
	{
		outGraph = {};
		outGraph.version = graphJson.value("version", kVersion);

		if (const auto it = graphJson.find("nodes"); it != graphJson.end() && it->is_array())
		{
			for (const auto& n : *it)
			{
				MaterialGraphNode node;
				node.id = n.value("id", std::string());
				node.displayName = n.value("displayName", std::string());
				node.scalarValue = n.value("scalarValue", 0.0f);
				node.parameterName = n.value("parameterName", std::string());
				node.texturePath = n.value("texturePath", std::string());
				node.isExposedParameter = n.value("isExposedParameter", true);

				if (const auto typeStr = n.value("nodeType", std::string()); !ParseNodeType(typeStr, node.nodeType))
				{
					if (errors != nullptr)
						errors->push_back(std::format("Unknown nodeType '{}'", typeStr));
					continue;
				}

				const auto pos = n.find("position");
				if (pos != n.end() && pos->is_array() && pos->size() >= 2)
				{
					node.position.x = (*pos)[0].get<float>();
					node.position.y = (*pos)[1].get<float>();
				}

				const auto vec = n.find("vectorValue");
				if (vec != n.end() && vec->is_array() && vec->size() >= 4)
				{
					node.vectorValue.x = (*vec)[0].get<float>();
					node.vectorValue.y = (*vec)[1].get<float>();
					node.vectorValue.z = (*vec)[2].get<float>();
					node.vectorValue.w = (*vec)[3].get<float>();
				}

				const auto parsePins = [](const json& pinArray, MaterialGraphPinDirection direction, std::vector<MaterialGraphPin>& outPins)
				{
					if (!pinArray.is_array())
						return;

					for (const auto& p : pinArray)
					{
						MaterialGraphPin pin;
						pin.id = p.value("id", std::string());
						pin.name = p.value("name", std::string());
						pin.direction = direction;
						MaterialGraph::ParseValueType(p.value("valueType", std::string("Scalar")), pin.valueType);
						outPins.push_back(std::move(pin));
					}
				};

				if (const auto pins = n.find("inputPins"); pins != n.end())
					parsePins(*pins, MaterialGraphPinDirection::Input, node.inputPins);
				if (const auto pins = n.find("outputPins"); pins != n.end())
					parsePins(*pins, MaterialGraphPinDirection::Output, node.outputPins);

				outGraph.nodes.push_back(std::move(node));
			}
		}

		if (const auto it = graphJson.find("connections"); it != graphJson.end() && it->is_array())
		{
			for (const auto& c : *it)
			{
				MaterialGraphConnection connection;
				connection.fromNodeId = c.value("fromNodeId", std::string());
				connection.fromPinId = c.value("fromPinId", std::string());
				connection.toNodeId = c.value("toNodeId", std::string());
				connection.toPinId = c.value("toPinId", std::string());
				outGraph.connections.push_back(std::move(connection));
			}
		}

		if (const auto it = graphJson.find("outputs"); it != graphJson.end() && it->is_array())
		{
			for (const auto& o : *it)
			{
				MaterialGraphOutputBinding output;
				if (!ParseOutputSemantic(o.value("semantic", std::string()), output.semantic))
					continue;

				output.nodeId = o.value("nodeId", std::string());
				output.pinId = o.value("pinId", std::string());
				outGraph.outputs.push_back(std::move(output));
			}
		}

		if (const auto it = graphJson.find("parameters"); it != graphJson.end() && it->is_array())
		{
			for (const auto& p : *it)
			{
				MaterialGraphParameter parameter;
				parameter.name = p.value("name", std::string());
				parameter.scalarValue = p.value("scalarValue", 0.0f);
				parameter.texturePath = p.value("texturePath", std::string());
				parameter.isExposed = p.value("isExposed", true);
				ParseValueType(p.value("valueType", std::string("Scalar")), parameter.valueType);
				if (const auto vec = p.find("vectorValue"); vec != p.end() && vec->is_array() && vec->size() >= 4)
				{
					parameter.vectorValue = math::Vector4((*vec)[0].get<float>(), (*vec)[1].get<float>(), (*vec)[2].get<float>(), (*vec)[3].get<float>());
				}

				outGraph.parameters.push_back(std::move(parameter));
			}
		}

		outGraph.EnsureDefaultOutputBindings();
		return true;
	}

	void MaterialGraph::SerializeInstance(json& instanceJson, const MaterialGraphInstanceData& instance)
	{
		instanceJson["parentMaterialPath"] = instance.parentMaterialPath.generic_string();
		instanceJson["overrides"] = json::array();

		for (const auto& overrideValue : instance.overrides)
		{
			instanceJson["overrides"].push_back(
				{
					{ "name", overrideValue.name },
					{ "valueType", ValueTypeToString(overrideValue.valueType) },
					{ "scalarValue", overrideValue.scalarValue },
					{ "vectorValue", { overrideValue.vectorValue.x, overrideValue.vectorValue.y, overrideValue.vectorValue.z, overrideValue.vectorValue.w } },
					{ "texturePath", overrideValue.texturePath.generic_string() }
				});
		}
	}

	bool MaterialGraph::DeserializeInstance(const json& instanceJson, MaterialGraphInstanceData& outInstance, std::vector<std::string>* errors)
	{
		(void)errors;
		outInstance = {};
		outInstance.parentMaterialPath = instanceJson.value("parentMaterialPath", std::string());
		if (const auto it = instanceJson.find("overrides"); it != instanceJson.end() && it->is_array())
		{
			for (const auto& item : *it)
			{
				MaterialGraphParameterOverride overrideValue;
				overrideValue.name = item.value("name", std::string());
				overrideValue.scalarValue = item.value("scalarValue", 0.0f);
				overrideValue.texturePath = item.value("texturePath", std::string());
				ParseValueType(item.value("valueType", std::string("Scalar")), overrideValue.valueType);

				if (const auto vec = item.find("vectorValue"); vec != item.end() && vec->is_array() && vec->size() >= 4)
				{
					overrideValue.vectorValue = math::Vector4((*vec)[0].get<float>(), (*vec)[1].get<float>(), (*vec)[2].get<float>(), (*vec)[3].get<float>());
				}

				outInstance.overrides.push_back(std::move(overrideValue));
			}
		}

		return true;
	}
}
