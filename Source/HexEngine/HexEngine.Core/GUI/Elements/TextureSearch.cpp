
#include "TextureSearch.hpp"
#include "Dialog.hpp"
#include "MessageBox.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	const int32_t TextureSearchBarSize = 20;

	TextureSearch::TextureSearch(
		Element* parent,
		const Point& position,
		const Point& size,
		const std::wstring& label,
		const std::shared_ptr<ITexture2D>& texture,
		std::shared_ptr<Material> material,
		MaterialTexture type) :
		Element(parent, position, size),
		_texture(texture),
		_material(material),
		_type(type)
	{
		_edit = new LineEdit(this, Point(0, 0), Point(size.x, TextureSearchBarSize), label);
		_edit->SetIcon(ITexture2D::Create("EngineData.Textures/UI/magnifying_glass.png"), math::Color(HEX_RGBA_TO_FLOAT4(140, 140, 140, 255)));
		_edit->SetOnDragAndDropFn(std::bind(&TextureSearch::DragAndDropTexture, this, std::placeholders::_2));
		

		if (_texture)
		{
			_edit->SetValue(_texture->GetAbsolutePath().filename());
		}
	}

	TextureSearch::~TextureSearch()
	{
	}

	void TextureSearch::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Element::Render(renderer, w, h);		

		const auto pos = _edit->GetAbsolutePosition();

		renderer->FillQuad(
			pos.x + _edit->GetLabelMinSize() + TextureSearchBarSize,
			pos.y + 25,
			_size.y - 25,
			_size.y - 25,
			renderer->_style.lineedit_back);

		if (_texture)
		{
			renderer->FillTexturedQuad(
				_texture.get(),
				pos.x + _edit->GetLabelMinSize() + 25,
				pos.y + 30,
				_size.y - 35,
				_size.y - 35,
				math::Color(1,1,1,1));
		}

		
		

	}

	bool TextureSearch::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			const auto pos = _edit->GetAbsolutePosition();

			if (_edit->IsMouseOver(
				pos.x + _size.x - (TextureSearchBarSize + 4),
				pos.y + (g_pEnv->GetUIManager().GetRenderer()->_style.win_title_height / 2) - (TextureSearchBarSize / 2) - 1,
				TextureSearchBarSize-4, TextureSearchBarSize-4
			))
			{
				_material->SetTexture(_type, nullptr);
				_texture.reset();
				return true;
			}
		}
		return Element::OnInputEvent(event, data);
	}

	void TextureSearch::ConvertRMA()
	{

	}
	void TextureSearch::DragAndDropTexture(const fs::path& path)
	{
		/*if (path.filename().string().find("RMA") != std::string::npos)
		{
			MessageBox::Info(
				L"RMA texture detected",
				L"We believe this texture is an RMA (roughness, metallic, ambient occlusion) texture.\nWould you like us to import it as such?",
				std::bind(&TextureSearch::ConvertRMA, this));

			return;

		}*/
		auto newTexture = ITexture2D::Create(path);

		if (newTexture)
		{
			_material->SetTexture(_type, newTexture);

			_texture = newTexture;

			_edit->SetValue(path.filename());
		}
	}

	int32_t TextureSearch::GetLabelWidth() const
	{
		return _edit->GetLabelWidth();
	}

	void TextureSearch::SetLabelMinSize(int32_t minSize)
	{
		_edit->SetLabelMinSize(minSize);
	}

	void TextureSearch::PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto pos = _edit->GetAbsolutePosition();

		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		renderer->FillQuad(mx, my, 4, 4, math::Color(1, 0, 0, 1));

		renderer->FillTexturedQuad(
			renderer->_style.img_win_close.get(),
			pos.x + _size.x - (TextureSearchBarSize + 4),
			pos.y + (renderer->_style.win_title_height / 2) - (TextureSearchBarSize / 2) - 1,
			TextureSearchBarSize-4, TextureSearchBarSize-4, math::Color(HEX_RGB_TO_FLOAT3(240, 20, 20), 1));
	}
}