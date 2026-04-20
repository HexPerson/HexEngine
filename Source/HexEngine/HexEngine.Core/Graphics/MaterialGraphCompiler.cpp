#include "MaterialGraphCompiler.hpp"
#include "Material.hpp"
#include "../HexEngine.hpp"
#include <algorithm>
#include <format>
#include <unordered_map>

namespace HexEngine
{
	namespace
	{
		struct GraphValue
		{
			MaterialGraphValueType type = MaterialGraphValueType::Scalar;
			float scalar = 0.0f;
			math::Vector4 vector = math::Vector4::Zero;
			fs::path texturePath;
		};

		const MaterialGraphParameterOverride* FindOverride(
			const std::vector<MaterialGraphParameterOverride>* overrides,
			const std::string& parameterName)
		{
			if (overrides == nullptr)
				return nullptr;

			for (const auto& overrideValue : *overrides)
			{
				if (overrideValue.name == parameterName)
					return &overrideValue;
			}

			return nullptr;
		}

		const MaterialGraphParameter* FindParameter(const MaterialGraph& graph, const std::string& parameterName)
		{
			for (const auto& parameter : graph.parameters)
			{
				if (parameter.name == parameterName)
					return &parameter;
			}

			return nullptr;
		}

		bool ResolveInputSource(
			const MaterialGraph& graph,
			const std::string& nodeId,
			const std::string& pinId,
			std::string& sourceNodeId,
			std::string& sourcePinId)
		{
			for (const auto& connection : graph.connections)
			{
				if (connection.toNodeId == nodeId && connection.toPinId == pinId)
				{
					sourceNodeId = connection.fromNodeId;
					sourcePinId = connection.fromPinId;
					return true;
				}
			}

			return false;
		}

		bool ScalarFromValue(const GraphValue& value, float& outScalar)
		{
			switch (value.type)
			{
			case MaterialGraphValueType::Scalar:
				outScalar = value.scalar;
				return true;
			case MaterialGraphValueType::Vector2:
			case MaterialGraphValueType::Vector3:
			case MaterialGraphValueType::Vector4:
				outScalar = value.vector.x;
				return true;
			default:
				break;
			}

			return false;
		}

		bool VectorFromValue(const GraphValue& value, math::Vector4& outVector)
		{
			switch (value.type)
			{
			case MaterialGraphValueType::Scalar:
				outVector = math::Vector4(value.scalar, value.scalar, value.scalar, value.scalar);
				return true;
			case MaterialGraphValueType::Vector2:
			case MaterialGraphValueType::Vector3:
			case MaterialGraphValueType::Vector4:
				outVector = value.vector;
				return true;
			default:
				break;
			}

			return false;
		}

		bool EvaluateNodeOutput(
			const MaterialGraph& graph,
			const std::string& nodeId,
			const std::string& outputPinId,
			const std::vector<MaterialGraphParameterOverride>* overrides,
			std::unordered_map<std::string, GraphValue>& cache,
			std::vector<std::string>& errors,
			GraphValue& outValue)
		{
			const std::string cacheKey = nodeId + ":" + outputPinId;
			if (const auto it = cache.find(cacheKey); it != cache.end())
			{
				outValue = it->second;
				return true;
			}

			const auto* node = graph.FindNode(nodeId);
			if (node == nullptr)
			{
				errors.push_back(std::format("Missing node '{}'.", nodeId));
				return false;
			}

			auto evalInput = [&](const std::string& inputPinId, GraphValue& valueOut) -> bool
			{
				std::string srcNodeId;
				std::string srcPinId;
				if (!ResolveInputSource(graph, node->id, inputPinId, srcNodeId, srcPinId))
					return false;

				return EvaluateNodeOutput(graph, srcNodeId, srcPinId, overrides, cache, errors, valueOut);
			};

			GraphValue value;
			switch (node->nodeType)
			{
			case MaterialGraphNodeType::ScalarConstant:
				value.type = MaterialGraphValueType::Scalar;
				value.scalar = node->scalarValue;
				break;
			case MaterialGraphNodeType::VectorConstant:
				value.type = MaterialGraphValueType::Vector4;
				value.vector = node->vectorValue;
				break;
			case MaterialGraphNodeType::TexCoord:
				value.type = MaterialGraphValueType::UV;
				value.vector = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
				break;
			case MaterialGraphNodeType::TextureParameter:
			case MaterialGraphNodeType::ScalarParameter:
			case MaterialGraphNodeType::VectorParameter:
			{
				const auto* graphParameter = FindParameter(graph, node->parameterName);
				if (graphParameter == nullptr)
				{
					errors.push_back(std::format("Parameter '{}' is not defined.", node->parameterName));
					return false;
				}

				value.type = graphParameter->valueType;
				value.scalar = graphParameter->scalarValue;
				value.vector = graphParameter->vectorValue;
				value.texturePath = graphParameter->texturePath;

				if (const auto* overrideValue = FindOverride(overrides, node->parameterName); overrideValue != nullptr)
				{
					value.type = overrideValue->valueType;
					value.scalar = overrideValue->scalarValue;
					value.vector = overrideValue->vectorValue;
					value.texturePath = overrideValue->texturePath;
				}
				break;
			}
			case MaterialGraphNodeType::TextureSample:
			{
				GraphValue textureInput;
				if (!evalInput("Tex", textureInput))
				{
					if (!node->texturePath.empty())
					{
						value.type = MaterialGraphValueType::Texture2D;
						value.texturePath = node->texturePath;
						break;
					}

					errors.push_back(std::format("TextureSample '{}' is missing a texture input.", node->id));
					return false;
				}

				if (textureInput.type != MaterialGraphValueType::Texture2D)
				{
					errors.push_back(std::format("TextureSample '{}' expected a Texture2D input.", node->id));
					return false;
				}

				value.type = MaterialGraphValueType::Texture2D;
				value.texturePath = textureInput.texturePath;
				break;
			}
			case MaterialGraphNodeType::Add:
			case MaterialGraphNodeType::Multiply:
			{
				GraphValue a;
				GraphValue b;
				if (!evalInput("A", a) || !evalInput("B", b))
				{
					errors.push_back(std::format("Node '{}' is missing one or more inputs.", node->id));
					return false;
				}

				if (a.type == MaterialGraphValueType::Texture2D || b.type == MaterialGraphValueType::Texture2D)
				{
					errors.push_back(std::format("Node '{}' uses texture math which is unsupported in v1.", node->id));
					return false;
				}

				float as = 0.0f;
				float bs = 0.0f;
				if (ScalarFromValue(a, as) && ScalarFromValue(b, bs))
				{
					value.type = MaterialGraphValueType::Scalar;
					value.scalar = node->nodeType == MaterialGraphNodeType::Add ? (as + bs) : (as * bs);
				}
				else
				{
					math::Vector4 av;
					math::Vector4 bv;
					if (!VectorFromValue(a, av) || !VectorFromValue(b, bv))
					{
						errors.push_back(std::format("Node '{}' could not coerce inputs for math.", node->id));
						return false;
					}

					value.type = MaterialGraphValueType::Vector4;
					value.vector = node->nodeType == MaterialGraphNodeType::Add ? (av + bv) : (av * bv);
				}
				break;
			}
			case MaterialGraphNodeType::Lerp:
			{
				GraphValue a;
				GraphValue b;
				GraphValue alpha;
				if (!evalInput("A", a) || !evalInput("B", b) || !evalInput("Alpha", alpha))
				{
					errors.push_back(std::format("Lerp node '{}' is missing one or more inputs.", node->id));
					return false;
				}

				if (a.type == MaterialGraphValueType::Texture2D || b.type == MaterialGraphValueType::Texture2D)
				{
					errors.push_back(std::format("Lerp node '{}' uses texture math which is unsupported in v1.", node->id));
					return false;
				}

				float t = 0.0f;
				if (!ScalarFromValue(alpha, t))
				{
					errors.push_back(std::format("Lerp node '{}' alpha input must be scalar.", node->id));
					return false;
				}

				float as = 0.0f;
				float bs = 0.0f;
				if (ScalarFromValue(a, as) && ScalarFromValue(b, bs))
				{
					value.type = MaterialGraphValueType::Scalar;
					value.scalar = std::lerp(as, bs, t);
				}
				else
				{
					math::Vector4 av;
					math::Vector4 bv;
					if (!VectorFromValue(a, av) || !VectorFromValue(b, bv))
					{
						errors.push_back(std::format("Lerp node '{}' input types are unsupported.", node->id));
						return false;
					}

					value.type = MaterialGraphValueType::Vector4;
					value.vector = av + (bv - av) * t;
				}
				break;
			}
			case MaterialGraphNodeType::OneMinus:
			{
				GraphValue in;
				if (!evalInput("In", in))
				{
					errors.push_back(std::format("OneMinus node '{}' is missing input.", node->id));
					return false;
				}

				float s = 0.0f;
				if (ScalarFromValue(in, s))
				{
					value.type = MaterialGraphValueType::Scalar;
					value.scalar = 1.0f - s;
				}
				else
				{
					math::Vector4 v;
					if (!VectorFromValue(in, v))
					{
						errors.push_back(std::format("OneMinus node '{}' input type is unsupported.", node->id));
						return false;
					}
					value.type = MaterialGraphValueType::Vector4;
					value.vector = math::Vector4(1.0f) - v;
				}
				break;
			}
			case MaterialGraphNodeType::NormalMap:
			{
				GraphValue normalInput;
				if (!evalInput("Normal", normalInput))
				{
					errors.push_back(std::format("NormalMap node '{}' is missing normal input.", node->id));
					return false;
				}

				if (normalInput.type == MaterialGraphValueType::Texture2D)
				{
					value.type = MaterialGraphValueType::Texture2D;
					value.texturePath = normalInput.texturePath;
				}
				else
				{
					math::Vector4 normal;
					if (!VectorFromValue(normalInput, normal))
					{
						errors.push_back(std::format("NormalMap node '{}' input type is unsupported.", node->id));
						return false;
					}
					value.type = MaterialGraphValueType::Vector4;
					value.vector = normal;
				}
				break;
			}
			default:
				errors.push_back(std::format("Unsupported node '{}' ({})", node->id, MaterialGraph::NodeTypeToString(node->nodeType)));
				return false;
			}

			cache[cacheKey] = value;
			outValue = value;
			return true;
		}

		bool EvaluateOutput(
			const MaterialGraph& graph,
			MaterialGraphOutputSemantic semantic,
			const std::vector<MaterialGraphParameterOverride>* overrides,
			std::unordered_map<std::string, GraphValue>& cache,
			std::vector<std::string>& errors,
			GraphValue& outValue)
		{
			for (const auto& output : graph.outputs)
			{
				if (output.semantic != semantic)
					continue;

				if (output.nodeId.empty() || output.pinId.empty())
					return false;

				return EvaluateNodeOutput(graph, output.nodeId, output.pinId, overrides, cache, errors, outValue);
			}

			return false;
		}
	}

	MaterialGraphCompileResult MaterialGraphCompiler::CompileToMaterial(
		const MaterialGraph& graph,
		Material& material,
		const std::vector<MaterialGraphParameterOverride>* overrides)
	{
		MaterialGraphCompileResult result;

		const auto validation = MaterialGraphValidator::Validate(graph);
		for (const auto& message : validation.messages)
		{
			if (message.severity == MaterialGraphValidationMessage::Severity::Error)
				result.errors.push_back(message.message);
			else
				result.warnings.push_back(message.message);
		}

		if (validation.HasErrors())
			return result;

		std::unordered_map<std::string, GraphValue> cache;
		std::vector<std::string> evalErrors;

		material.SetStandardShader(IShader::Create("EngineData.Shaders/Default.hcs"));
		material.SetShadowMapShader(IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"));
		material._properties.hasTransparency = 0;
		material.SetTexture(MaterialTexture::Albedo, nullptr);
		material.SetTexture(MaterialTexture::Normal, nullptr);
		material.SetTexture(MaterialTexture::Roughness, nullptr);
		material.SetTexture(MaterialTexture::Metallic, nullptr);
		material.SetTexture(MaterialTexture::Emission, nullptr);
		material.SetTexture(MaterialTexture::Opacity, nullptr);

		GraphValue baseColor;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::BaseColor, overrides, cache, evalErrors, baseColor))
		{
			if (baseColor.type == MaterialGraphValueType::Texture2D && !baseColor.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Albedo, ITexture2D::Create(baseColor.texturePath));
			}
			else
			{
				math::Vector4 color = math::Vector4::One;
				VectorFromValue(baseColor, color);
				material._properties.diffuseColour = color;
			}
		}

		GraphValue normal;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::Normal, overrides, cache, evalErrors, normal))
		{
			if (normal.type == MaterialGraphValueType::Texture2D && !normal.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Normal, ITexture2D::Create(normal.texturePath));
			}
		}

		GraphValue roughness;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::Roughness, overrides, cache, evalErrors, roughness))
		{
			if (roughness.type == MaterialGraphValueType::Texture2D && !roughness.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Roughness, ITexture2D::Create(roughness.texturePath));
			}
			else
			{
				float s = 0.5f;
				ScalarFromValue(roughness, s);
				material._properties.roughnessFactor = std::clamp(s, 0.0f, 1.0f);
			}
		}

		GraphValue metallic;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::Metallic, overrides, cache, evalErrors, metallic))
		{
			if (metallic.type == MaterialGraphValueType::Texture2D && !metallic.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Metallic, ITexture2D::Create(metallic.texturePath));
			}
			else
			{
				float s = 0.0f;
				ScalarFromValue(metallic, s);
				material._properties.metallicFactor = std::clamp(s, 0.0f, 1.0f);
			}
		}

		GraphValue emissive;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::Emissive, overrides, cache, evalErrors, emissive))
		{
			if (emissive.type == MaterialGraphValueType::Texture2D && !emissive.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Emission, ITexture2D::Create(emissive.texturePath));
			}
			else
			{
				math::Vector4 emissiveColor = math::Vector4::Zero;
				VectorFromValue(emissive, emissiveColor);
				material._properties.emissiveColour = emissiveColor;
			}
		}

		GraphValue opacity;
		if (EvaluateOutput(graph, MaterialGraphOutputSemantic::Opacity, overrides, cache, evalErrors, opacity))
		{
			if (opacity.type == MaterialGraphValueType::Texture2D && !opacity.texturePath.empty())
			{
				material.SetTexture(MaterialTexture::Opacity, ITexture2D::Create(opacity.texturePath));
				material._properties.hasTransparency = 1;
			}
			else
			{
				float s = 1.0f;
				ScalarFromValue(opacity, s);
				if (s < 0.999f)
					material._properties.hasTransparency = 1;
			}
		}

		result.errors.insert(result.errors.end(), evalErrors.begin(), evalErrors.end());
		result.success = result.errors.empty();
		return result;
	}

	MaterialGraphCompileResult MaterialGraphCompiler::ApplyInstanceToMaterial(
		const MaterialGraph& graph,
		const MaterialGraphInstanceData& instanceData,
		Material& material)
	{
		return CompileToMaterial(graph, material, &instanceData.overrides);
	}
}
