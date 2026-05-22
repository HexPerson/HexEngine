#pragma once

#include "../Required.hpp"
#include "RenderStructs.hpp"

namespace HexEngine
{
	class Material;

	enum class MaterialGraphValueType : uint8_t
	{
		Scalar = 0,
		Vector2,
		Vector3,
		Vector4,
		Texture2D,
		UV
	};

	enum class MaterialGraphNodeType : uint8_t
	{
		Output = 0,
		ScalarConstant,
		VectorConstant,
		TextureSample,
		TexCoord,
		Add,
		Multiply,
		Lerp,
		OneMinus,
		NormalMap,
		ScalarParameter,
		VectorParameter,
		TextureParameter,
		WeatherScalar,
		WeatherVector
	};

	enum class MaterialGraphPinDirection : uint8_t
	{
		Input = 0,
		Output
	};

	enum class MaterialGraphOutputSemantic : uint8_t
	{
		BaseColor = 0,
		Normal,
		Roughness,
		Metallic,
		Emissive,
		Opacity
	};

	struct MaterialGraphPin
	{
		std::string id;
		std::string name;
		MaterialGraphValueType valueType = MaterialGraphValueType::Scalar;
		MaterialGraphPinDirection direction = MaterialGraphPinDirection::Input;
	};

	struct MaterialGraphNode
	{
		std::string id;
		MaterialGraphNodeType nodeType = MaterialGraphNodeType::ScalarConstant;
		std::string displayName;
		math::Vector2 position = math::Vector2::Zero;

		float scalarValue = 0.0f;
		math::Vector4 vectorValue = math::Vector4(1.0f);
		fs::path texturePath;
		std::string parameterName;
		bool isExposedParameter = true;

		std::vector<MaterialGraphPin> inputPins;
		std::vector<MaterialGraphPin> outputPins;
	};

	struct MaterialGraphConnection
	{
		std::string fromNodeId;
		std::string fromPinId;
		std::string toNodeId;
		std::string toPinId;
	};

	struct MaterialGraphOutputBinding
	{
		MaterialGraphOutputSemantic semantic = MaterialGraphOutputSemantic::BaseColor;
		std::string nodeId;
		std::string pinId;
	};

	struct MaterialGraphParameter
	{
		std::string name;
		MaterialGraphValueType valueType = MaterialGraphValueType::Scalar;
		float scalarValue = 0.0f;
		math::Vector4 vectorValue = math::Vector4::One;
		fs::path texturePath;
		bool isExposed = true;
	};

	struct MaterialGraphParameterOverride
	{
		std::string name;
		MaterialGraphValueType valueType = MaterialGraphValueType::Scalar;
		float scalarValue = 0.0f;
		math::Vector4 vectorValue = math::Vector4::One;
		fs::path texturePath;
	};

	struct MaterialGraphInstanceData
	{
		fs::path parentMaterialPath;
		std::vector<MaterialGraphParameterOverride> overrides;
	};

	class HEX_API MaterialGraph
	{
	public:
		static constexpr int32_t kVersion = 1;

		int32_t version = kVersion;
		std::vector<MaterialGraphNode> nodes;
		std::vector<MaterialGraphConnection> connections;
		std::vector<MaterialGraphOutputBinding> outputs;
		std::vector<MaterialGraphParameter> parameters;

		MaterialGraphNode* FindNode(const std::string& id);
		const MaterialGraphNode* FindNode(const std::string& id) const;
		const MaterialGraphPin* FindPin(const std::string& nodeId, const std::string& pinId, MaterialGraphPinDirection direction) const;

		void EnsureDefaultOutputBindings();
		static MaterialGraph CreateDefaultPbrGraph();

		/**
		 * @brief Build a graph that mirrors a standard-material's PBR inputs.
		 *
		 * For each PBR channel (BaseColor, Normal, Roughness, Metallic, Emissive,
		 * Opacity) the resulting graph either:
		 *   - emits a TextureParameter + TextureSample chain (TextureParameter +
		 *     NormalMap for the Normal channel) if the standard material has a
		 *     texture bound for that slot, or
		 *   - emits a Vector/Scalar Constant seeded with the material's scalar /
		 *     colour property (diffuseColour, roughnessFactor, etc.)
		 *
		 * Each source feeds the matching `output_*` node so the new graph renders
		 * identically to the standard material it came from. Used by the graph
		 * dialog when "promoting" a non-graph material on-open, and by the
		 * AssetExplorer "Convert to material graph" context-menu action.
		 */
		static MaterialGraph CreateFromStandardMaterial(const Material& material);

		static bool ParseValueType(const std::string& text, MaterialGraphValueType& outType);
		static const char* ValueTypeToString(MaterialGraphValueType type);
		static bool ParseNodeType(const std::string& text, MaterialGraphNodeType& outType);
		static const char* NodeTypeToString(MaterialGraphNodeType type);
		static bool ParseOutputSemantic(const std::string& text, MaterialGraphOutputSemantic& outSemantic);
		static const char* OutputSemanticToString(MaterialGraphOutputSemantic semantic);

		static void Serialize(json& graphJson, const MaterialGraph& graph);
		static bool Deserialize(const json& graphJson, MaterialGraph& outGraph, std::vector<std::string>* errors = nullptr);

		static void SerializeInstance(json& instanceJson, const MaterialGraphInstanceData& instance);
		static bool DeserializeInstance(const json& instanceJson, MaterialGraphInstanceData& outInstance, std::vector<std::string>* errors = nullptr);
	};
}
