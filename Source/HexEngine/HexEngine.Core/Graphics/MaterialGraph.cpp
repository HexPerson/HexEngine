#include "MaterialGraph.hpp"
#include "Material.hpp"
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
			MaterialGraphOutputSemantic::Opacity,
			MaterialGraphOutputSemantic::Smoothness
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

		MaterialGraphNode smoothness;
		smoothness.id = "node_smoothness";
		smoothness.nodeType = MaterialGraphNodeType::ScalarConstant;
		smoothness.displayName = "Smoothness";
		smoothness.position = math::Vector2(80.0f, 560.0f);
		smoothness.scalarValue = 0.0f;
		smoothness.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output });
		graph.nodes.push_back(smoothness);

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
		graph.nodes.push_back(makeOutputNode("output_smoothness", "Smoothness", math::Vector2(620.0f, 560.0f), MaterialGraphValueType::Scalar));

		graph.connections =
		{
			{ "node_albedo", "out", "output_basecolor", "In" },
			{ "node_normal", "out", "output_normal", "In" },
			{ "node_roughness", "out", "output_roughness", "In" },
			{ "node_metallic", "out", "output_metallic", "In" },
			{ "node_emissive", "out", "output_emissive", "In" },
			{ "node_opacity", "out", "output_opacity", "In" },
			{ "node_smoothness", "out", "output_smoothness", "In" }
		};

		graph.outputs =
		{
			{ MaterialGraphOutputSemantic::BaseColor, "node_albedo", "out" },
			{ MaterialGraphOutputSemantic::Normal, "node_normal", "out" },
			{ MaterialGraphOutputSemantic::Roughness, "node_roughness", "out" },
			{ MaterialGraphOutputSemantic::Metallic, "node_metallic", "out" },
			{ MaterialGraphOutputSemantic::Emissive, "node_emissive", "out" },
			{ MaterialGraphOutputSemantic::Opacity, "node_opacity", "out" },
			{ MaterialGraphOutputSemantic::Smoothness, "node_smoothness", "out" }
		};

		return graph;
	}

	MaterialGraph MaterialGraph::CreateFromStandardMaterial(const Material& material)
	{
		// Originally a private helper inside MaterialGraphDialog.cpp; promoted to
		// MaterialGraph so the AssetExplorer's "Convert to material graph" context
		// menu can call it without having to open the graph dialog first.

		MaterialGraph graph;
		graph.version = kVersion;

		auto makeNode = [](const std::string& id, MaterialGraphNodeType type, const std::string& name, const math::Vector2& position)
		{
			MaterialGraphNode node;
			node.id = id;
			node.nodeType = type;
			node.displayName = name;
			node.position = position;
			return node;
		};

		auto addConnection = [&graph](const std::string& fromNode, const std::string& fromPin, const std::string& toNode, const std::string& toPin)
		{
			graph.connections.push_back({ fromNode, fromPin, toNode, toPin });
		};

		auto addVectorConstant = [&](const char* id, const char* label, const math::Vector2& pos, const math::Vector4& value)
		{
			auto node = makeNode(id, MaterialGraphNodeType::VectorConstant, label, pos);
			node.vectorValue = value;
			node.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
			graph.nodes.push_back(std::move(node));
		};

		auto addScalarConstant = [&](const char* id, const char* label, const math::Vector2& pos, float value)
		{
			auto node = makeNode(id, MaterialGraphNodeType::ScalarConstant, label, pos);
			node.scalarValue = value;
			node.outputPins.push_back({ "out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output });
			graph.nodes.push_back(std::move(node));
		};

		// Builds a TextureParameter (+ optional NormalMap or TextureSample) chain
		// for one of the standard material's bound texture slots. Returns the node
		// id whose "Out" pin should be wired to the corresponding output node, or
		// empty if the material has no texture bound for that slot (caller falls
		// back to a Vector/Scalar constant in that case).
		auto addTextureChain = [&](MaterialTexture textureType, const char* paramId, const char* sampleId, const char* label, const math::Vector2& pos, bool useNormalMap) -> std::string
		{
			const auto texture = material.GetTexture(textureType);
			if (!texture)
				return {};

			auto textureNode = makeNode(paramId, MaterialGraphNodeType::TextureParameter, label, pos);
			textureNode.texturePath = texture->GetFileSystemPath();
			textureNode.outputPins.push_back({ "Out", "Out", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Output });
			graph.nodes.push_back(std::move(textureNode));

			if (useNormalMap)
			{
				auto normalNode = makeNode(sampleId, MaterialGraphNodeType::NormalMap, "NormalMap", pos + math::Vector2(180.0f, 0.0f));
				normalNode.inputPins.push_back({ "Normal", "Normal", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Input });
				normalNode.outputPins.push_back({ "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
				graph.nodes.push_back(std::move(normalNode));
				addConnection(paramId, "Out", sampleId, "Normal");
			}
			else
			{
				auto sampleNode = makeNode(sampleId, MaterialGraphNodeType::TextureSample, "TextureSample", pos + math::Vector2(180.0f, 0.0f));
				sampleNode.inputPins.push_back({ "Tex", "Tex", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Input });
				sampleNode.inputPins.push_back({ "UV", "UV", MaterialGraphValueType::UV, MaterialGraphPinDirection::Input });
				sampleNode.outputPins.push_back({ "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
				graph.nodes.push_back(std::move(sampleNode));
				addConnection(paramId, "Out", sampleId, "Tex");
			}

			return sampleId;
		};

		// Pre-multiply emissive by the alpha-channel strength so the graph encodes
		// the same lit emission the standard material was producing.
		const math::Vector3 emissive(
			material._properties.emissiveColour.x * material._properties.emissiveColour.w,
			material._properties.emissiveColour.y * material._properties.emissiveColour.w,
			material._properties.emissiveColour.z * material._properties.emissiveColour.w);

		std::string baseColorSource = addTextureChain(MaterialTexture::Albedo, "node_albedo_tex", "node_albedo_sample", "Albedo Texture", math::Vector2(40.0f, 60.0f), false);
		if (baseColorSource.empty())
		{
			addVectorConstant("node_albedo", "Base Color", math::Vector2(80.0f, 80.0f), material._properties.diffuseColour);
			baseColorSource = "node_albedo";
		}

		std::string normalSource = addTextureChain(MaterialTexture::Normal, "node_normal_tex", "node_normal_map", "Normal Texture", math::Vector2(40.0f, 150.0f), true);
		if (normalSource.empty())
		{
			addVectorConstant("node_normal", "Normal", math::Vector2(80.0f, 160.0f), math::Vector4(0.5f, 0.5f, 1.0f, 1.0f));
			normalSource = "node_normal";
		}

		std::string roughnessSource = addTextureChain(MaterialTexture::Roughness, "node_roughness_tex", "node_roughness_sample", "Roughness Texture", math::Vector2(40.0f, 230.0f), false);
		if (roughnessSource.empty())
		{
			addScalarConstant("node_roughness", "Roughness", math::Vector2(80.0f, 240.0f), material._properties.roughnessFactor);
			roughnessSource = "node_roughness";
		}

		std::string metallicSource = addTextureChain(MaterialTexture::Metallic, "node_metallic_tex", "node_metallic_sample", "Metallic Texture", math::Vector2(40.0f, 310.0f), false);
		if (metallicSource.empty())
		{
			addScalarConstant("node_metallic", "Metallic", math::Vector2(80.0f, 320.0f), material._properties.metallicFactor);
			metallicSource = "node_metallic";
		}

		// Emission is special - the standard DefaultPixel shader gates emission on
		// emissiveColour.a (strength), so a material with an emission texture bound
		// but strength=0 produces no emission. The naive conversion that just wires
		// TextureSample.Out -> Emissive ignored this and dumped the raw texture as
		// emission, which (because emission is added on top of the lit albedo and
		// bypasses lighting) showed up as "fully bright / unlit" surfaces on any
		// material whose Emission slot was filled by a Simplygon-style bulk importer
		// even when the author never intended emission. We now mirror the standard
		// shader exactly: TextureSample * (tint.rgb * strength) -> Emissive.
		std::string emissiveSource;
		const std::string emissiveTextureSourceId = addTextureChain(MaterialTexture::Emission, "node_emissive_tex", "node_emissive_sample", "Emission Texture", math::Vector2(40.0f, 390.0f), false);
		if (!emissiveTextureSourceId.empty())
		{
			// Tint+strength constant; equivalent to g_material.emissiveColour.rgb *
			// emissiveColour.a in DefaultPixel.shader. Stored as a Vector4 with
			// alpha=1 so the Multiply downstream doesn't accidentally zero the .a.
			addVectorConstant("node_emissive_tint", "Emission Tint",
				math::Vector2(40.0f, 420.0f),
				math::Vector4(emissive.x, emissive.y, emissive.z, 1.0f));

			auto multiplyNode = makeNode("node_emissive_mul", MaterialGraphNodeType::Multiply, "Emission * Tint", math::Vector2(260.0f, 405.0f));
			multiplyNode.inputPins.push_back({ "A", "A", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input });
			multiplyNode.inputPins.push_back({ "B", "B", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input });
			multiplyNode.outputPins.push_back({ "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output });
			graph.nodes.push_back(std::move(multiplyNode));

			addConnection(emissiveTextureSourceId, "Out", "node_emissive_mul", "A");
			addConnection("node_emissive_tint", "out", "node_emissive_mul", "B");
			emissiveSource = "node_emissive_mul";
		}
		else
		{
			addVectorConstant("node_emissive", "Emissive", math::Vector2(80.0f, 400.0f), math::Vector4(emissive.x, emissive.y, emissive.z, 1.0f));
			emissiveSource = "node_emissive";
		}

		std::string opacitySource = addTextureChain(MaterialTexture::Opacity, "node_opacity_tex", "node_opacity_sample", "Opacity Texture", math::Vector2(40.0f, 470.0f), false);
		if (opacitySource.empty())
		{
			addScalarConstant("node_opacity", "Opacity", math::Vector2(80.0f, 480.0f), 1.0f);
			opacitySource = "node_opacity";
		}

		// No texture-slot equivalent for smoothness on the standard-material side,
		// so always seed from the scalar field. Users can wire up a TextureSample
		// chain manually in the graph editor after conversion.
		addScalarConstant("node_smoothness", "Smoothness", math::Vector2(80.0f, 560.0f), material._properties.smoothness);
		const std::string smoothnessSource = "node_smoothness";

		auto makeOutputNode = [&](const char* id, const char* name, const math::Vector2& position, MaterialGraphValueType valueType)
		{
			auto node = makeNode(id, MaterialGraphNodeType::Output, name, position);
			node.inputPins.push_back({ "In", "In", valueType, MaterialGraphPinDirection::Input });
			graph.nodes.push_back(std::move(node));
		};

		makeOutputNode("output_basecolor", "BaseColor", math::Vector2(620.0f, 80.0f), MaterialGraphValueType::Vector4);
		makeOutputNode("output_normal", "Normal", math::Vector2(620.0f, 160.0f), MaterialGraphValueType::Vector4);
		makeOutputNode("output_roughness", "Roughness", math::Vector2(620.0f, 240.0f), MaterialGraphValueType::Scalar);
		makeOutputNode("output_metallic", "Metallic", math::Vector2(620.0f, 320.0f), MaterialGraphValueType::Scalar);
		makeOutputNode("output_emissive", "Emissive", math::Vector2(620.0f, 400.0f), MaterialGraphValueType::Vector4);
		makeOutputNode("output_opacity", "Opacity", math::Vector2(620.0f, 480.0f), MaterialGraphValueType::Scalar);
		makeOutputNode("output_smoothness", "Smoothness", math::Vector2(620.0f, 560.0f), MaterialGraphValueType::Scalar);

		addConnection(baseColorSource, "Out", "output_basecolor", "In");
		addConnection(normalSource, "Out", "output_normal", "In");
		addConnection(roughnessSource, "Out", "output_roughness", "In");
		addConnection(metallicSource, "Out", "output_metallic", "In");
		addConnection(emissiveSource, "Out", "output_emissive", "In");
		addConnection(opacitySource, "Out", "output_opacity", "In");
		addConnection(smoothnessSource, "out", "output_smoothness", "In");

		graph.outputs =
		{
			{ MaterialGraphOutputSemantic::BaseColor, baseColorSource, "Out" },
			{ MaterialGraphOutputSemantic::Normal, normalSource, "Out" },
			{ MaterialGraphOutputSemantic::Roughness, roughnessSource, "Out" },
			{ MaterialGraphOutputSemantic::Metallic, metallicSource, "Out" },
			{ MaterialGraphOutputSemantic::Emissive, emissiveSource, "Out" },
			{ MaterialGraphOutputSemantic::Opacity, opacitySource, "Out" },
			{ MaterialGraphOutputSemantic::Smoothness, smoothnessSource, "out" }
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
		if (text == "WeatherScalar") { outType = MaterialGraphNodeType::WeatherScalar; return true; }
		if (text == "WeatherVector") { outType = MaterialGraphNodeType::WeatherVector; return true; }
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
		case MaterialGraphNodeType::WeatherScalar: return "WeatherScalar";
		case MaterialGraphNodeType::WeatherVector: return "WeatherVector";
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
		if (text == "Smoothness") { outSemantic = MaterialGraphOutputSemantic::Smoothness; return true; }
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
		case MaterialGraphOutputSemantic::Smoothness: return "Smoothness";
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
