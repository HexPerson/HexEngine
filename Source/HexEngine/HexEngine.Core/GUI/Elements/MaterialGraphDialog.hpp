#pragma once

#include "Dialog.hpp"
#include "ComponentWidget.hpp"
#include "AssetSearch.hpp"
#include "LineEdit.hpp"
#include "DragFloat.hpp"
#include "Checkbox.hpp"
#include "DropDown.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/MaterialGraphCompiler.hpp"

namespace HexEngine
{
	class HEX_API MaterialGraphDialog : public Dialog
	{
	public:
		MaterialGraphDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, const std::shared_ptr<Material>& material, bool embeddedMode = false);
		virtual ~MaterialGraphDialog() override;
		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		virtual Point GetAbsolutePosition() const override;

		bool SaveAndApply();
		bool CompileOnly();

		void OnNodeSelectionChanged(const std::string& nodeId);
		void MarkDirty();
		void SetStatusText(const std::wstring& text, bool isError);

	private:
		void RebuildPropertyPanel();
		MaterialGraphNode* GetSelectedNode();
		void EnsureGraphExists();
		void SyncParameterDefinition(const MaterialGraphNode& node);
		void SyncGraphParametersFromNodes();
		void UpdateCompileMessages(const MaterialGraphCompileResult& compileResult);
		void FocusFirstErrorNode(const MaterialGraphCompileResult& compileResult);
	private:
		std::shared_ptr<Material> _material;
		Element* _canvas = nullptr;
		ComponentWidget* _properties = nullptr;
		LineEdit* _parameterName = nullptr;
		DragFloat* _scalarValue = nullptr;
		float _scalarScratch = 0.0f;
		float _vectorValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		DragFloat* _vectorDrags[4] = { nullptr, nullptr, nullptr, nullptr };
		AssetSearch* _texturePath = nullptr;
		LineEdit* _selectedNodeLabel = nullptr;

		// PbrOutput per-material widgets. Only enabled when the selected node
		// is a PbrOutput; otherwise hidden. They bind directly to whichever
		// PbrOutput node is currently selected via raw pointers refreshed in
		// RebuildPropertyPanel - so flipping nodes doesn't accidentally edit a
		// stale node.
		bool        _pbrAffectsGI = true;
		bool        _pbrEmissiveAffectsGI = false;
		bool        _pbrHasTransparency = false;
		float       _pbrRainDripIntensity = 0.0f;
		float       _pbrCullDistance = 0.0f;
		float       _pbrModelParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Checkbox*   _pbrAffectsGiToggle = nullptr;
		Checkbox*   _pbrEmissiveGiToggle = nullptr;
		Checkbox*   _pbrTransparencyToggle = nullptr;
		DragFloat*  _pbrRainDripDrag = nullptr;
		DragFloat*  _pbrCullDistanceDrag = nullptr;
		DragFloat*  _pbrModelParamDrags[4] = { nullptr, nullptr, nullptr, nullptr };
		DropDown*   _pbrShadingModelDrop = nullptr;
		DropDown*   _pbrDepthStateDrop = nullptr;
		DropDown*   _pbrBlendStateDrop = nullptr;
		DropDown*   _pbrCullModeDrop = nullptr;
		DropDown*   _pbrFormatDrop = nullptr;
		std::string _selectedNodeId;
		bool _isDirty = false;
		LineEdit* _statusLine = nullptr;
		LineEdit* _compileMessages[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		bool _statusIsError = false;
		bool _embeddedMode = false;
	};
}
