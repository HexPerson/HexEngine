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
		WeatherVector,
		// Unified "single output node" replacing the seven separate output_*
		// nodes. Carries all 7 shading channels as input pins AND a bundle of
		// per-material constants (depth/blend state, cullDistance, flags,
		// model params, etc.) so a graph only needs one terminal node instead
		// of the old "spread your wires across 7 separate stubs" pattern.
		// Both layouts coexist - old graphs with seven Output nodes still
		// load and compile identically.
		PbrOutput
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
		Opacity,
		// Smoothness drives gbuffer.mat.b - read by SSR as both the reflection-
		// enable gate AND the reflection-sharpness control (1-smoothness becomes
		// the SSR ray roughness). Defaults to the standard material's smoothness
		// scalar when the graph doesn't bind it, preserving back-compat with
		// pre-graph materials.
		Smoothness
	};

	struct MaterialGraphPin
	{
		std::string id;
		std::string name;
		MaterialGraphValueType valueType = MaterialGraphValueType::Scalar;
		MaterialGraphPinDirection direction = MaterialGraphPinDirection::Input;
	};

	// Bundle of per-material constants exposed by the PbrOutput node. These are
	// values that don't make sense to drive per-pixel - render state, material
	// model selection, etc. - and that are read by the graph compiler at compile
	// time to populate the resulting Material's properties / render state.
	// Defaults match what a freshly-created Material would have.
	struct MaterialGraphPbrOutputProperties
	{
		// Material property flags (also exposed via the simple MaterialDialog).
		int hasTransparency = 0;
		int materialModel = 0;
		math::Vector4 modelParams = math::Vector4::Zero;
		float rainDripIntensity = 0.0f;
		int affectsGI = 1;
		int emissiveAffectsGI = 0;

		// Render state - read once by the graph compiler and written into the
		// Material before its standard shader is compiled, so artists can drive
		// blend mode / culling / depth from the graph editor without separately
		// editing the simple material dialog.
		DepthBufferState depthState = DepthBufferState::DepthDefault;
		BlendState blendState = BlendState::Opaque;
		CullingMode cullMode = CullingMode::BackFace;
		MaterialFormat materialFormat = MaterialFormat::None;
		float cullDistance = 0.0f;
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

		// Only populated when nodeType == PbrOutput. Carried on every node so
		// there's no separate side-table to keep in sync with the nodes vector.
		MaterialGraphPbrOutputProperties pbrOutputProperties;
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

		// Build a PbrOutput node with the seven shading-input pins + default
		// PbrOutputProperties. Used by CreateDefaultPbrGraph and the graph
		// dialog's "Add PBR Output" path. Position is the canvas coord the
		// caller wants the node to land at.
		static MaterialGraphNode CreatePbrOutputNode(const std::string& id, const math::Vector2& position);

		// Walks the graph and returns a pointer to the first PbrOutput node (or
		// nullptr if none exists - old graphs that still use the seven separate
		// Output nodes). The compiler / dialog use this to decide which output
		// layout to honour.
		MaterialGraphNode* FindPbrOutputNode();
		const MaterialGraphNode* FindPbrOutputNode() const;

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
