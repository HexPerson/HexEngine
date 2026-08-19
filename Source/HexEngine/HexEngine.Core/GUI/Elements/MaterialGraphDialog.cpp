#include "MaterialGraphDialog.hpp"

#include "Button.hpp"
#include "ContextMenu.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../GUI/UIManager.hpp"
#include <algorithm>
#include <format>

namespace HexEngine
{
	namespace
	{
		struct PinHit
		{
			std::string nodeId;
			std::string pinId;
			MaterialGraphPinDirection direction = MaterialGraphPinDirection::Input;
		};

		bool IsMaterialOutputNodeId(const std::string& id)
		{
			return id == "output_basecolor" ||
				id == "output_normal" ||
				id == "output_roughness" ||
				id == "output_metallic" ||
				id == "output_emissive" ||
				id == "output_opacity" ||
				id == "output_smoothness";
		}

		bool TryGetOutputSemanticByNodeId(const std::string& id, MaterialGraphOutputSemantic& outSemantic)
		{
			if (id == "output_basecolor") { outSemantic = MaterialGraphOutputSemantic::BaseColor; return true; }
			if (id == "output_normal") { outSemantic = MaterialGraphOutputSemantic::Normal; return true; }
			if (id == "output_roughness") { outSemantic = MaterialGraphOutputSemantic::Roughness; return true; }
			if (id == "output_metallic") { outSemantic = MaterialGraphOutputSemantic::Metallic; return true; }
			if (id == "output_emissive") { outSemantic = MaterialGraphOutputSemantic::Emissive; return true; }
			if (id == "output_opacity") { outSemantic = MaterialGraphOutputSemantic::Opacity; return true; }
			if (id == "output_smoothness") { outSemantic = MaterialGraphOutputSemantic::Smoothness; return true; }
			return false;
		}

		bool IsNumericValueType(MaterialGraphValueType type)
		{
			return type == MaterialGraphValueType::Scalar ||
				type == MaterialGraphValueType::Vector2 ||
				type == MaterialGraphValueType::Vector3 ||
				type == MaterialGraphValueType::Vector4 ||
				type == MaterialGraphValueType::UV;
		}

		std::string TryExtractNodeIdFromMessage(const std::string& message)
		{
			const size_t nodePos = message.find("node_");
			const size_t outputPos = message.find("output_");
			size_t pos = std::string::npos;
			if (nodePos != std::string::npos)
				pos = nodePos;
			else if (outputPos != std::string::npos)
				pos = outputPos;

			if (pos == std::string::npos)
				return {};

			size_t end = pos;
			while (end < message.size())
			{
				const char c = message[end];
				if ((c >= 'a' && c <= 'z') ||
					(c >= 'A' && c <= 'Z') ||
					(c >= '0' && c <= '9') ||
					c == '_')
				{
					++end;
					continue;
				}
				break;
			}

			return message.substr(pos, end - pos);
		}

		const char* GetOutputNodeIdForSemantic(MaterialGraphOutputSemantic semantic)
		{
			switch (semantic)
			{
			default:
			case MaterialGraphOutputSemantic::BaseColor: return "output_basecolor";
			case MaterialGraphOutputSemantic::Normal: return "output_normal";
			case MaterialGraphOutputSemantic::Roughness: return "output_roughness";
			case MaterialGraphOutputSemantic::Metallic: return "output_metallic";
			case MaterialGraphOutputSemantic::Emissive: return "output_emissive";
			case MaterialGraphOutputSemantic::Opacity: return "output_opacity";
			case MaterialGraphOutputSemantic::Smoothness: return "output_smoothness";
			}
		}

		MaterialGraphNode MakeMaterialOutputNode(
			const char* id,
			const char* name,
			const math::Vector2& position,
			MaterialGraphValueType valueType)
		{
			MaterialGraphNode node;
			node.id = id;
			node.nodeType = MaterialGraphNodeType::Output;
			node.displayName = name;
			node.position = position;
			node.inputPins.push_back({ "In", "In", valueType, MaterialGraphPinDirection::Input });
			return node;
		}

		// BuildGraphFromMaterial + MakeGraphNode + AddConnection helpers were
		// moved to MaterialGraph::CreateFromStandardMaterial so the AssetExplorer
		// "Convert to material graph" action can use the same conversion without
		// pulling in this dialog. MakeMaterialOutputNode above stays - it's also
		// used by EnsureGraphExists to backfill missing output nodes on existing
		// graphs.

		class MaterialGraphCanvasImpl final : public Element
		{
		public:
			MaterialGraphCanvasImpl(Element* parent, const Point& position, const Point& size, MaterialGraphDialog* owner, MaterialGraph* graph) :
				Element(parent, position, size),
				_owner(owner),
				_graph(graph)
			{
			}

			virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override
			{
				(void)w;
				(void)h;

				const auto abs = GetAbsolutePosition();
				renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, math::Color(HEX_RGBA_TO_FLOAT4(20, 20, 26, 255)));
				renderer->Frame(abs.x, abs.y, _size.x, _size.y, 1, math::Color(HEX_RGBA_TO_FLOAT4(40, 40, 48, 255)));

				if (_graph == nullptr)
					return;

				// Draw connections first.
				for (const auto& connection : _graph->connections)
				{
					auto fromCenter = GetPinCenter(connection.fromNodeId, connection.fromPinId, MaterialGraphPinDirection::Output);
					auto toCenter = GetPinCenter(connection.toNodeId, connection.toPinId, MaterialGraphPinDirection::Input);
					if (fromCenter.x < 0 || toCenter.x < 0)
						continue;

					const int32_t midX = (fromCenter.x + toCenter.x) / 2;
					DrawConnectionSegment(renderer, fromCenter.x, fromCenter.y, midX, fromCenter.y, 2, math::Color(HEX_RGBA_TO_FLOAT4(80, 140, 220, 255)));
					DrawConnectionSegment(renderer, midX, fromCenter.y, midX, toCenter.y, 2, math::Color(HEX_RGBA_TO_FLOAT4(80, 140, 220, 255)));
					DrawConnectionSegment(renderer, midX, toCenter.y, toCenter.x, toCenter.y, 2, math::Color(HEX_RGBA_TO_FLOAT4(80, 140, 220, 255)));
				}

				if (_pendingConnection.nodeId.length() > 0)
				{
					int32_t mx = 0;
					int32_t my = 0;
					g_pEnv->_inputSystem->GetMousePosition(mx, my);
					const auto fromCenter = GetPinCenter(_pendingConnection.nodeId, _pendingConnection.pinId, _pendingConnection.direction);
					if (fromCenter.x >= 0)
					{
						const int32_t midX = (fromCenter.x + mx) / 2;
						DrawConnectionSegment(renderer, fromCenter.x, fromCenter.y, midX, fromCenter.y, 1, math::Color(HEX_RGBA_TO_FLOAT4(140, 170, 255, 255)));
						DrawConnectionSegment(renderer, midX, fromCenter.y, midX, my, 1, math::Color(HEX_RGBA_TO_FLOAT4(140, 170, 255, 255)));
						DrawConnectionSegment(renderer, midX, my, mx, my, 1, math::Color(HEX_RGBA_TO_FLOAT4(140, 170, 255, 255)));
					}
				}

				for (const auto& node : _graph->nodes)
				{
					const auto r = GetNodeRect(node);
					const bool isSelected = node.id == _selectedNodeId;
					renderer->FillQuad(r.left, r.top, r.right - r.left, r.bottom - r.top, isSelected ? math::Color(HEX_RGBA_TO_FLOAT4(52, 58, 74, 255)) : math::Color(HEX_RGBA_TO_FLOAT4(34, 36, 44, 255)));
					renderer->Frame(r.left, r.top, r.right - r.left, r.bottom - r.top, 1, isSelected ? math::Color(HEX_RGBA_TO_FLOAT4(120, 150, 220, 255)) : math::Color(HEX_RGBA_TO_FLOAT4(60, 65, 80, 255)));
					renderer->FillQuad(r.left, r.top, r.right - r.left, 20, math::Color(HEX_RGBA_TO_FLOAT4(45, 50, 64, 255)));

					const std::wstring nodeName = node.displayName.empty() ? s2ws(node.id) : s2ws(node.displayName);
					renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, r.left + 6, r.top + 10, renderer->_style.text_regular, FontAlign::CentreUD, nodeName);

					DrawPins(renderer, node, r);
				}
			}

			virtual bool OnInputEvent(InputEvent event, InputData* data) override
			{
				if (Element::OnInputEvent(event, data))
					return true;

				if (_graph == nullptr)
					return false;

				if (event == InputEvent::MouseDown)
				{
					if (data->MouseDown.button == VK_LBUTTON && IsMouseOver(true))
					{
						g_pEnv->GetUIManager().SetInputFocus(this);

						PinHit pinHit;
						if (TryHitPin(data->MouseDown.xpos, data->MouseDown.ypos, pinHit))
						{
							if (_pendingConnection.nodeId.empty())
							{
								if (pinHit.direction == MaterialGraphPinDirection::Output)
								{
									_pendingConnection = pinHit;
								}
								else
								{
									// Clicking an input pin with no pending connection removes its existing link.
									_graph->connections.erase(
										std::remove_if(_graph->connections.begin(), _graph->connections.end(),
											[&](const MaterialGraphConnection& connection)
											{
												return connection.toNodeId == pinHit.nodeId && connection.toPinId == pinHit.pinId;
											}),
										_graph->connections.end());

									// Keep graph.outputs in sync for BOTH output layouts:
									// legacy output_* stub nodes map by node id, the unified
									// PbrOutput node maps by pin id (pin id == semantic name).
									// Missing the PbrOutput case left a stale binding behind,
									// so the compiler kept using the old source after the
									// artist visibly deleted the wire.
									MaterialGraphOutputSemantic semantic;
									bool affectsBinding = TryGetOutputSemanticByNodeId(pinHit.nodeId, semantic);
									if (!affectsBinding)
									{
										const auto* toNode = _graph->FindNode(pinHit.nodeId);
										affectsBinding = toNode != nullptr &&
											toNode->nodeType == MaterialGraphNodeType::PbrOutput &&
											MaterialGraph::ParseOutputSemantic(pinHit.pinId, semantic);
									}
									if (affectsBinding)
									{
										for (auto& output : _graph->outputs)
										{
											if (output.semantic == semantic)
											{
												output.nodeId.clear();
												output.pinId.clear();
												break;
											}
										}
									}
									_owner->MarkDirty();
								}
							}
							else
							{
								if (_pendingConnection.direction == MaterialGraphPinDirection::Output &&
									pinHit.direction == MaterialGraphPinDirection::Input)
								{
									ConnectPins(_pendingConnection, pinHit);
									_pendingConnection = {};
								}
								else
								{
									_pendingConnection = pinHit.direction == MaterialGraphPinDirection::Output ? pinHit : PinHit{};
								}
							}
							return true;
						}

						// Any non-pin click drops an in-progress connection drag so the
						// ghost wire doesn't stick to the cursor forever.
						_pendingConnection = {};

						const auto* node = FindNodeAt(data->MouseDown.xpos, data->MouseDown.ypos);
						if (node != nullptr)
						{
							_selectedNodeId = node->id;
							_draggingNodeId = node->id;
							_dragMouseStart = Point(data->MouseDown.xpos, data->MouseDown.ypos);
							const auto* selectedNode = _graph->FindNode(node->id);
							if (selectedNode != nullptr)
								_dragNodeStart = Point((int32_t)selectedNode->position.x, (int32_t)selectedNode->position.y);
							_owner->OnNodeSelectionChanged(_selectedNodeId);
							return true;
						}
						else
						{
							_selectedNodeId.clear();
							_owner->OnNodeSelectionChanged(_selectedNodeId);
						}
				}
				else if (data->MouseDown.button == VK_RBUTTON && IsMouseOver(true))
				{
					OpenAddNodeMenu(Point(data->MouseDown.xpos, data->MouseDown.ypos));
					return true;
				}
				}
				else if (event == InputEvent::MouseMove)
				{
					if (!_draggingNodeId.empty())
					{
						auto* node = _graph->FindNode(_draggingNodeId);
						if (node != nullptr)
						{
							const int32_t dx = (int32_t)data->MouseMove.x - _dragMouseStart.x;
							const int32_t dy = (int32_t)data->MouseMove.y - _dragMouseStart.y;
							node->position.x = (float)(_dragNodeStart.x + dx);
							node->position.y = (float)(_dragNodeStart.y + dy);
							_owner->MarkDirty();
						}
						return true;
					}
				}
				else if (event == InputEvent::MouseUp)
				{
					if (data->MouseUp.button == VK_LBUTTON)
					{
						_draggingNodeId.clear();
						return IsMouseOver(true);
					}
				}
				else if (event == InputEvent::KeyDown && IsInputFocus())
				{
					if (data->KeyDown.key == VK_DELETE && !_selectedNodeId.empty())
					{
						DeleteSelectedNode();
						return true;
					}
				}

				return false;
			}

			const std::string& GetSelectedNodeId() const { return _selectedNodeId; }
			void SetSelectedNodeId(const std::string& id) { _selectedNodeId = id; }

		private:
			static RECT MakeRect(int32_t x, int32_t y, int32_t w, int32_t h)
			{
				RECT r{};
				r.left = x;
				r.top = y;
				r.right = x + w;
				r.bottom = y + h;
				return r;
			}

			RECT GetNodeRect(const MaterialGraphNode& node) const
			{
				const auto abs = GetAbsolutePosition();
				const int32_t x = abs.x + (int32_t)node.position.x;
				const int32_t y = abs.y + (int32_t)node.position.y;
				const int32_t pinRows = (int32_t)std::max(node.inputPins.size(), node.outputPins.size());
				const int32_t h = std::max(70, 26 + (pinRows * 16));
				return MakeRect(x, y, 190, h);
			}

			RECT GetPinRect(const MaterialGraphNode& node, const MaterialGraphPin& pin, MaterialGraphPinDirection direction, int32_t index) const
			{
				const auto r = GetNodeRect(node);
				const int32_t y = r.top + 28 + (index * 16);
				if (direction == MaterialGraphPinDirection::Input)
					return MakeRect(r.left - 4, y, 8, 8);
				return MakeRect(r.right - 4, y, 8, 8);
			}

			Point GetPinCenter(const std::string& nodeId, const std::string& pinId, MaterialGraphPinDirection direction) const
			{
				const auto* node = _graph->FindNode(nodeId);
				if (node == nullptr)
					return Point(-1, -1);

				const auto& pins = direction == MaterialGraphPinDirection::Input ? node->inputPins : node->outputPins;
				for (size_t i = 0; i < pins.size(); ++i)
				{
					if (pins[i].id == pinId)
					{
						const auto r = GetPinRect(*node, pins[i], direction, (int32_t)i);
						return Point((r.left + r.right) / 2, (r.top + r.bottom) / 2);
					}
				}

				return Point(-1, -1);
			}

			void DrawPins(GuiRenderer* renderer, const MaterialGraphNode& node, const RECT& rect) const
			{
				for (int32_t i = 0; i < (int32_t)node.inputPins.size(); ++i)
				{
					const auto& pin = node.inputPins[(size_t)i];
					const auto p = GetPinRect(node, pin, MaterialGraphPinDirection::Input, i);
					renderer->FillQuad(p.left, p.top, p.right - p.left, p.bottom - p.top, math::Color(HEX_RGBA_TO_FLOAT4(180, 180, 200, 255)));
					renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, p.right + 4, p.top + 4, renderer->_style.text_regular, FontAlign::CentreUD, s2ws(pin.name));
				}

				for (int32_t i = 0; i < (int32_t)node.outputPins.size(); ++i)
				{
					const auto& pin = node.outputPins[(size_t)i];
					const auto p = GetPinRect(node, pin, MaterialGraphPinDirection::Output, i);
					renderer->FillQuad(p.left, p.top, p.right - p.left, p.bottom - p.top, math::Color(HEX_RGBA_TO_FLOAT4(110, 180, 240, 255)));

					int32_t tw = 0;
					int32_t th = 0;
					renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, s2ws(pin.name), tw, th);
					renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, p.left - tw - 4, p.top + 4, renderer->_style.text_regular, FontAlign::CentreUD, s2ws(pin.name));
				}

				(void)rect;
			}

			static void DrawConnectionSegment(GuiRenderer* renderer, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thickness, const math::Color& color)
			{
				if (x0 == x1)
				{
					const int32_t y = std::min(y0, y1);
					renderer->FillQuad(x0 - thickness / 2, y, thickness, std::max(1, std::abs(y1 - y0)), color);
				}
				else if (y0 == y1)
				{
					const int32_t x = std::min(x0, x1);
					renderer->FillQuad(x, y0 - thickness / 2, std::max(1, std::abs(x1 - x0)), thickness, color);
				}
			}

			const MaterialGraphNode* FindNodeAt(int32_t x, int32_t y) const
			{
				for (auto it = _graph->nodes.rbegin(); it != _graph->nodes.rend(); ++it)
				{
					const auto r = GetNodeRect(*it);
					if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
						return &(*it);
				}

				return nullptr;
			}

			bool TryHitPin(int32_t x, int32_t y, PinHit& outHit) const
			{
				for (const auto& node : _graph->nodes)
				{
					for (int32_t i = 0; i < (int32_t)node.inputPins.size(); ++i)
					{
						const auto& pin = node.inputPins[(size_t)i];
						const auto r = GetPinRect(node, pin, MaterialGraphPinDirection::Input, i);
						if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom)
						{
							outHit.nodeId = node.id;
							outHit.pinId = pin.id;
							outHit.direction = MaterialGraphPinDirection::Input;
							return true;
						}
					}
					for (int32_t i = 0; i < (int32_t)node.outputPins.size(); ++i)
					{
						const auto& pin = node.outputPins[(size_t)i];
						const auto r = GetPinRect(node, pin, MaterialGraphPinDirection::Output, i);
						if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom)
						{
							outHit.nodeId = node.id;
							outHit.pinId = pin.id;
							outHit.direction = MaterialGraphPinDirection::Output;
							return true;
						}
					}
				}

				return false;
			}

			void ConnectPins(const PinHit& outputPin, const PinHit& inputPin)
			{
				const auto* outPin = _graph->FindPin(outputPin.nodeId, outputPin.pinId, MaterialGraphPinDirection::Output);
				const auto* inPin = _graph->FindPin(inputPin.nodeId, inputPin.pinId, MaterialGraphPinDirection::Input);
				if (outPin == nullptr || inPin == nullptr)
				{
					_owner->SetStatusText(L"Invalid pin selection.", true);
					return;
				}

				if (!IsCompatible(inputPin.nodeId, outPin->valueType, inPin->valueType))
				{
					_owner->SetStatusText(
						std::format(
							L"Cannot connect {} to {} (type mismatch: {} -> {}).",
							s2ws(outputPin.nodeId),
							s2ws(inputPin.nodeId),
							s2ws(MaterialGraph::ValueTypeToString(outPin->valueType)),
							s2ws(MaterialGraph::ValueTypeToString(inPin->valueType))),
						true);
					return;
				}

				_graph->connections.erase(
					std::remove_if(_graph->connections.begin(), _graph->connections.end(),
						[&](const MaterialGraphConnection& connection)
						{
							return connection.toNodeId == inputPin.nodeId && connection.toPinId == inputPin.pinId;
						}),
					_graph->connections.end());

				MaterialGraphConnection connection;
				connection.fromNodeId = outputPin.nodeId;
				connection.fromPinId = outputPin.pinId;
				connection.toNodeId = inputPin.nodeId;
				connection.toPinId = inputPin.pinId;
				_graph->connections.push_back(std::move(connection));

				// Update graph.outputs for both output layouts (legacy output_*
				// stubs by node id, unified PbrOutput by pin id). Without the
				// PbrOutput case, a wire dragged into the PBR Output node changed
				// `connections` but not the binding the compiler reads, so the new
				// wiring did nothing until the dialog was reopened.
				MaterialGraphOutputSemantic semantic;
				bool affectsBinding = TryGetOutputSemanticByNodeId(inputPin.nodeId, semantic);
				if (!affectsBinding)
				{
					const auto* toNode = _graph->FindNode(inputPin.nodeId);
					affectsBinding = toNode != nullptr &&
						toNode->nodeType == MaterialGraphNodeType::PbrOutput &&
						MaterialGraph::ParseOutputSemantic(inputPin.pinId, semantic);
				}
				if (affectsBinding)
				{
					for (auto& output : _graph->outputs)
					{
						if (output.semantic == semantic)
						{
							output.nodeId = outputPin.nodeId;
							output.pinId = outputPin.pinId;
							break;
						}
					}
				}

				_owner->SetStatusText(L"Connected.", false);
				_owner->MarkDirty();
			}

			bool IsCompatible(const std::string& toNodeId, MaterialGraphValueType from, MaterialGraphValueType to) const
			{
				if (from == to)
					return true;

				if (IsNumericValueType(from) && IsNumericValueType(to))
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

				if ((from == MaterialGraphValueType::Vector2 && to == MaterialGraphValueType::Scalar) ||
					(from == MaterialGraphValueType::Vector3 && to == MaterialGraphValueType::Scalar) ||
					(from == MaterialGraphValueType::Vector4 && to == MaterialGraphValueType::Scalar))
				{
					return true;
				}

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

			void DeleteSelectedNode()
			{
				const auto selected = _selectedNodeId;
				if (selected.empty())
					return;
				// Refuse to delete terminal output nodes - both the legacy output_*
				// stubs and the unified PbrOutput node. The context menu has no way
				// to re-add a PbrOutput, so deleting it used to permanently orphan
				// the graph until the dialog was closed and reopened.
				const auto* selectedNode = _graph->FindNode(selected);
				const bool isOutputNode = IsMaterialOutputNodeId(selected) ||
					(selectedNode != nullptr &&
						(selectedNode->nodeType == MaterialGraphNodeType::Output ||
							selectedNode->nodeType == MaterialGraphNodeType::PbrOutput));
				if (isOutputNode)
				{
					_owner->SetStatusText(L"Material output nodes cannot be deleted.", true);
					return;
				}

				_graph->nodes.erase(
					std::remove_if(_graph->nodes.begin(), _graph->nodes.end(),
						[&](const MaterialGraphNode& node)
						{
							return node.id == selected;
						}),
					_graph->nodes.end());

				_graph->connections.erase(
					std::remove_if(_graph->connections.begin(), _graph->connections.end(),
						[&](const MaterialGraphConnection& connection)
						{
							return connection.fromNodeId == selected || connection.toNodeId == selected;
						}),
					_graph->connections.end());

				for (auto& output : _graph->outputs)
				{
					if (output.nodeId == selected)
					{
						output.nodeId.clear();
						output.pinId.clear();
					}
				}

				_selectedNodeId.clear();
				_owner->OnNodeSelectionChanged(_selectedNodeId);
				_owner->MarkDirty();
			}

			static std::vector<MaterialGraphPin> BuildInputPins(MaterialGraphNodeType type)
			{
				switch (type)
				{
				case MaterialGraphNodeType::TextureSample:
					return
					{
						{ "Tex", "Tex", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Input },
						{ "UV", "UV", MaterialGraphValueType::UV, MaterialGraphPinDirection::Input }
					};
				case MaterialGraphNodeType::Add:
				case MaterialGraphNodeType::Multiply:
					return
					{
						{ "A", "A", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input },
						{ "B", "B", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input }
					};
				case MaterialGraphNodeType::Lerp:
					return
					{
						{ "A", "A", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input },
						{ "B", "B", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input },
						{ "Alpha", "Alpha", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Input }
					};
				case MaterialGraphNodeType::OneMinus:
					return { { "In", "In", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Input } };
				case MaterialGraphNodeType::NormalMap:
					return { { "Normal", "Normal", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Input } };
				default:
					break;
				}

				return {};
			}

			static std::vector<MaterialGraphPin> BuildOutputPins(MaterialGraphNodeType type)
			{
				switch (type)
				{
				case MaterialGraphNodeType::ScalarConstant:
				case MaterialGraphNodeType::ScalarParameter:
				case MaterialGraphNodeType::WeatherScalar:
					return { { "Out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::VectorConstant:
				case MaterialGraphNodeType::VectorParameter:
				case MaterialGraphNodeType::WeatherVector:
				case MaterialGraphNodeType::Add:
				case MaterialGraphNodeType::Multiply:
				case MaterialGraphNodeType::Lerp:
				case MaterialGraphNodeType::OneMinus:
				case MaterialGraphNodeType::NormalMap:
					return { { "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::TextureSample:
					return { { "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::TextureParameter:
					return { { "Out", "Out", MaterialGraphValueType::Texture2D, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::TexCoord:
					return { { "Out", "Out", MaterialGraphValueType::UV, MaterialGraphPinDirection::Output } };
				default:
					break;
				}
				return {};
			}

			void AddNode(MaterialGraphNodeType type, const Point& mousePos)
			{
				MaterialGraphNode node;
				// _nodeIdCounter resets to 1 whenever the dialog reopens, but the
				// loaded graph may already contain ids from earlier sessions. Keep
				// bumping until the id is actually free - a duplicate id fails
				// validation ("Duplicate node id") and corrupts hit-testing /
				// deletion, which both match nodes by id.
				do
				{
					node.id = std::format("node_{}_{}", (int32_t)type, _nodeIdCounter++);
				} while (_graph->FindNode(node.id) != nullptr);
				node.nodeType = type;
				node.displayName = MaterialGraph::NodeTypeToString(type);
				const auto abs = GetAbsolutePosition();
				node.position = math::Vector2((float)(mousePos.x - abs.x), (float)(mousePos.y - abs.y));
				node.scalarValue = 0.5f;
				node.vectorValue = math::Vector4::One;
				node.inputPins = BuildInputPins(type);
				node.outputPins = BuildOutputPins(type);
				_graph->nodes.push_back(std::move(node));
				_owner->MarkDirty();
			}

			void OpenAddNodeMenu(const Point& mousePos)
			{
				if (_contextMenu != nullptr)
				{
					_contextMenu->DeleteMe();
					_contextMenu = nullptr;
				}

				auto* root = g_pEnv->GetUIManager().GetRootElement();
				if (root == nullptr)
					return;

				_contextMenu = new ContextMenu(root, Point(mousePos.x - root->GetAbsolutePosition().x, mousePos.y - root->GetAbsolutePosition().y));
				_contextMenu->AddItem(new ContextItem(L"Scalar Constant", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::ScalarConstant, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Vector Constant", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::VectorConstant, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Texture Sample", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::TextureSample, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"TexCoord", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::TexCoord, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Add", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::Add, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Multiply", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::Multiply, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Lerp", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::Lerp, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"OneMinus", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::OneMinus, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"NormalMap", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::NormalMap, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Scalar Parameter", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::ScalarParameter, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Vector Parameter", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::VectorParameter, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Texture Parameter", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::TextureParameter, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Weather Scalar", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::WeatherScalar, mousePos); }));
				_contextMenu->AddItem(new ContextItem(L"Weather Vector", [this, mousePos](const std::wstring&) { AddNode(MaterialGraphNodeType::WeatherVector, mousePos); }));
			}

		private:
			MaterialGraphDialog* _owner = nullptr;
			MaterialGraph* _graph = nullptr;
			std::string _selectedNodeId;
			std::string _draggingNodeId;
			Point _dragMouseStart = Point();
			Point _dragNodeStart = Point();
			PinHit _pendingConnection = {};
			ContextMenu* _contextMenu = nullptr;
			int32_t _nodeIdCounter = 1;
		};
	}

	MaterialGraphDialog::MaterialGraphDialog(
		Element* parent,
		const Point& position,
		const Point& size,
		const std::wstring& title,
		const std::shared_ptr<Material>& material,
		bool embeddedMode) :
		Dialog(parent, position, size, title),
		_material(material),
		_embeddedMode(embeddedMode)
	{
		if (_material != nullptr)
			_material->IncrementEditorOpenCount();

		// INSTANCE MODE: a graph-instance material owns no graph of its own -
		// it references a parent graph material and stores parameter
		// overrides. Display the PARENT's graph via a local copy (never
		// saved) and restrict persistent edits to the override set. This must
		// be decided BEFORE EnsureGraphExists, which would otherwise see
		// !_hasGraph and "promote" the instance to a flattened standard graph
		// - corrupting it.
		if (_material != nullptr && _material->_hasGraphInstance && !_material->_hasGraph)
		{
			auto parent = Material::Create(_material->_graphInstance.parentMaterialPath);
			if (parent != nullptr && parent->_hasGraph)
			{
				_parentMaterial = parent;
				_instanceViewGraph = parent->_graph;
				_instanceMode = true;

				// Show the instance's current override values on the parameter
				// nodes so the canvas reflects THIS instance, not the parent
				// defaults.
				for (const auto& ov : _material->_graphInstance.overrides)
				{
					for (auto& node : _instanceViewGraph.nodes)
					{
						if (node.parameterName == ov.name)
						{
							node.scalarValue = ov.scalarValue;
							node.vectorValue = ov.vectorValue;
							if (!ov.texturePath.empty())
								node.texturePath = ov.texturePath;
						}
					}
				}
			}
		}

		if (!_instanceMode)
			EnsureGraphExists();

		const int32_t topOffset = _embeddedMode ? 8 : 36;
		const int32_t graphTop = _embeddedMode ? 34 : 62;

		_statusLine = new LineEdit(this, Point(10, topOffset), Point(size.x - 220, 20), L"Status");
		_statusLine->SetDoesCallbackWaitForReturn(false);
		_statusLine->SetValue(L"Ready");
		// Read-only, NOT DisableRecursive: Disable() means "don't render" in
		// this toolkit (UIManager::RenderElement skips disabled elements), so
		// the status line - the ONLY compile success/failure feedback - was
		// never drawn at all. Compile failures looked like the button doing
		// nothing. EnableInput(false) keeps it visible but non-editable
		// (SetHasInputFocus refuses focus when input is disabled).
		_statusLine->EnableInput(false);

		new Button(this, Point(size.x - 200, topOffset - 2), Point(90, 24), L"Compile", [this](Button*) { return CompileOnly(); });
		new Button(this, Point(size.x - 104, topOffset - 2), Point(90, 24), L"Apply", [this](Button*) { return SaveAndApply(); });

		_canvas = new MaterialGraphCanvasImpl(this, Point(10, graphTop), Point((size.x * 70) / 100 - 20, size.y - graphTop - 10), this,
			_instanceMode ? &_instanceViewGraph : &_material->_graph);

		_properties = new ComponentWidget(this, Point((size.x * 70) / 100 + 10, graphTop), Point(size.x - ((size.x * 70) / 100) - 20, size.y - graphTop - 10), L"Node Properties");

		// Material-level scalars that aren't node-driven. Placed at the top of the
		// properties panel so they're always visible regardless of which node is
		// selected. rainDripIntensity multiplies g_weatherSurface.wetness in the
		// graph-compiled PS to drive procedural rain droplets - turn this up on
		// the dome / car / glass materials so they bead with rain, leave at 0 for
		// surfaces that should look the same regardless of weather.
		new DragFloat(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20),
			L"Rain Drip Intensity", &_material->_properties.rainDripIntensity, 0.0f, 1.0f, 0.01f, 2);

		_selectedNodeLabel = new LineEdit(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20), L"Selected Node");
		_selectedNodeLabel->SetDoesCallbackWaitForReturn(false);
		_selectedNodeLabel->EnableInput(false); // read-only but visible (Disable = hidden)

		_parameterName = new LineEdit(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20), L"Parameter Name");
		_parameterName->SetOnInputFn([this](LineEdit*, const std::wstring& value)
		{
			// Parameter NAMES belong to the parent graph - renaming from an
			// instance would silently orphan every sibling instance's override.
			if (_instanceMode)
				return;
			if (auto* node = GetSelectedNode(); node != nullptr)
			{
				node->parameterName = ws2s(value);
				SyncParameterDefinition(*node);
				MarkDirty();
			}
		});

		_scalarValue = new DragFloat(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20), L"Scalar", &_scalarScratch, -10.0f, 10.0f, 0.01f, 3);
		_scalarValue->SetOnDrag([this](float value, float, float)
		{
			if (auto* node = GetSelectedNode(); node != nullptr)
			{
				node->scalarValue = value;
				WriteInstanceOverrideFromNode(*node);
				MarkDirty();
			}
		});

		for (int32_t i = 0; i < 4; ++i)
		{
			_vectorValue[i] = 0.0f;
			_vectorDrags[i] = new DragFloat(
				_properties,
				_properties->GetNextPos(),
				Point(_properties->GetSize().x - 20, 20),
				std::format(L"Vector {}", i),
				&_vectorValue[i],
				-10.0f,
				10.0f,
				0.01f,
				3);
			_vectorDrags[i]->SetOnDrag([this, i](float value, float, float)
			{
				_vectorValue[i] = value;
				if (auto* node = GetSelectedNode(); node != nullptr)
				{
					node->vectorValue = math::Vector4(_vectorValue[0], _vectorValue[1], _vectorValue[2], _vectorValue[3]);
					WriteInstanceOverrideFromNode(*node);
					MarkDirty();
				}
			});
		}

		_texturePath = new AssetSearch(
			_properties,
			_properties->GetNextPos(),
			Point(_properties->GetSize().x - 20, 80),
			L"Texture",
			{ ResourceType::Image },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				if (auto* node = GetSelectedNode(); node != nullptr)
				{
					const fs::path path = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					node->texturePath = path;
					WriteInstanceOverrideFromNode(*node);
					MarkDirty();
				}
			});

		// PbrOutput per-material widgets. Created once here, enabled only when
		// a PbrOutput node is selected. Each widget's on-edit callback writes
		// back to the currently-selected PbrOutput node's pbrOutputProperties
		// AND mirrors into a scratch field (so the widget can show a stable
		// value when nothing's selected).
		const auto pbrRowSize = Point(_properties->GetSize().x - 20, 20);

		_pbrTransparencyToggle = new Checkbox(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Has Transparency", &_pbrHasTransparency);
		_pbrTransparencyToggle->SetOnCheckFn([this](Checkbox*, bool v)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.hasTransparency = v ? 1 : 0; MarkDirty();
			}
		});

		_pbrAffectsGiToggle = new Checkbox(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Affects GI", &_pbrAffectsGI);
		_pbrAffectsGiToggle->SetOnCheckFn([this](Checkbox*, bool v)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.affectsGI = v ? 1 : 0; MarkDirty();
			}
		});

		_pbrEmissiveGiToggle = new Checkbox(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Emissive Affects GI", &_pbrEmissiveAffectsGI);
		_pbrEmissiveGiToggle->SetOnCheckFn([this](Checkbox*, bool v)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.emissiveAffectsGI = v ? 1 : 0; MarkDirty();
			}
		});

		_pbrReceivesSnowToggle = new Checkbox(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Receives Snow", &_pbrReceivesSnow);
		_pbrReceivesSnowToggle->SetOnCheckFn([this](Checkbox*, bool v)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.receivesSnow = v ? 1 : 0; MarkDirty();
			}
			// Apply straight to the Material too - receivesSnow only reaches
			// _receivesSnow via CompileToMaterial otherwise, which is SKIPPED
			// for cached graph shaders, so the node said true while the
			// material (and the .hmat, and the runtime shell gate) stayed
			// false. Direct apply keeps all three in sync regardless of
			// whether a recompile runs. (The node value still drives a fresh
			// compile / new material seeded from this graph.)
			if (_material)
				_material->SetReceivesSnow(v);
		});

		_pbrRainDripDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Rain Drip Intensity", &_pbrRainDripIntensity, 0.0f, 1.0f, 0.01f, 2);
		_pbrRainDripDrag->SetOnDrag([this](float v, float, float)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.rainDripIntensity = v; MarkDirty();
			}
		});

		// Wind sway (bend / flutter / height / mode). Same direct-apply rule as
		// Receives Snow above: CompileToMaterial is skipped for cached graph
		// shaders, so the drags write the Material's live properties too or the
		// node would say "sway" while the uploaded cbuffer stayed zero.
		auto applyWindSway = [this](int lane, float v)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				float* lanes = &n->pbrOutputProperties.windSwayParams.x;
				lanes[lane] = v;
				MarkDirty();
			}
			if (_material)
			{
				float* lanes = &_material->_properties.windSwayParams.x;
				lanes[lane] = v;
			}
		};
		_pbrWindSwayModeDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Wind Sway Mode (0/1/2)", &_pbrWindSwayMode, 0.0f, 2.0f, 1.0f, 0);
		_pbrWindSwayModeDrag->SetOnDrag([applyWindSway](float v, float, float) { applyWindSway(3, v); });
		_pbrWindSwayBendDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Wind Sway Bend", &_pbrWindSwayBend, 0.0f, 2.0f, 0.01f, 2);
		_pbrWindSwayBendDrag->SetOnDrag([applyWindSway](float v, float, float) { applyWindSway(0, v); });
		_pbrWindSwayFlutterDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Wind Sway Flutter", &_pbrWindSwayFlutter, 0.0f, 2.0f, 0.01f, 2);
		_pbrWindSwayFlutterDrag->SetOnDrag([applyWindSway](float v, float, float) { applyWindSway(1, v); });
		_pbrWindSwayHeightDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Wind Sway Height (m)", &_pbrWindSwayHeight, 0.1f, 40.0f, 0.1f, 1);
		_pbrWindSwayHeightDrag->SetOnDrag([applyWindSway](float v, float, float) { applyWindSway(2, v); });

		_pbrCullDistanceDrag = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
			L"Cull Distance", &_pbrCullDistance, 0.0f, 10000.0f, 1.0f, 1);
		_pbrCullDistanceDrag->SetOnDrag([this](float v, float, float)
		{
			if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
			{
				n->pbrOutputProperties.cullDistance = v; MarkDirty();
			}
		});

		for (int32_t i = 0; i < 4; ++i)
		{
			_pbrModelParamDrags[i] = new DragFloat(_properties, _properties->GetNextPos(), pbrRowSize,
				std::format(L"Model Param {}", i), &_pbrModelParams[i], -1.0f, 1.0f, 0.01f, 3);
			_pbrModelParamDrags[i]->SetOnDrag([this, i](float v, float, float)
			{
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					switch (i)
					{
					case 0: n->pbrOutputProperties.modelParams.x = v; break;
					case 1: n->pbrOutputProperties.modelParams.y = v; break;
					case 2: n->pbrOutputProperties.modelParams.z = v; break;
					case 3: n->pbrOutputProperties.modelParams.w = v; break;
					}
					MarkDirty();
				}
			});
		}

		// Shading-model dropdown. Mirrors MaterialDialog's options.
		_pbrShadingModelDrop = new DropDown(_properties, _properties->GetNextPos(), Point(200, 18), L"Shading Model");
		{
			auto setModel = [this](int m) {
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					n->pbrOutputProperties.materialModel = m; MarkDirty();
				}
			};
			_pbrShadingModelDrop->GetContextMenu()->AddItem(new ContextItem(L"Standard PBR",      [setModel](const std::wstring&) { setModel(0); }));
			_pbrShadingModelDrop->GetContextMenu()->AddItem(new ContextItem(L"Subsurface (SSS)",  [setModel](const std::wstring&) { setModel(1); }));
			_pbrShadingModelDrop->GetContextMenu()->AddItem(new ContextItem(L"Clearcoat",          [setModel](const std::wstring&) { setModel(2); }));
			_pbrShadingModelDrop->GetContextMenu()->AddItem(new ContextItem(L"Anisotropic",        [setModel](const std::wstring&) { setModel(3); }));
			_pbrShadingModelDrop->GetContextMenu()->AddItem(new ContextItem(L"Sheen / Cloth",      [setModel](const std::wstring&) { setModel(4); }));
		}

		// Depth-state dropdown.
		_pbrDepthStateDrop = new DropDown(_properties, _properties->GetNextPos(), Point(200, 18), L"Depth State");
		{
			auto setDS = [this](DepthBufferState ds) {
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					n->pbrOutputProperties.depthState = ds; MarkDirty();
				}
			};
			_pbrDepthStateDrop->GetContextMenu()->AddItem(new ContextItem(L"None",          [setDS](const std::wstring&) { setDS(DepthBufferState::DepthNone); }));
			_pbrDepthStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Default",       [setDS](const std::wstring&) { setDS(DepthBufferState::DepthDefault); }));
			_pbrDepthStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Read",          [setDS](const std::wstring&) { setDS(DepthBufferState::DepthRead); }));
			_pbrDepthStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Reverse-Z",     [setDS](const std::wstring&) { setDS(DepthBufferState::DepthReverseZ); }));
			_pbrDepthStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Read Reverse-Z",[setDS](const std::wstring&) { setDS(DepthBufferState::DepthReadReverseZ); }));
		}

		// Blend-state dropdown.
		_pbrBlendStateDrop = new DropDown(_properties, _properties->GetNextPos(), Point(200, 18), L"Blend State");
		{
			auto setBS = [this](BlendState bs) {
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					n->pbrOutputProperties.blendState = bs; MarkDirty();
				}
			};
			_pbrBlendStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Opaque",       [setBS](const std::wstring&) { setBS(BlendState::Opaque); }));
			_pbrBlendStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Additive",     [setBS](const std::wstring&) { setBS(BlendState::Additive); }));
			_pbrBlendStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Subtractive",  [setBS](const std::wstring&) { setBS(BlendState::Subtractive); }));
			_pbrBlendStateDrop->GetContextMenu()->AddItem(new ContextItem(L"Transparency", [setBS](const std::wstring&) { setBS(BlendState::Transparency); }));
		}

		// Culling-mode dropdown.
		_pbrCullModeDrop = new DropDown(_properties, _properties->GetNextPos(), Point(200, 18), L"Culling Mode");
		{
			auto setCM = [this](CullingMode cm) {
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					n->pbrOutputProperties.cullMode = cm; MarkDirty();
				}
			};
			_pbrCullModeDrop->GetContextMenu()->AddItem(new ContextItem(L"None",       [setCM](const std::wstring&) { setCM(CullingMode::NoCulling); }));
			_pbrCullModeDrop->GetContextMenu()->AddItem(new ContextItem(L"Back Face",  [setCM](const std::wstring&) { setCM(CullingMode::BackFace); }));
			_pbrCullModeDrop->GetContextMenu()->AddItem(new ContextItem(L"Front Face", [setCM](const std::wstring&) { setCM(CullingMode::FrontFace); }));
		}

		// Material-format (texture packing) dropdown.
		_pbrFormatDrop = new DropDown(_properties, _properties->GetNextPos(), Point(200, 18), L"Material Format");
		{
			auto setF = [this](MaterialFormat f) {
				if (auto* n = GetSelectedNode(); n != nullptr && n->nodeType == MaterialGraphNodeType::PbrOutput)
				{
					n->pbrOutputProperties.materialFormat = f; MarkDirty();
				}
			};
			_pbrFormatDrop->GetContextMenu()->AddItem(new ContextItem(L"None", [setF](const std::wstring&) { setF(MaterialFormat::None); }));
			_pbrFormatDrop->GetContextMenu()->AddItem(new ContextItem(L"ORM",  [setF](const std::wstring&) { setF(MaterialFormat::ORM); }));
			_pbrFormatDrop->GetContextMenu()->AddItem(new ContextItem(L"RMA",  [setF](const std::wstring&) { setF(MaterialFormat::RMA); }));
		}

		// Footstep sounds. Unlike the PBR widgets above (which drive the selected
		// PbrOutput node and are applied on compile), footstep data is a pure
		// material-level attribute with no graph node - it bypasses the graph and
		// writes straight to the Material, persisted by Save() via MaterialLoader.
		// Mirrors the controls in MaterialDialog so either editor can author them.
		const auto footstepRowSize = Point(_properties->GetSize().x - 20, 80);

		auto* graphFootstepSearch = new AssetSearch(
			_properties, _properties->GetNextPos(), footstepRowSize,
			L"Footstep Sound", { ResourceType::Audio },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				_material->SetFootstepSoundPath(chosen.string());
				MarkDirty();
			});
		if (const std::string& fsp = _material->GetFootstepSoundPath(); !fsp.empty())
			graphFootstepSearch->SetValue(std::wstring(fsp.begin(), fsp.end()));

		auto* graphSurfaceMapSearch = new AssetSearch(
			_properties, _properties->GetNextPos(), footstepRowSize,
			L"Footstep Surface Map (red = id)", { ResourceType::Image },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				_material->SetFootstepSurfaceMapPath(chosen.string());
				MarkDirty();
			});
		if (const std::string& smp = _material->GetFootstepSurfaceMapPath(); !smp.empty())
			graphSurfaceMapSearch->SetValue(std::wstring(smp.begin(), smp.end()));

		const int kGraphFootstepSurfaceSlots = 8;
		for (int id = 0; id < kGraphFootstepSurfaceSlots; ++id)
		{
			auto* slot = new AssetSearch(
				_properties, _properties->GetNextPos(), footstepRowSize,
				L"Surface " + std::to_wstring(id) + L" Sound", { ResourceType::Audio },
				[this, id](AssetSearch*, const AssetSearchResult& result)
				{
					const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					_material->SetFootstepSurfaceSound(id, chosen.string());
					MarkDirty();
				});
			if (const std::string& s = _material->GetFootstepSurfaceSound(id); !s.empty())
				slot->SetValue(std::wstring(s.begin(), s.end()));
		}

		for (int32_t i = 0; i < 6; ++i)
		{
			_compileMessages[i] = new LineEdit(
				_properties,
				_properties->GetNextPos(),
				Point(_properties->GetSize().x - 20, 20),
				i == 0 ? L"Compile Messages" : L"");
			_compileMessages[i]->SetDoesCallbackWaitForReturn(false);
			// Read-only but VISIBLE - DisableRecursive skipped rendering, so
			// compile errors/warnings written here were never shown to anyone.
			_compileMessages[i]->EnableInput(false);
		}

		RebuildPropertyPanel();
	}

	MaterialGraphDialog::~MaterialGraphDialog()
	{
		if (_material != nullptr)
			_material->DecrementEditorOpenCount();
	}

	void MaterialGraphDialog::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		if (!_embeddedMode)
		{
			Dialog::Render(renderer, w, h);
			return;
		}

		const auto abs = Element::GetAbsolutePosition();
		renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, renderer->_style.win_back);
		renderer->Frame(abs.x, abs.y, _size.x, _size.y, 1, renderer->_style.win_border);
	}

	bool MaterialGraphDialog::OnInputEvent(InputEvent event, InputData* data)
	{
		if (!_embeddedMode)
			return Dialog::OnInputEvent(event, data);

		return Element::OnInputEvent(event, data);
	}

	Point MaterialGraphDialog::GetAbsolutePosition() const
	{
		if (!_embeddedMode)
			return Dialog::GetAbsolutePosition();

		return Element::GetAbsolutePosition();
	}

	void MaterialGraphDialog::EnsureGraphExists()
	{
		if (_material == nullptr)
			return;

		if (!_material->_hasGraph)
		{
			// Graph-only authoring policy: legacy standard materials are
			// promoted to graphs ON OPEN, seeded from their scalars + bound
			// textures so they render identically - and SAVED immediately so
			// the promotion sticks without requiring an explicit Apply.
			// (Instances never reach here - the constructor routes them into
			// instance mode before calling this.)
			_material->_graph = MaterialGraph::CreateFromStandardMaterial(*_material);
			_material->_hasGraph = true;
			_material->InvalidateGiGraphTintCache();
			_material->Save();
		}
		else if (_material->_graph.nodes.empty())
		{
			_material->_graph = MaterialGraph::CreateFromStandardMaterial(*_material);
			_material->InvalidateGiGraphTintCache();
			_isDirty = true;
		}

		_material->_graph.EnsureDefaultOutputBindings();

		// Two output layouts coexist: the legacy "seven separate Output nodes"
		// and the unified PbrOutput node. New graphs get the unified one; old
		// graphs keep their legacy nodes so we don't churn existing files. The
		// decision is per-graph based on what's already in there:
		//   - PbrOutput node present  -> unified layout, skip legacy backfill
		//   - Any legacy output_* node present -> legacy layout, backfill the
		//     missing ones so artists who started with a partial set get a
		//     complete row
		//   - Neither -> fresh graph, insert a PbrOutput
		const bool hasPbrOutput = _material->_graph.FindPbrOutputNode() != nullptr;
		const bool hasAnyLegacyOutputNode =
			_material->_graph.FindNode("output_basecolor")  != nullptr ||
			_material->_graph.FindNode("output_normal")     != nullptr ||
			_material->_graph.FindNode("output_roughness")  != nullptr ||
			_material->_graph.FindNode("output_metallic")   != nullptr ||
			_material->_graph.FindNode("output_emissive")   != nullptr ||
			_material->_graph.FindNode("output_opacity")    != nullptr ||
			_material->_graph.FindNode("output_smoothness") != nullptr;

		if (hasPbrOutput)
		{
			// New layout - the PbrOutput's input-pin connections already drive
			// graph.outputs via EnsureDefaultOutputBindings above. Nothing else
			// to do.
		}
		else if (hasAnyLegacyOutputNode)
		{
			// Legacy layout - backfill any missing output_* nodes so the
			// artist's canvas always has the full row.
			auto ensureOutputNode = [this](const char* id, const char* name, const math::Vector2& pos, MaterialGraphValueType type)
			{
				if (auto* node = _material->_graph.FindNode(id); node == nullptr)
				{
					_material->_graph.nodes.push_back(MakeMaterialOutputNode(id, name, pos, type));
					_isDirty = true;
				}
				else
				{
					node->nodeType = MaterialGraphNodeType::Output;
					node->displayName = name;
					if (node->inputPins.empty())
					{
						node->inputPins.push_back({ "In", "In", type, MaterialGraphPinDirection::Input });
						_isDirty = true;
					}
				}
			};

			ensureOutputNode("output_basecolor",  "BaseColor",  math::Vector2(620.0f, 80.0f),  MaterialGraphValueType::Vector4);
			ensureOutputNode("output_normal",     "Normal",     math::Vector2(620.0f, 160.0f), MaterialGraphValueType::Vector4);
			ensureOutputNode("output_roughness",  "Roughness",  math::Vector2(620.0f, 240.0f), MaterialGraphValueType::Scalar);
			ensureOutputNode("output_metallic",   "Metallic",   math::Vector2(620.0f, 320.0f), MaterialGraphValueType::Scalar);
			ensureOutputNode("output_emissive",   "Emissive",   math::Vector2(620.0f, 400.0f), MaterialGraphValueType::Vector4);
			ensureOutputNode("output_opacity",    "Opacity",    math::Vector2(620.0f, 480.0f), MaterialGraphValueType::Scalar);
			ensureOutputNode("output_smoothness", "Smoothness", math::Vector2(620.0f, 560.0f), MaterialGraphValueType::Scalar);

			// Keep visual output-node links in sync with authoritative output bindings.
			for (const auto& output : _material->_graph.outputs)
			{
				if (output.nodeId.empty() || output.pinId.empty())
					continue;

				const std::string outputNodeId = GetOutputNodeIdForSemantic(output.semantic);
				const bool hasConnection = std::find_if(
					_material->_graph.connections.begin(),
					_material->_graph.connections.end(),
					[&](const MaterialGraphConnection& connection)
					{
						return connection.fromNodeId == output.nodeId &&
							connection.fromPinId == output.pinId &&
							connection.toNodeId == outputNodeId &&
							connection.toPinId == "In";
					}) != _material->_graph.connections.end();

				if (!hasConnection)
				{
					_material->_graph.connections.push_back(
						{ output.nodeId, output.pinId, outputNodeId, "In" });
					_isDirty = true;
				}
			}
		}
		else
		{
			// Fresh graph - drop in a unified PbrOutput node so the canvas
			// has somewhere for wires to terminate.
			_material->_graph.nodes.push_back(MaterialGraph::CreatePbrOutputNode("output_pbr", math::Vector2(620.0f, 300.0f)));
			_isDirty = true;
		}
	}

	void MaterialGraphDialog::MarkDirty()
	{
		_isDirty = true;
		if (_statusLine != nullptr)
		{
			_statusLine->SetValue(L"Dirty (changes not saved)");
		}
	}

	void MaterialGraphDialog::SetStatusText(const std::wstring& text, bool isError)
	{
		_statusIsError = isError;
		if (_statusLine != nullptr)
		{
			_statusLine->SetValue(text);
		}
	}

	MaterialGraphNode* MaterialGraphDialog::GetSelectedNode()
	{
		if (_material == nullptr)
			return nullptr;

		// Instance mode edits the local parent-graph copy (only parameter
		// overrides persist - see WriteInstanceOverrideFromNode).
		if (_instanceMode)
			return _instanceViewGraph.FindNode(_selectedNodeId);

		if (!_material->_hasGraph)
			return nullptr;

		return _material->_graph.FindNode(_selectedNodeId);
	}

	void MaterialGraphDialog::WriteInstanceOverrideFromNode(const MaterialGraphNode& node)
	{
		if (!_instanceMode || _material == nullptr || node.parameterName.empty())
			return;

		const bool isParameterNode =
			node.nodeType == MaterialGraphNodeType::ScalarParameter ||
			node.nodeType == MaterialGraphNodeType::VectorParameter ||
			node.nodeType == MaterialGraphNodeType::TextureParameter;
		if (!isParameterNode)
			return;

		auto& overrides = _material->_graphInstance.overrides;
		auto it = std::find_if(overrides.begin(), overrides.end(),
			[&node](const MaterialGraphParameterOverride& o) { return o.name == node.parameterName; });
		if (it == overrides.end())
		{
			overrides.emplace_back();
			it = std::prev(overrides.end());
			it->name = node.parameterName;
		}

		// Same node-type -> value-type mapping as SyncParameterDefinition.
		switch (node.nodeType)
		{
		case MaterialGraphNodeType::ScalarParameter:
			it->valueType = MaterialGraphValueType::Scalar;
			it->scalarValue = node.scalarValue;
			break;
		case MaterialGraphNodeType::VectorParameter:
			it->valueType = MaterialGraphValueType::Vector4;
			it->vectorValue = node.vectorValue;
			break;
		case MaterialGraphNodeType::TextureParameter:
			it->valueType = MaterialGraphValueType::Texture2D;
			it->texturePath = node.texturePath;
			break;
		default:
			break;
		}
	}

	void MaterialGraphDialog::OnNodeSelectionChanged(const std::string& nodeId)
	{
		_selectedNodeId = nodeId;
		RebuildPropertyPanel();
	}

	void MaterialGraphDialog::RebuildPropertyPanel()
	{
		auto* node = GetSelectedNode();
		if (_selectedNodeLabel != nullptr)
		{
			if (node != nullptr)
				_selectedNodeLabel->SetValue(s2ws(node->displayName.empty() ? node->id : node->displayName));
			else
				_selectedNodeLabel->SetValue(L"<none>");
		}

		const bool hasNode = node != nullptr;
		const bool isScalarNode = hasNode && (node->nodeType == MaterialGraphNodeType::ScalarConstant || node->nodeType == MaterialGraphNodeType::ScalarParameter || node->nodeType == MaterialGraphNodeType::WeatherScalar);
		const bool isVectorNode = hasNode && (node->nodeType == MaterialGraphNodeType::VectorConstant || node->nodeType == MaterialGraphNodeType::VectorParameter || node->nodeType == MaterialGraphNodeType::WeatherVector);
		const bool isTextureNode = hasNode && (node->nodeType == MaterialGraphNodeType::TextureSample || node->nodeType == MaterialGraphNodeType::TextureParameter);
		const bool isParameterNode = hasNode &&
			(node->nodeType == MaterialGraphNodeType::ScalarParameter ||
				node->nodeType == MaterialGraphNodeType::VectorParameter ||
				node->nodeType == MaterialGraphNodeType::TextureParameter ||
				node->nodeType == MaterialGraphNodeType::WeatherScalar ||
				node->nodeType == MaterialGraphNodeType::WeatherVector);

		_parameterName->SetValue(hasNode ? s2ws(node->parameterName) : L"");
		if (isParameterNode) _parameterName->EnableRecursive(); else _parameterName->DisableRecursive();

		if (hasNode) _scalarValue->SetValue(std::format(L"{:.3f}", node->scalarValue));
		if (hasNode) _scalarScratch = node->scalarValue;
		if (isScalarNode) _scalarValue->EnableRecursive(); else _scalarValue->DisableRecursive();

		if (hasNode)
		{
			_vectorValue[0] = node->vectorValue.x;
			_vectorValue[1] = node->vectorValue.y;
			_vectorValue[2] = node->vectorValue.z;
			_vectorValue[3] = node->vectorValue.w;
			for (int32_t i = 0; i < 4; ++i)
				_vectorDrags[i]->SetValue(std::format(L"{:.3f}", _vectorValue[i]));
		}

		for (int32_t i = 0; i < 4; ++i)
		{
			if (isVectorNode) _vectorDrags[i]->EnableRecursive(); else _vectorDrags[i]->DisableRecursive();
		}

		// Always sync the AssetSearch to the selected node's state, including
		// clearing it when the node has no texture. The old code only called
		// SetValue when texturePath was non-empty, which meant the widget kept
		// showing the PREVIOUSLY-selected node's path - users would see a
		// texture in the field, think they'd assigned it, and then the
		// compiler would error with "missing a texture input" because the
		// actual node->texturePath was still empty.
		if (hasNode)
			_texturePath->SetValue(node->texturePath.wstring());
		else
			_texturePath->SetValue(L"");
		if (isTextureNode) _texturePath->EnableRecursive(); else _texturePath->DisableRecursive();

		// PbrOutput widgets: only enabled when a PbrOutput node is selected.
		// Sync local scratch vars to the node's properties so the widgets show
		// the right values on selection change.
		const bool isPbrOutputNode = hasNode && node->nodeType == MaterialGraphNodeType::PbrOutput;
		if (isPbrOutputNode)
		{
			const auto& p = node->pbrOutputProperties;
			_pbrHasTransparency    = (p.hasTransparency    != 0);
			_pbrAffectsGI          = (p.affectsGI          != 0);
			_pbrEmissiveAffectsGI  = (p.emissiveAffectsGI  != 0);
			_pbrReceivesSnow       = (p.receivesSnow       != 0);
			_pbrRainDripIntensity  = p.rainDripIntensity;
			_pbrCullDistance       = p.cullDistance;
			_pbrModelParams[0]     = p.modelParams.x;
			_pbrModelParams[1]     = p.modelParams.y;
			_pbrModelParams[2]     = p.modelParams.z;
			_pbrModelParams[3]     = p.modelParams.w;
			_pbrWindSwayBend    = p.windSwayParams.x;
			_pbrWindSwayFlutter = p.windSwayParams.y;
			_pbrWindSwayHeight  = p.windSwayParams.z;
			_pbrWindSwayMode    = p.windSwayParams.w;
			if (_pbrRainDripDrag)    _pbrRainDripDrag->SetValue(std::format(L"{:.3f}", _pbrRainDripIntensity));
			if (_pbrCullDistanceDrag)_pbrCullDistanceDrag->SetValue(std::format(L"{:.1f}", _pbrCullDistance));
			for (int32_t i = 0; i < 4; ++i)
				if (_pbrModelParamDrags[i]) _pbrModelParamDrags[i]->SetValue(std::format(L"{:.3f}", _pbrModelParams[i]));
			if (_pbrWindSwayModeDrag)    _pbrWindSwayModeDrag->SetValue(std::format(L"{:.0f}", _pbrWindSwayMode));
			if (_pbrWindSwayBendDrag)    _pbrWindSwayBendDrag->SetValue(std::format(L"{:.2f}", _pbrWindSwayBend));
			if (_pbrWindSwayFlutterDrag) _pbrWindSwayFlutterDrag->SetValue(std::format(L"{:.2f}", _pbrWindSwayFlutter));
			if (_pbrWindSwayHeightDrag)  _pbrWindSwayHeightDrag->SetValue(std::format(L"{:.1f}", _pbrWindSwayHeight));

			// Dropdowns: push the node's STORED values into the displayed text.
			// The write path (context-menu callbacks) was always wired, but the
			// read path never was, so all five rendered blank regardless of
			// what the node held - the "properties don't deserialize" report
			// (the JSON round-trips fine; the panel just never showed it).
			// Labels must match the ContextItem strings above.
			if (_pbrShadingModelDrop)
			{
				static const wchar_t* kModelNames[] = {
					L"Standard PBR", L"Subsurface (SSS)", L"Clearcoat",
					L"Anisotropic", L"Sheen / Cloth" };
				const int32_t m = std::clamp(p.materialModel, 0, 4);
				_pbrShadingModelDrop->SetValue(kModelNames[m]);
			}
			if (_pbrDepthStateDrop)
			{
				switch (p.depthState)
				{
				case DepthBufferState::DepthNone:         _pbrDepthStateDrop->SetValue(L"None"); break;
				case DepthBufferState::DepthRead:         _pbrDepthStateDrop->SetValue(L"Read"); break;
				case DepthBufferState::DepthReverseZ:     _pbrDepthStateDrop->SetValue(L"Reverse-Z"); break;
				case DepthBufferState::DepthReadReverseZ: _pbrDepthStateDrop->SetValue(L"Read Reverse-Z"); break;
				case DepthBufferState::DepthDefault:
				default:                                  _pbrDepthStateDrop->SetValue(L"Default"); break;
				}
			}
			if (_pbrBlendStateDrop)
			{
				switch (p.blendState)
				{
				case BlendState::Additive:     _pbrBlendStateDrop->SetValue(L"Additive"); break;
				case BlendState::Subtractive:  _pbrBlendStateDrop->SetValue(L"Subtractive"); break;
				case BlendState::Transparency: _pbrBlendStateDrop->SetValue(L"Transparency"); break;
				case BlendState::Opaque:
				default:                       _pbrBlendStateDrop->SetValue(L"Opaque"); break;
				}
			}
			if (_pbrCullModeDrop)
			{
				switch (p.cullMode)
				{
				case CullingMode::NoCulling:  _pbrCullModeDrop->SetValue(L"None"); break;
				case CullingMode::FrontFace:  _pbrCullModeDrop->SetValue(L"Front Face"); break;
				case CullingMode::BackFace:
				default:                      _pbrCullModeDrop->SetValue(L"Back Face"); break;
				}
			}
			if (_pbrFormatDrop)
			{
				switch (p.materialFormat)
				{
				case MaterialFormat::ORM:  _pbrFormatDrop->SetValue(L"ORM"); break;
				case MaterialFormat::RMA:  _pbrFormatDrop->SetValue(L"RMA"); break;
				case MaterialFormat::None:
				default:                   _pbrFormatDrop->SetValue(L"None"); break;
				}
			}
		}
		// PBR-output widgets stay disabled in instance mode: those properties
		// belong to the parent graph (editing them here would only churn the
		// local view copy and never persist).
		const bool pbrEditable = isPbrOutputNode && !_instanceMode;
		const auto setPbrEnabled = [pbrEditable](Element* e) {
			if (e == nullptr) return;
			if (pbrEditable) e->EnableRecursive(); else e->DisableRecursive();
		};
		setPbrEnabled(_pbrTransparencyToggle);
		setPbrEnabled(_pbrAffectsGiToggle);
		setPbrEnabled(_pbrEmissiveGiToggle);
		setPbrEnabled(_pbrRainDripDrag);
		setPbrEnabled(_pbrWindSwayModeDrag);
		setPbrEnabled(_pbrWindSwayBendDrag);
		setPbrEnabled(_pbrWindSwayFlutterDrag);
		setPbrEnabled(_pbrWindSwayHeightDrag);
		setPbrEnabled(_pbrCullDistanceDrag);
		for (int32_t i = 0; i < 4; ++i)
			setPbrEnabled(_pbrModelParamDrags[i]);
		setPbrEnabled(_pbrShadingModelDrop);
		setPbrEnabled(_pbrDepthStateDrop);
		setPbrEnabled(_pbrBlendStateDrop);
		setPbrEnabled(_pbrCullModeDrop);
		setPbrEnabled(_pbrFormatDrop);
	}

	void MaterialGraphDialog::SyncParameterDefinition(const MaterialGraphNode& node)
	{
		const bool isParameterNode =
			node.nodeType == MaterialGraphNodeType::ScalarParameter ||
			node.nodeType == MaterialGraphNodeType::VectorParameter ||
			node.nodeType == MaterialGraphNodeType::TextureParameter;
		if (!isParameterNode || node.parameterName.empty())
			return;

		MaterialGraphParameter parameter;
		parameter.name = node.parameterName;
		parameter.isExposed = node.isExposedParameter;
		switch (node.nodeType)
		{
		case MaterialGraphNodeType::ScalarParameter:
			parameter.valueType = MaterialGraphValueType::Scalar;
			parameter.scalarValue = node.scalarValue;
			break;
		case MaterialGraphNodeType::VectorParameter:
			parameter.valueType = MaterialGraphValueType::Vector4;
			parameter.vectorValue = node.vectorValue;
			break;
		case MaterialGraphNodeType::TextureParameter:
			parameter.valueType = MaterialGraphValueType::Texture2D;
			parameter.texturePath = node.texturePath;
			break;
		default:
			break;
		}

		auto& params = _material->_graph.parameters;
		const auto it = std::find_if(params.begin(), params.end(),
			[&parameter](const MaterialGraphParameter& p) { return p.name == parameter.name; });
		if (it != params.end())
			*it = parameter;
		else
			params.push_back(std::move(parameter));
	}

	void MaterialGraphDialog::SyncGraphParametersFromNodes()
	{
		if (_material == nullptr || !_material->_hasGraph)
			return;

		_material->_graph.parameters.clear();
		for (const auto& node : _material->_graph.nodes)
			SyncParameterDefinition(node);
	}

	void MaterialGraphDialog::UpdateCompileMessages(const MaterialGraphCompileResult& compileResult)
	{
		size_t writeIndex = 0;
		for (const auto& error : compileResult.errors)
		{
			if (writeIndex >= 6)
				break;
			_compileMessages[writeIndex++]->SetValue(std::format(L"Error: {}", s2ws(error)));
		}
		for (const auto& warning : compileResult.warnings)
		{
			if (writeIndex >= 6)
				break;
			_compileMessages[writeIndex++]->SetValue(std::format(L"Warning: {}", s2ws(warning)));
		}
		for (; writeIndex < 6; ++writeIndex)
		{
			_compileMessages[writeIndex]->SetValue(L"");
		}
	}

	void MaterialGraphDialog::FocusFirstErrorNode(const MaterialGraphCompileResult& compileResult)
	{
		for (const auto& error : compileResult.errors)
		{
			const auto nodeId = TryExtractNodeIdFromMessage(error);
			if (nodeId.empty())
				continue;

			if (_material->_graph.FindNode(nodeId) == nullptr)
				continue;

			_selectedNodeId = nodeId;
			if (auto* canvas = static_cast<MaterialGraphCanvasImpl*>(_canvas); canvas != nullptr)
				canvas->SetSelectedNodeId(nodeId);

			OnNodeSelectionChanged(nodeId);
			break;
		}
	}

	bool MaterialGraphDialog::CompileOnly()
	{
		if (_material == nullptr)
			return false;

		// Instance mode: full recompile of the PARENT graph with this
		// instance's overrides baked in, targeting the INSTANCE material (it
		// gets its own generated shader). Matches what the loader's
		// ApplyInstanceToMaterial now does on load - overrides are baked into
		// the compile there too.
		if (_instanceMode)
		{
			const auto compileResult = MaterialGraphCompiler::CompileToMaterial(
				_instanceViewGraph, *_material, &_material->_graphInstance.overrides);
			UpdateCompileMessages(compileResult);
			if (!compileResult.success)
			{
				std::wstring message = L"Compile failed: ";
				for (size_t i = 0; i < compileResult.errors.size(); ++i)
				{
					if (i > 0) message += L" | ";
					message += s2ws(compileResult.errors[i]);
				}
				SetStatusText(message, true);
				FocusFirstErrorNode(compileResult);
				return false;
			}
			SetStatusText(L"Instance compile succeeded.", false);
			return true;
		}

		if (!_material->_hasGraph)
			return false;

		SyncGraphParametersFromNodes();
		// Re-derive graph.outputs from the PbrOutput node's current pin wiring so
		// the compiler always sees what's on the canvas (connect/disconnect update
		// the bindings too, but this is the authoritative sync before compile and
		// before SaveAndApply serializes the outputs array).
		_material->_graph.EnsureDefaultOutputBindings();
		const auto compileResult = MaterialGraphCompiler::CompileToMaterial(_material->_graph, *_material, nullptr);
		UpdateCompileMessages(compileResult);
		if (!compileResult.success)
		{
			std::wstring message = L"Compile failed: ";
			for (size_t i = 0; i < compileResult.errors.size(); ++i)
			{
				if (i > 0) message += L" | ";
				message += s2ws(compileResult.errors[i]);
			}
			SetStatusText(message, true);
			FocusFirstErrorNode(compileResult);
			return false;
		}

		if (compileResult.warnings.empty())
		{
			SetStatusText(L"Compile succeeded.", false);
		}
		else
		{
			SetStatusText(std::format(L"Compile succeeded ({} warning(s)).", compileResult.warnings.size()), false);
		}
		return true;
	}

	bool MaterialGraphDialog::SaveAndApply()
	{
		if (!CompileOnly())
			return false;

		// An instance must stay an instance on disk: only the override set is
		// authored here; the graph belongs to the parent.
		if (_instanceMode)
		{
			_material->_hasGraph = false;
			_material->_hasGraphInstance = true;
		}
		else
		{
			_material->_hasGraph = true;
		}
		_material->InvalidateGiGraphTintCache();
		_material->Save();
		_isDirty = false;
		SetStatusText(_instanceMode ? L"Instance saved and applied." : L"Saved and applied.", false);
		return true;
	}
}
