#pragma once

#include "Dialog.hpp"
#include "ComponentWidget.hpp"
#include "AssetSearch.hpp"
#include "LineEdit.hpp"
#include "DragFloat.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/MaterialGraphCompiler.hpp"

namespace HexEngine
{
	class MaterialGraphCanvas;

	class HEX_API MaterialGraphDialog : public Dialog
	{
	public:
		MaterialGraphDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, const std::shared_ptr<Material>& material);
		virtual ~MaterialGraphDialog() = default;

		bool SaveAndApply();
		bool CompileOnly();

		void OnNodeSelectionChanged(const std::string& nodeId);
		void MarkDirty();
		void SetStatusText(const std::wstring& text, bool isError);
		void BindSelectedNodeToOutput(MaterialGraphOutputSemantic semantic, const std::string& outputPinId = "Out");

	private:
		void RebuildPropertyPanel();
		MaterialGraphNode* GetSelectedNode();
		void EnsureGraphExists();
		void SyncParameterDefinition(const MaterialGraphNode& node);
		void SyncGraphParametersFromNodes();
	private:
		std::shared_ptr<Material> _material;
		MaterialGraphCanvas* _canvas = nullptr;
		ComponentWidget* _properties = nullptr;
		LineEdit* _parameterName = nullptr;
		DragFloat* _scalarValue = nullptr;
		float _scalarScratch = 0.0f;
		float _vectorValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		DragFloat* _vectorDrags[4] = { nullptr, nullptr, nullptr, nullptr };
		AssetSearch* _texturePath = nullptr;
		LineEdit* _selectedNodeLabel = nullptr;
		std::string _selectedNodeId;
		bool _isDirty = false;
		LineEdit* _statusLine = nullptr;
		bool _statusIsError = false;
	};
}
