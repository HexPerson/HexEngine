

#include "MeshRendererWidget.h"

#include <qfiledialog.h>

#include <HexEngine.Core\Entity\Component\MeshRenderer.hpp>
#include <HexEngine.Core\Environment\IEnvironment.hpp>

MeshRendererWidget::MeshRendererWidget(HexEngine::Entity* entity, QWidget* parent) :
	BaseWidget(parent),
	_entity(entity)
{
	ui.setupUi(this);

	_meshRenderer = entity->GetComponent<HexEngine::MeshRenderer>();

	for (auto i = 0; i < _meshRenderer->GetMeshes().size(); ++i)
	{
		ui.meshIndex->insertItem(i, std::to_string(i).c_str());
	}

	ui.meshEdit->setText(_meshRenderer->GetMesh(0)->GetFullName().c_str());

	connect(ui.meshIndex, &QComboBox::currentIndexChanged, this, &MeshRendererWidget::SelectMesh);

	if(_meshRenderer->GetMesh(0)->GetSplatMap().inUse)
		ui.splatMap->setText(_meshRenderer->GetMesh(0)->GetSplatMap().texture->GetPath().u8string().c_str());

	connect(ui.splatMapBtn, &QPushButton::pressed, this, &MeshRendererWidget::SelectSplatMap);

	//auto accordion = new QAccordion(this);
	
	//accordion->
	resize(400, 160);

	//resize(0, 0);
}

void MeshRendererWidget::SelectMesh(int index)
{
	ui.meshEdit->setText(_meshRenderer->GetMesh(index)->GetFullName().c_str());
}

void MeshRendererWidget::SelectSplatMap()
{
	auto fileName = QFileDialog::getOpenFileName(this, "Open Texture", QDir::currentPath(), tr("Texture File (*.png;*.jpg;*.jpeg;*.bmp)"));

	if (fileName.isEmpty())
		return;

	std::wstring path = (wchar_t*)fileName.constData();

	auto texture = (HexEngine::ITexture2D*)HexEngine::g_pEnv->_resourceSystem->LoadResource(path);

	_meshRenderer->GetMesh(0)->SetSplatMap(HexEngine::SplatMap::Format::RGB, texture);
}

