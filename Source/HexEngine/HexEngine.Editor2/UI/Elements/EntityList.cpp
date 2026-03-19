
#include "EntityList.hpp"
#include "../EditorUI.hpp"
#include "../../Editor.hpp"
#include <HexEngine.Core\FileSystem\ResourceSystem.hpp>
#include <HexEngine.Core\Entity\Entity.hpp>
#include <HexEngine.Core\Environment\IEnvironment.hpp>
#include <HexEngine.Core\Scene\SceneManager.hpp>

namespace HexEditor
{
	const int32_t ToolbarHeight = 32;
	const int32_t IconSize = ToolbarHeight - 2;

	EntityList::EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		HexEngine::EntityList(parent, position, size)
	{
		_onEntityClicked = std::bind(&Inspector::InspectEntity, g_pUIManager->GetInspector(), std::placeholders::_2);
		_onEntityParented = std::bind(&EntityList::SetEntityParent, this, std::placeholders::_2, std::placeholders::_3);

		_newFolderImg = HexEngine::ITexture2D::Create("EngineData.Textures/UI/folder_new.png");

		/*_entitySearch = new HexEngine::LineEdit(this, HexEngine::Point(0, 0), HexEngine::Point(size.x - (IconSize + 10), IconSize - 5), L"");
		_entitySearch->SetIcon(HexEngine::ITexture2D::Create("EngineData.Textures/UI/magnifying_glass.png"), math::Color(HEX_RGBA_TO_FLOAT4(140, 140, 140, 255)));
		_entitySearch->SetDoesCallbackWaitForReturn(false);*/
		//_entitySearch->SetOnInputFn()
	}

	EntityList::~EntityList()
	{
	}

	void EntityList::SetEntityParent(HexEngine::Entity* sourceEnt, HexEngine::Entity* targetEnt)
	{
		if (sourceEnt == targetEnt)
			return;

		if (sourceEnt && targetEnt)
		{
			sourceEnt->SetParent(targetEnt);
		}

	}

	void EntityList::SaveAsPrefab(HexEngine::Entity* entity, HexEngine::FileSystem* fs)
	{
		HexEngine::EntityList::SaveAsPrefab(entity, g_pEditor->_projectFS);
	}

	void EntityList::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//auto dl = renderer->PushDrawList();

		auto pos = GetAbsolutePosition();
		auto origPos = GetPosition();
		auto origSize = GetSize();
		

		//renderer->FillTexturedQuad(_newFolderImg.get(), pos.x + origSize.x - ToolbarHeight, pos.y, IconSize, IconSize, math::Color(1, 1, 1, 1));

		//SetPosition(HexEngine::Point(origPos.x, origPos.y + ToolbarHeight));
		//SetSize(HexEngine::Point(origSize.x, origSize.y - ToolbarHeight));

		HexEngine::EntityList::Render(renderer, w, h);

		//SetPosition(HexEngine::Point(origPos.x, origPos.y));
		//SetSize(HexEngine::Point(origSize.x, origSize.y));

		//renderer->ListDraw(dl);
		//renderer->PopDrawList();
	}

	/*void EntityList::DuplicateEntity(HexEngine::Entity* entity)
	{
		g_pEnv->_sceneManager->GetCurrentScene()->CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale());
		_ctx->DeleteMe();
		_ctx = nullptr;
	}*/
}