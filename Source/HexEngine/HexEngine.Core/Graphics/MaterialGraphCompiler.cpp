#include "MaterialGraphCompiler.hpp"
#include "Material.hpp"
#include "IShader.hpp"
#include "ITexture2D.hpp"
#include "RenderStructs.hpp"
#include "../HexEngine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace HexEngine
{
	namespace
	{
		constexpr int32_t kMaxGraphTextureSlots = 8;

		struct GraphExpression
		{
			MaterialGraphValueType type = MaterialGraphValueType::Scalar;
			std::string code;
		};

		struct CompilerContext
		{
			const MaterialGraph& graph;
			const std::vector<MaterialGraphParameterOverride>* overrides = nullptr;
			std::unordered_map<std::string, GraphExpression> cache;
			std::unordered_map<std::string, int32_t> textureSlots;
			std::array<fs::path, kMaxGraphTextureSlots> slotPaths{};
			std::unordered_map<std::string, int32_t> textureParameterSlots;
			int32_t nextTextureSlot = 0;
			std::vector<std::string> errors;
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

		MaterialTexture SlotToMaterialTexture(int32_t slot)
		{
			switch (slot)
			{
			case 0: return MaterialTexture::Albedo;
			case 1: return MaterialTexture::Normal;
			case 2: return MaterialTexture::Roughness;
			case 3: return MaterialTexture::Metallic;
			case 4: return MaterialTexture::Height;
			case 5: return MaterialTexture::Emission;
			case 6: return MaterialTexture::Opacity;
			case 7: return MaterialTexture::AmbientOcclusion;
			default: return MaterialTexture::Albedo;
			}
		}

		std::string Wrap(const std::string& code)
		{
			return std::format("({})", code);
		}

		std::string ToFloat2(CompilerContext& ctx, const GraphExpression& expr);
		std::string ToFloat3(CompilerContext& ctx, const GraphExpression& expr);
		std::string ToFloat4(CompilerContext& ctx, const GraphExpression& expr);
		std::string ToScalar(CompilerContext& ctx, const GraphExpression& expr);

		int32_t AcquireTextureSlot(CompilerContext& ctx, const fs::path& texturePath, const std::string& stableKey = {})
		{
			if (texturePath.empty())
				return -1;

			const std::string key = !stableKey.empty() ? stableKey : texturePath.lexically_normal().generic_string();
			if (const auto it = ctx.textureSlots.find(key); it != ctx.textureSlots.end())
				return it->second;

			if (ctx.nextTextureSlot >= kMaxGraphTextureSlots)
			{
				ctx.errors.push_back(std::format(
					"Graph references more than {} unique textures; this exceeds runtime texture slots.",
					kMaxGraphTextureSlots));
				return -1;
			}

			const int32_t slot = ctx.nextTextureSlot++;
			ctx.textureSlots[key] = slot;
			ctx.slotPaths[(size_t)slot] = texturePath;
			return slot;
		}

		GraphExpression MakeTextureObjectExpression(
			CompilerContext& ctx,
			const fs::path& texturePath,
			const std::string& stableKey = {})
		{
			GraphExpression expr;
			expr.type = MaterialGraphValueType::Texture2D;

			if (texturePath.empty())
			{
				ctx.errors.push_back("Texture input is missing a texture path.");
				expr.code = "g_graphTex0";
				return expr;
			}

			const int32_t slot = AcquireTextureSlot(ctx, texturePath, stableKey);
			if (slot < 0)
			{
				expr.code = "g_graphTex0";
				return expr;
			}

			expr.code = std::format("g_graphTex{}", slot);
			return expr;
		}

		std::string ToFloat2(CompilerContext& ctx, const GraphExpression& expr)
		{
			switch (expr.type)
			{
			case MaterialGraphValueType::UV:
			case MaterialGraphValueType::Vector2:
				return Wrap(expr.code);
			case MaterialGraphValueType::Scalar:
				return std::format("float2({0}, {0})", expr.code);
			case MaterialGraphValueType::Vector3:
				return std::format("({}).xy", Wrap(expr.code));
			case MaterialGraphValueType::Vector4:
				return std::format("({}).xy", Wrap(expr.code));
			case MaterialGraphValueType::Texture2D:
				return std::format("({}).xy", ToFloat4(ctx, expr));
			default:
				break;
			}

			return "float2(0.0f, 0.0f)";
		}

		std::string ToFloat3(CompilerContext& ctx, const GraphExpression& expr)
		{
			switch (expr.type)
			{
			case MaterialGraphValueType::Vector3:
				return Wrap(expr.code);
			case MaterialGraphValueType::Scalar:
				return std::format("float3({0}, {0}, {0})", expr.code);
			case MaterialGraphValueType::UV:
			case MaterialGraphValueType::Vector2:
				return std::format("float3({}, 0.0f)", ToFloat2(ctx, expr));
			case MaterialGraphValueType::Vector4:
				return std::format("({}).xyz", Wrap(expr.code));
			case MaterialGraphValueType::Texture2D:
				return std::format("({}).xyz", ToFloat4(ctx, expr));
			default:
				break;
			}

			return "float3(0.0f, 0.0f, 0.0f)";
		}

		std::string ToFloat4(CompilerContext& ctx, const GraphExpression& expr)
		{
			switch (expr.type)
			{
			case MaterialGraphValueType::Vector4:
				return Wrap(expr.code);
			case MaterialGraphValueType::Scalar:
				return std::format("float4({0}, {0}, {0}, {0})", expr.code);
			case MaterialGraphValueType::UV:
			case MaterialGraphValueType::Vector2:
				return std::format("float4({}, 0.0f, 1.0f)", ToFloat2(ctx, expr));
			case MaterialGraphValueType::Vector3:
				return std::format("float4({}, 1.0f)", ToFloat3(ctx, expr));
			case MaterialGraphValueType::Texture2D:
				return std::format("{}.Sample(g_textureSampler, input.texcoord)", expr.code);
			default:
				break;
			}

			return "float4(0.0f, 0.0f, 0.0f, 1.0f)";
		}

		std::string ToScalar(CompilerContext& ctx, const GraphExpression& expr)
		{
			switch (expr.type)
			{
			case MaterialGraphValueType::Scalar:
				return Wrap(expr.code);
			case MaterialGraphValueType::UV:
			case MaterialGraphValueType::Vector2:
				return std::format("({}).x", Wrap(ToFloat2(ctx, expr)));
			case MaterialGraphValueType::Vector3:
				return std::format("({}).x", Wrap(ToFloat3(ctx, expr)));
			case MaterialGraphValueType::Vector4:
				return std::format("({}).x", Wrap(expr.code));
			case MaterialGraphValueType::Texture2D:
				return std::format("({}).x", Wrap(ToFloat4(ctx, expr)));
			default:
				break;
			}

			return "0.0f";
		}

		bool EvaluateNodeOutput(
			CompilerContext& ctx,
			const std::string& nodeId,
			const std::string& outputPinId,
			GraphExpression& outValue)
		{
			const std::string cacheKey = nodeId + ":" + outputPinId;
			if (const auto it = ctx.cache.find(cacheKey); it != ctx.cache.end())
			{
				outValue = it->second;
				return true;
			}

			const auto* node = ctx.graph.FindNode(nodeId);
			if (node == nullptr)
			{
				ctx.errors.push_back(std::format("Missing node '{}'.", nodeId));
				return false;
			}

			auto evalInput = [&](const std::string& inputPinId, GraphExpression& valueOut) -> bool
			{
				std::string srcNodeId;
				std::string srcPinId;
				if (!ResolveInputSource(ctx.graph, node->id, inputPinId, srcNodeId, srcPinId))
					return false;

				return EvaluateNodeOutput(ctx, srcNodeId, srcPinId, valueOut);
			};

			GraphExpression value;
			switch (node->nodeType)
			{
			case MaterialGraphNodeType::Output:
			{
				if (!evalInput("In", value))
				{
					ctx.errors.push_back(std::format("Output node '{}' is missing input.", node->displayName.empty() ? node->id : node->displayName));
					return false;
				}
				break;
			}
			case MaterialGraphNodeType::ScalarConstant:
				value.type = MaterialGraphValueType::Scalar;
				value.code = std::format("{:.9f}", node->scalarValue);
				break;
			case MaterialGraphNodeType::VectorConstant:
				value.type = MaterialGraphValueType::Vector4;
				value.code = std::format("float4({:.9f}, {:.9f}, {:.9f}, {:.9f})", node->vectorValue.x, node->vectorValue.y, node->vectorValue.z, node->vectorValue.w);
				break;
			case MaterialGraphNodeType::TexCoord:
				value.type = MaterialGraphValueType::UV;
				value.code = "input.texcoord";
				break;
			case MaterialGraphNodeType::ScalarParameter:
			case MaterialGraphNodeType::VectorParameter:
			case MaterialGraphNodeType::TextureParameter:
			{
				const auto* graphParameter = FindParameter(ctx.graph, node->parameterName);
				MaterialGraphValueType paramType = MaterialGraphValueType::Scalar;
				float scalar = node->scalarValue;
				math::Vector4 vec = node->vectorValue;
				fs::path texturePath = node->texturePath;

				if (graphParameter != nullptr)
				{
					paramType = graphParameter->valueType;
					scalar = graphParameter->scalarValue;
					vec = graphParameter->vectorValue;
					texturePath = graphParameter->texturePath;
				}
				else if (!node->parameterName.empty())
				{
					ctx.errors.push_back(std::format("Parameter '{}' is not defined.", node->parameterName));
				}

				if (const auto* overrideValue = FindOverride(ctx.overrides, node->parameterName); overrideValue != nullptr)
				{
					paramType = overrideValue->valueType;
					scalar = overrideValue->scalarValue;
					vec = overrideValue->vectorValue;
					texturePath = overrideValue->texturePath;
				}

				if (node->nodeType == MaterialGraphNodeType::TextureParameter || paramType == MaterialGraphValueType::Texture2D)
				{
					const std::string paramKey = std::format("param:{}", node->parameterName);
					value = MakeTextureObjectExpression(ctx, texturePath, paramKey);
					if (!node->parameterName.empty())
					{
						const int32_t slot = AcquireTextureSlot(ctx, texturePath, paramKey);
						if (slot >= 0)
							ctx.textureParameterSlots[node->parameterName] = slot;
					}
				}
				else if (node->nodeType == MaterialGraphNodeType::ScalarParameter || paramType == MaterialGraphValueType::Scalar)
				{
					value.type = MaterialGraphValueType::Scalar;
					value.code = std::format("{:.9f}", scalar);
				}
				else
				{
					value.type = MaterialGraphValueType::Vector4;
					value.code = std::format("float4({:.9f}, {:.9f}, {:.9f}, {:.9f})", vec.x, vec.y, vec.z, vec.w);
				}
				break;
			}
			case MaterialGraphNodeType::TextureSample:
			{
				GraphExpression textureInput;
				if (!evalInput("Tex", textureInput))
				{
					if (!node->texturePath.empty())
					{
						textureInput = MakeTextureObjectExpression(ctx, node->texturePath, std::format("node:{}", node->id));
					}
					else
					{
						ctx.errors.push_back(std::format("TextureSample '{}' is missing a texture input.", node->id));
						return false;
					}
				}

				if (textureInput.type != MaterialGraphValueType::Texture2D)
				{
					ctx.errors.push_back(std::format("TextureSample '{}' expected a Texture2D input.", node->id));
					return false;
				}

				std::string uvExpr = "input.texcoord";
				GraphExpression uvInput;
				if (evalInput("UV", uvInput))
					uvExpr = ToFloat2(ctx, uvInput);

				value.type = MaterialGraphValueType::Vector4;
				value.code = std::format("{}.Sample(g_textureSampler, {})", textureInput.code, uvExpr);
				break;
			}
			case MaterialGraphNodeType::Add:
			case MaterialGraphNodeType::Multiply:
			{
				GraphExpression a;
				GraphExpression b;
				if (!evalInput("A", a) || !evalInput("B", b))
				{
					ctx.errors.push_back(std::format("Node '{}' is missing one or more inputs.", node->id));
					return false;
				}

				if (a.type == MaterialGraphValueType::Scalar && b.type == MaterialGraphValueType::Scalar)
				{
					value.type = MaterialGraphValueType::Scalar;
					value.code = std::format("({} {} {})", ToScalar(ctx, a), node->nodeType == MaterialGraphNodeType::Add ? "+" : "*", ToScalar(ctx, b));
				}
				else
				{
					value.type = MaterialGraphValueType::Vector4;
					value.code = std::format("({} {} {})", ToFloat4(ctx, a), node->nodeType == MaterialGraphNodeType::Add ? "+" : "*", ToFloat4(ctx, b));
				}
				break;
			}
			case MaterialGraphNodeType::Lerp:
			{
				GraphExpression a;
				GraphExpression b;
				GraphExpression alpha;
				if (!evalInput("A", a) || !evalInput("B", b) || !evalInput("Alpha", alpha))
				{
					ctx.errors.push_back(std::format("Lerp node '{}' is missing one or more inputs.", node->id));
					return false;
				}

				if (a.type == MaterialGraphValueType::Scalar && b.type == MaterialGraphValueType::Scalar)
				{
					value.type = MaterialGraphValueType::Scalar;
					value.code = std::format("lerp({}, {}, {})", ToScalar(ctx, a), ToScalar(ctx, b), ToScalar(ctx, alpha));
				}
				else
				{
					value.type = MaterialGraphValueType::Vector4;
					value.code = std::format("lerp({}, {}, {})", ToFloat4(ctx, a), ToFloat4(ctx, b), ToScalar(ctx, alpha));
				}
				break;
			}
			case MaterialGraphNodeType::OneMinus:
			{
				GraphExpression input;
				if (!evalInput("In", input))
				{
					ctx.errors.push_back(std::format("OneMinus node '{}' is missing input.", node->id));
					return false;
				}

				if (input.type == MaterialGraphValueType::Scalar)
				{
					value.type = MaterialGraphValueType::Scalar;
					value.code = std::format("(1.0f - {})", ToScalar(ctx, input));
				}
				else
				{
					value.type = MaterialGraphValueType::Vector4;
					value.code = std::format("(float4(1.0f, 1.0f, 1.0f, 1.0f) - {})", ToFloat4(ctx, input));
				}
				break;
			}
			case MaterialGraphNodeType::NormalMap:
			{
				GraphExpression normalInput;
				if (!evalInput("Normal", normalInput))
				{
					ctx.errors.push_back(std::format("NormalMap node '{}' is missing normal input.", node->id));
					return false;
				}

				value.type = MaterialGraphValueType::Vector3;
				value.code = std::format(
					"normalize(mul(normalize({} * 2.0f - 1.0f), float3x3(input.tangent, input.binormal, normalize(input.normal))))",
					ToFloat3(ctx, normalInput));
				break;
			}
			default:
				ctx.errors.push_back(std::format("Unsupported node '{}' ({})", node->id, MaterialGraph::NodeTypeToString(node->nodeType)));
				return false;
			}

			ctx.cache[cacheKey] = value;
			outValue = value;
			return true;
		}

		bool EvaluateOutputExpression(
			CompilerContext& ctx,
			MaterialGraphOutputSemantic semantic,
			GraphExpression& outExpr)
		{
			for (const auto& output : ctx.graph.outputs)
			{
				if (output.semantic != semantic)
					continue;

				if (output.nodeId.empty() || output.pinId.empty())
					return false;

				return EvaluateNodeOutput(ctx, output.nodeId, output.pinId, outExpr);
			}

			return false;
		}

		std::string BuildGraphShaderSource(
			CompilerContext& ctx,
			const GraphExpression* baseColorExpr,
			const GraphExpression* normalExpr,
			const GraphExpression* roughnessExpr,
			const GraphExpression* metallicExpr,
			const GraphExpression* emissiveExpr,
			const GraphExpression* opacityExpr)
		{
			std::stringstream ss;

			ss << "\"Requirements\"\n{\n}\n";
			ss << "\"InputLayout\"\n{\n\tPosNormTanBinTex_INSTANCED\n}\n";
			ss << "\"VertexShaderIncludes\"\n{\n\tMeshCommon\n\tUtils\n}\n";
			ss << "\"PixelShaderIncludes\"\n{\n\tMeshCommon\n\tUtils\n}\n";
			ss << "\"VertexShader\"\n{\n";
			ss << "\tMeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)\n\t{\n";
			ss << "\t\tMeshPixelInput output;\n";
			ss << "\t\tinput.position.w = 1.0f;\n";
			ss << "\t\tmatrix worldMatrix = mul(instance.world, g_worldMatrix);\n";
			ss << "\t\tmatrix normalMatrix = mul(instance.worldInverseTranspose, g_worldMatrix);\n";
			ss << "\t\tmatrix worldPrev = mul(instance.worldPrev, g_worldMatrix);\n";
			ss << "\t\toutput.position = mul(input.position, worldMatrix);\n";
			ss << "\t\toutput.positionWS = output.position;\n";
			ss << "\t\tif(g_cullDistance > 0.0f)\n\t\t{\n";
			ss << "\t\t\toutput.cullDistance = length(output.positionWS.xyz - g_eyePos.xyz) >= g_cullDistance ? -1.0f : 1.0f;\n";
			ss << "\t\t}\n";
			ss << "\t\toutput.position = mul(output.position, g_viewProjectionMatrix);\n";
			ss << "\t\tfloat4 prevFrame_worldPos = mul(input.position, worldPrev);\n";
			ss << "\t\tfloat4 prevFrame_clipPos = mul(prevFrame_worldPos, g_viewProjectionMatrixPrev);\n";
			ss << "\t\toutput.previousPositionUnjittered = prevFrame_clipPos;\n";
			ss << "\t\toutput.currentPositionUnjittered = output.position;\n";
			ss << "\t\toutput.position.xy += g_jitterOffsets * output.position.w;\n";
			ss << "\t\toutput.texcoord = input.texcoord * instance.uvScale;\n";
			ss << "\t\toutput.normal = normalize(mul(input.normal, (float3x3)normalMatrix));\n";
			ss << "\t\toutput.tangent = normalize(mul(input.tangent, (float3x3)normalMatrix));\n";
			ss << "\t\toutput.binormal = normalize(mul(input.binormal, (float3x3)normalMatrix));\n";
			ss << "\t\toutput.viewDirection.xyz = normalize(g_eyePos.xyz - output.positionWS.xyz);\n";
			ss << "\t\toutput.colour = instance.colour;\n";
			ss << "\t\toutput.instanceID = instanceID + entityId;\n";
			ss << "\t\treturn output;\n\t}\n}\n";

			ss << "\"PixelShader\"\n{\n";
			for (int32_t slot = 0; slot < ctx.nextTextureSlot; ++slot)
			{
				ss << std::format("\tTexture2D g_graphTex{} : register(t{});\n", slot, slot);
			}
			ss << "\tSamplerState g_textureSampler : register(s0);\n\n";
			ss << "\tGBufferOut ShaderMain(MeshPixelInput input)\n\t{\n";
			ss << "\t\tGBufferOut output;\n";
			ss << "\t\tfloat3 worldNormal = normalize(input.normal);\n";
			ss << "\t\tfloat4 worldViewPosition = mul(input.positionWS, g_viewMatrix);\n";
			ss << "\t\tfloat pixelDepth = -worldViewPosition.z;\n";

			if (baseColorExpr != nullptr)
				ss << std::format("\t\tfloat4 baseColor = {};\n", ToFloat4(ctx, *baseColorExpr));
			else
				ss << "\t\tfloat4 baseColor = g_material.diffuseColour;\n";

			if (normalExpr != nullptr)
				ss << std::format("\t\tworldNormal = normalize({});\n", ToFloat3(ctx, *normalExpr));

			if (roughnessExpr != nullptr)
				ss << std::format("\t\tfloat roughness = saturate({});\n", ToScalar(ctx, *roughnessExpr));
			else
				ss << "\t\tfloat roughness = saturate(g_material.roughnessFactor);\n";

			if (metallicExpr != nullptr)
				ss << std::format("\t\tfloat metallic = saturate({});\n", ToScalar(ctx, *metallicExpr));
			else
				ss << "\t\tfloat metallic = saturate(g_material.metallicFactor);\n";

			if (emissiveExpr != nullptr)
				ss << std::format("\t\tfloat3 emission = {};\n", ToFloat3(ctx, *emissiveExpr));
			else
				ss << "\t\tfloat3 emission = g_material.emissiveColour.rgb * g_material.emissiveColour.a;\n";

			if (opacityExpr != nullptr)
				ss << std::format("\t\tfloat opacity = saturate({});\n", ToScalar(ctx, *opacityExpr));
			else
				ss << "\t\tfloat opacity = 1.0f;\n";

			ss << "\t\tif (baseColor.a <= 0.0f && g_material.isInTransparencyPhase == 0)\n\t\t\tclip(-1);\n";
			ss << "\t\tif (g_material.isInTransparencyPhase == 0)\n\t\t{\n\t\t\tif (opacity < 1.0f)\n\t\t\t\tclip(-1);\n\t\t}\n\t\telse if (opacity <= 0.0f)\n\t\t{\n\t\t\tclip(-1);\n\t\t}\n";
			ss << "\t\tfloat3 finalRGB = baseColor.rgb + emission;\n";
			ss << "\t\tfloat2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));\n";
			ss << "\t\tfloat transparencyAlpha = saturate(opacity * baseColor.a);\n";
			ss << "\t\tfloat outputAlpha = g_material.isInTransparencyPhase ? transparencyAlpha : input.instanceID;\n";
			ss << "\t\toutput.diff = float4(finalRGB, outputAlpha);\n";
			ss << "\t\toutput.mat = float4(metallic, roughness, g_material.smoothness, g_material.specularProbability);\n";
			ss << "\t\toutput.norm = float4(worldNormal.xyz, pixelDepth);\n";
			ss << "\t\toutput.pos = float4(input.positionWS.xyz, length(emission));\n";
			ss << "\t\toutput.velocity = velocity;\n";
			ss << "\t\treturn output;\n\t}\n}\n";

			return ss.str();
		}

		fs::path ResolveShaderIncludeDirectory(std::vector<fs::path>* searchedCandidates = nullptr)
		{
			std::vector<fs::path> candidates;

			auto addCandidate = [&candidates](const fs::path& p)
			{
				if (!p.empty())
					candidates.push_back(p);
			};

			auto addCandidatesFromRoot = [&](const fs::path& root)
			{
				if (root.empty())
					return;

				addCandidate(root / "Shaders");
				addCandidate(root / "Data/Shaders");
				addCandidate(root / "EngineData/Shaders");
				addCandidate(root / "HexEngine.Shaders");
				addCandidate(root / "Source/HexEngine/HexEngine.Shaders");
			};

			addCandidate(g_pEnv->GetFileSystem().GetDataDirectory() / "Shaders");
			addCandidatesFromRoot(g_pEnv->GetFileSystem().GetBaseDirectory());
			addCandidatesFromRoot(fs::current_path());

			auto addFromAncestorChain = [&](fs::path start)
			{
				std::error_code ec;
				start = fs::weakly_canonical(start, ec);
				if (ec)
					return;

				for (fs::path p = start; !p.empty(); p = p.parent_path())
				{
					addCandidatesFromRoot(p);
					const auto parent = p.parent_path();
					if (parent == p)
						break;
				}
			};

			addFromAncestorChain(g_pEnv->GetFileSystem().GetBaseDirectory());
			addFromAncestorChain(g_pEnv->GetFileSystem().GetDataDirectory());
			addFromAncestorChain(fs::current_path());

			for (auto candidate : candidates)
			{
				std::error_code ec;
				candidate = fs::weakly_canonical(candidate, ec);
				if (ec)
					continue;
				if (searchedCandidates != nullptr)
					searchedCandidates->push_back(candidate);

				if (fs::exists(candidate / "MeshCommon.shader") && fs::exists(candidate / "Utils.shader"))
					return candidate;
			}

			return {};
		}

		bool CompileGeneratedShader(
			const fs::path& inputShaderPath,
			const fs::path& outputShaderPath,
			const fs::path& includeDirectory,
			std::vector<std::string>& errors)
		{
			const fs::path compilerExe = g_pEnv->GetFileSystem().GetBaseDirectory() / "HexEngine.ShaderCompiler.exe";
			if (!fs::exists(compilerExe))
			{
				errors.push_back(std::format("Shader compiler was not found at '{}'.", compilerExe.string()));
				return false;
			}

			std::wstring commandLine = std::format(
				L"\"{}\" -I \"{}\" -T hlsl -O \"{}\"",
				compilerExe.wstring(),
				inputShaderPath.wstring(),
				outputShaderPath.wstring());

			if (!includeDirectory.empty())
			{
				commandLine += std::format(L" -P \"{}{}\"", includeDirectory.wstring(), L"/");
			}

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};

			std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
			mutableCmd.push_back(L'\0');

			if (CreateProcessW(
				nullptr,
				mutableCmd.data(),
				nullptr,
				nullptr,
				FALSE,
				0,
				nullptr,
				nullptr,
				&si,
				&pi) == FALSE)
			{
				errors.push_back(std::format("Failed to launch HexEngine.ShaderCompiler.exe (error {}).", GetLastError()));
				return false;
			}

			WaitForSingleObject(pi.hProcess, INFINITE);

			DWORD exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);

			if (exitCode != 0)
			{
				errors.push_back("Shader compiler failed. Check the compiler output window for details.");
				return false;
			}

			if (!fs::exists(outputShaderPath))
			{
				errors.push_back("Shader compiler completed but no output shader was produced.");
				return false;
			}

			return true;
		}

		fs::path ResolveGeneratedShaderDirectory(const Material& material)
		{
			if (auto* fsys = material.GetOwningFileSystem(); fsys != nullptr)
			{
				const fs::path dataDir = fsys->GetDataDirectory();
				if (!dataDir.empty())
					return dataDir / "Shaders" / "Generated";
			}

			const fs::path materialAbsPath = material.GetAbsolutePath();
			for (fs::path p = materialAbsPath.parent_path(); !p.empty(); p = p.parent_path())
			{
				if (p.filename() == "Data" || p.filename() == "EngineData")
					return p / "Shaders" / "Generated";
			}

			if (!materialAbsPath.empty())
				return materialAbsPath.parent_path() / "GeneratedShaders";

			return {};
		}

		std::string ComputeShaderSourceHash(const std::string& shaderSource)
		{
			const uint64_t hashValue = static_cast<uint64_t>(std::hash<std::string>{}(shaderSource));
			return std::format("{:016x}", hashValue);
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

		CompilerContext ctx{ graph, overrides };

		GraphExpression baseColor;
		GraphExpression normal;
		GraphExpression roughness;
		GraphExpression metallic;
		GraphExpression emissive;
		GraphExpression opacity;

		GraphExpression* baseColorPtr = nullptr;
		GraphExpression* normalPtr = nullptr;
		GraphExpression* roughnessPtr = nullptr;
		GraphExpression* metallicPtr = nullptr;
		GraphExpression* emissivePtr = nullptr;
		GraphExpression* opacityPtr = nullptr;

		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::BaseColor, baseColor)) baseColorPtr = &baseColor;
		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::Normal, normal)) normalPtr = &normal;
		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::Roughness, roughness)) roughnessPtr = &roughness;
		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::Metallic, metallic)) metallicPtr = &metallic;
		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::Emissive, emissive)) emissivePtr = &emissive;
		if (EvaluateOutputExpression(ctx, MaterialGraphOutputSemantic::Opacity, opacity)) opacityPtr = &opacity;

		if (!ctx.errors.empty())
		{
			result.errors.insert(result.errors.end(), ctx.errors.begin(), ctx.errors.end());
			return result;
		}

		for (int32_t i = 0; i < MaterialTexture::Count; ++i)
			material.SetTexture((MaterialTexture)i, nullptr);

		for (int32_t slot = 0; slot < ctx.nextTextureSlot; ++slot)
		{
			if (ctx.slotPaths[(size_t)slot].empty())
				continue;
			material.SetTexture(SlotToMaterialTexture(slot), ITexture2D::Create(ctx.slotPaths[(size_t)slot]));
		}
		if (ctx.nextTextureSlot >= kMaxGraphTextureSlots - 2)
		{
			result.warnings.push_back(std::format(
				"Texture slot usage is near limit ({} of {}).",
				ctx.nextTextureSlot,
				kMaxGraphTextureSlots));
		}

		material._properties.hasTransparency = opacityPtr != nullptr ? 1 : 0;

		const fs::path generatedShaderDirectory = ResolveGeneratedShaderDirectory(material);
		if (generatedShaderDirectory.empty())
		{
			result.errors.push_back("Could not resolve a generated shader output directory for this material.");
			return result;
		}

		std::error_code ec;
		fs::create_directories(generatedShaderDirectory, ec);
		if (ec)
		{
			result.errors.push_back(std::format("Failed to create generated shader directory '{}': {}", generatedShaderDirectory.string(), ec.message()));
			return result;
		}

		const fs::path materialStem = material.GetAbsolutePath().empty() ? fs::path("MaterialGraph") : material.GetAbsolutePath().stem();

		const std::string shaderSource = BuildGraphShaderSource(
			ctx,
			baseColorPtr,
			normalPtr,
			roughnessPtr,
			metallicPtr,
			emissivePtr,
			opacityPtr);
		const std::string sourceHash = ComputeShaderSourceHash(shaderSource);

		const fs::path hashedShaderSource = generatedShaderDirectory / (materialStem.generic_string() + "_graph_" + sourceHash + ".shader");
		fs::path generatedShaderOutput = generatedShaderDirectory / (materialStem.generic_string() + "_graph_" + sourceHash + ".hcs");

		if (!fs::exists(generatedShaderOutput))
		{
			DiskFile shaderFile(hashedShaderSource, std::ios::out | std::ios::trunc);
			if (!shaderFile.Open())
			{
				result.errors.push_back(std::format("Failed to open generated shader source '{}' for writing.", hashedShaderSource.string()));
				return result;
			}

			shaderFile.Write((void*)shaderSource.data(), (uint32_t)shaderSource.size());
			shaderFile.Flush();
			shaderFile.Close();

			std::vector<fs::path> includeSearchPaths;
			const fs::path includeDir = ResolveShaderIncludeDirectory(&includeSearchPaths);
			if (includeDir.empty())
			{
				std::string searchList;
				for (size_t i = 0; i < includeSearchPaths.size() && i < 12; ++i)
				{
					if (!searchList.empty())
						searchList += "; ";
					searchList += includeSearchPaths[i].string();
				}
				if (searchList.empty())
					searchList = "<none>";
				result.errors.push_back(std::format(
					"Failed to locate shader include directory (MeshCommon.shader/Utils.shader). Searched: {}",
					searchList));
				return result;
			}

			if (!CompileGeneratedShader(hashedShaderSource, generatedShaderOutput, includeDir, result.errors))
				return result;
		}

		for (const auto& [paramName, slot] : ctx.textureParameterSlots)
		{
			result.textureParameterSlots.push_back({ paramName, slot });
		}

		material.SetStandardShader(IShader::Create(generatedShaderOutput));
		material.SetShadowMapShader(IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"));

		if (auto shader = material.GetStandardShader(); shader == nullptr)
		{
			result.errors.push_back(std::format("Compiled shader '{}' could not be loaded.", generatedShaderOutput.string()));
			return result;
		}

		result.success = true;
		return result;
	}

	MaterialGraphCompileResult MaterialGraphCompiler::ApplyInstanceToMaterial(
		const MaterialGraph& graph,
		const MaterialGraphInstanceData& instanceData,
		Material& material)
	{
		// Compile graph structure once (without overrides), then apply texture overrides without recompiling.
		MaterialGraphCompileResult result = CompileToMaterial(graph, material, nullptr);
		if (!result.success)
			return result;

		for (const auto& overrideValue : instanceData.overrides)
		{
			if (overrideValue.valueType != MaterialGraphValueType::Texture2D || overrideValue.texturePath.empty())
				continue;

			const auto it = std::find_if(
				result.textureParameterSlots.begin(),
				result.textureParameterSlots.end(),
				[&](const std::pair<std::string, int32_t>& binding)
				{
					return binding.first == overrideValue.name;
				});
			if (it == result.textureParameterSlots.end())
				continue;

			material.SetTexture(SlotToMaterialTexture(it->second), ITexture2D::Create(overrideValue.texturePath));
		}

		const bool hasNonTextureOverride = std::find_if(
			instanceData.overrides.begin(),
			instanceData.overrides.end(),
			[](const MaterialGraphParameterOverride& o)
			{
				return o.valueType == MaterialGraphValueType::Scalar ||
					o.valueType == MaterialGraphValueType::Vector2 ||
					o.valueType == MaterialGraphValueType::Vector3 ||
					o.valueType == MaterialGraphValueType::Vector4;
			}) != instanceData.overrides.end();
		if (hasNonTextureOverride)
		{
			result.warnings.push_back("Scalar/vector instance overrides currently require full graph recompilation and were not hot-applied.");
		}

		return result;
	}
}
