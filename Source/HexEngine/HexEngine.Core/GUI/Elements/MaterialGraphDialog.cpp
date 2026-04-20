#include "MaterialGraphDialog.hpp"

#include "Button.hpp"
#include "ContextMenu.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../GUI/UIManager.hpp"

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
					PinHit pinHit;
					if (TryHitPin(data->MouseDown.xpos, data->MouseDown.ypos, pinHit) &&
						pinHit.direction == MaterialGraphPinDirection::Output)
					{
						OpenBindOutputMenu(pinHit, Point(data->MouseDown.xpos, data->MouseDown.ypos));
						return true;
					}

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

				_owner->MarkDirty();
			}

			void DeleteSelectedNode()
			{
				const auto selected = _selectedNodeId;
				if (selected.empty())
					return;

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
					return { { "Out", "Out", MaterialGraphValueType::Scalar, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::VectorConstant:
				case MaterialGraphNodeType::VectorParameter:
				case MaterialGraphNodeType::Add:
				case MaterialGraphNodeType::Multiply:
				case MaterialGraphNodeType::Lerp:
				case MaterialGraphNodeType::OneMinus:
				case MaterialGraphNodeType::NormalMap:
					return { { "Out", "Out", MaterialGraphValueType::Vector4, MaterialGraphPinDirection::Output } };
				case MaterialGraphNodeType::TextureSample:
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
				node.id = std::format("node_{}_{}", (int32_t)type, _nodeIdCounter++);
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
			}

			void OpenBindOutputMenu(const PinHit& outputPin, const Point& mousePos)
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
				_contextMenu->AddItem(new ContextItem(L"Bind As BaseColor", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::BaseColor, outputPin.pinId); }));
				_contextMenu->AddItem(new ContextItem(L"Bind As Normal", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Normal, outputPin.pinId); }));
				_contextMenu->AddItem(new ContextItem(L"Bind As Roughness", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Roughness, outputPin.pinId); }));
				_contextMenu->AddItem(new ContextItem(L"Bind As Metallic", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Metallic, outputPin.pinId); }));
				_contextMenu->AddItem(new ContextItem(L"Bind As Emissive", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Emissive, outputPin.pinId); }));
				_contextMenu->AddItem(new ContextItem(L"Bind As Opacity", [this, outputPin](const std::wstring&) { _owner->BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Opacity, outputPin.pinId); }));

				_selectedNodeId = outputPin.nodeId;
				_owner->OnNodeSelectionChanged(_selectedNodeId);
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
		EnsureGraphExists();

		const int32_t topOffset = _embeddedMode ? 8 : 36;
		const int32_t graphTop = _embeddedMode ? 34 : 62;

		_statusLine = new LineEdit(this, Point(10, topOffset), Point(size.x - 220, 20), L"Status");
		_statusLine->SetDoesCallbackWaitForReturn(false);
		_statusLine->SetValue(L"Ready");
		_statusLine->DisableRecursive();

		new Button(this, Point(size.x - 200, topOffset - 2), Point(90, 24), L"Compile", [this](Button*) { return CompileOnly(); });
		new Button(this, Point(size.x - 104, topOffset - 2), Point(90, 24), L"Apply", [this](Button*) { return SaveAndApply(); });

		_canvas = new MaterialGraphCanvasImpl(this, Point(10, graphTop), Point((size.x * 70) / 100 - 20, size.y - graphTop - 10), this, &_material->_graph);

		_properties = new ComponentWidget(this, Point((size.x * 70) / 100 + 10, graphTop), Point(size.x - ((size.x * 70) / 100) - 20, size.y - graphTop - 10), L"Node Properties");

		_selectedNodeLabel = new LineEdit(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20), L"Selected Node");
		_selectedNodeLabel->SetDoesCallbackWaitForReturn(false);
		_selectedNodeLabel->DisableRecursive();

		_parameterName = new LineEdit(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 20), L"Parameter Name");
		_parameterName->SetOnInputFn([this](LineEdit*, const std::wstring& value)
		{
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
					MarkDirty();
				}
			});

		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As BaseColor", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::BaseColor); return true; });
		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As Normal", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Normal); return true; });
		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As Roughness", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Roughness); return true; });
		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As Metallic", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Metallic); return true; });
		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As Emissive", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Emissive); return true; });
		new Button(_properties, _properties->GetNextPos(), Point(_properties->GetSize().x - 20, 22), L"Bind As Opacity", [this](Button*) { BindSelectedNodeToOutput(MaterialGraphOutputSemantic::Opacity); return true; });

		RebuildPropertyPanel();
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
			_material->_graph = MaterialGraph::CreateDefaultPbrGraph();
			_material->_hasGraph = true;
			_isDirty = true;
		}
		else if (_material->_graph.nodes.empty())
		{
			_material->_graph = MaterialGraph::CreateDefaultPbrGraph();
			_isDirty = true;
		}

		_material->_graph.EnsureDefaultOutputBindings();
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
		if (_material == nullptr || !_material->_hasGraph)
			return nullptr;

		return _material->_graph.FindNode(_selectedNodeId);
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
		const bool isScalarNode = hasNode && (node->nodeType == MaterialGraphNodeType::ScalarConstant || node->nodeType == MaterialGraphNodeType::ScalarParameter);
		const bool isVectorNode = hasNode && (node->nodeType == MaterialGraphNodeType::VectorConstant || node->nodeType == MaterialGraphNodeType::VectorParameter);
		const bool isTextureNode = hasNode && (node->nodeType == MaterialGraphNodeType::TextureSample || node->nodeType == MaterialGraphNodeType::TextureParameter);
		const bool isParameterNode = hasNode && (node->nodeType == MaterialGraphNodeType::ScalarParameter || node->nodeType == MaterialGraphNodeType::VectorParameter || node->nodeType == MaterialGraphNodeType::TextureParameter);

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

		if (hasNode && !node->texturePath.empty())
			_texturePath->SetValue(node->texturePath.wstring());
		if (isTextureNode) _texturePath->EnableRecursive(); else _texturePath->DisableRecursive();
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

	void MaterialGraphDialog::BindSelectedNodeToOutput(MaterialGraphOutputSemantic semantic, const std::string& outputPinId)
	{
		auto* node = GetSelectedNode();
		if (node == nullptr)
			return;

		const auto pinId = outputPinId.empty() ? std::string("Out") : outputPinId;
		const auto* outputPin = _material->_graph.FindPin(node->id, pinId, MaterialGraphPinDirection::Output);
		if (outputPin == nullptr)
			return;

		_material->_graph.EnsureDefaultOutputBindings();
		for (auto& output : _material->_graph.outputs)
		{
			if (output.semantic == semantic)
			{
				output.nodeId = node->id;
				output.pinId = pinId;
				MarkDirty();
				RebuildPropertyPanel();
				return;
			}
		}
	}

	bool MaterialGraphDialog::CompileOnly()
	{
		if (_material == nullptr || !_material->_hasGraph)
			return false;

		SyncGraphParametersFromNodes();
		const auto compileResult = MaterialGraphCompiler::CompileToMaterial(_material->_graph, *_material, nullptr);
		if (!compileResult.success)
		{
			std::wstring message = L"Compile failed: ";
			for (size_t i = 0; i < compileResult.errors.size(); ++i)
			{
				if (i > 0) message += L" | ";
				message += s2ws(compileResult.errors[i]);
			}
			SetStatusText(message, true);
			return false;
		}

		SetStatusText(L"Compile succeeded.", false);
		return true;
	}

	bool MaterialGraphDialog::SaveAndApply()
	{
		if (!CompileOnly())
			return false;

		_material->_hasGraph = true;
		_material->Save();
		_isDirty = false;
		SetStatusText(L"Saved and applied.", false);
		return true;
	}
}
