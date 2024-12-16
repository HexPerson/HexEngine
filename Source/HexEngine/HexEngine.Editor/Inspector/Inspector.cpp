

#include "Inspector.h"
#include "../HexEngineEditor.h"
#include "TransformWidget.h"
#include "MeshRendererWidget.h"
#include "PointLightWidget.h"
#include "EntityWidget.h"
#include "RigidBodyWidget.h"

#include <HexEngine.Core\Entity\Component\MeshRenderer.hpp>
#include <HexEngine.Core\Entity\Component\RigidBody.hpp>

Inspector::Inspector()
{}

void Inspector::InspectEntity(HexEngine::Entity* entity)
{
	bool a = false;

	QLayoutItem* item;
	while ((item = g_pEditor->GetUi().verticalLayout->layout()->takeAt(0)) != NULL)
	{
		delete item->widget();
		delete item;
	}

	g_pEditor->GetUi().verticalLayout->addWidget(new EntityWidget(entity, nullptr));

	for (auto&& component : entity->GetAllComponents())
	{
		if(component->GetComponentId() == HexEngine::Transform::_GetComponentId())
		{
			g_pEditor->GetUi().verticalLayout->addWidget(new TransformWidget(entity, nullptr));
		}		
		else if (component->GetComponentId() == HexEngine::MeshRenderer::_GetComponentId())
		{
			g_pEditor->GetUi().verticalLayout->addWidget(new MeshRendererWidget(entity, nullptr));
		}
		else if (component->GetComponentId() == HexEngine::PointLight::_GetComponentId())
		{
			g_pEditor->GetUi().verticalLayout->addWidget(new PointLightWidget(entity, nullptr));
		}
		else if (component->GetComponentId() == HexEngine::RigidBody::_GetComponentId())
		{
			g_pEditor->GetUi().verticalLayout->addWidget(new RigidBodyWidget(entity, nullptr));
		}
	}

	g_pEditor->GetUi().verticalLayout->addStretch();

	HexEngine::g_pEnv->_debugGui->ShowGizmo(entity);
}

void Inspector::InspectMaterial(HexEngine::Material* material)
{

}

void Inspector::AppendWidget(QWidget* widget)
{	
	auto count = g_pEditor->GetUi().verticalLayout->layout()->count();

	if (auto item = g_pEditor->GetUi().verticalLayout->layout()->takeAt(count - 1); item != NULL)
	{
		delete item->widget();
		delete item;
	}

	g_pEditor->GetUi().verticalLayout->addWidget(widget);
	g_pEditor->GetUi().verticalLayout->addStretch();
}