

#include "EntityWidget.h"
#include "../HexEngineEditor.h"
#include "Inspector.h"
#include "RigidBodyWidget.h"
#include <HexEngine.Core\Entity\Component\RigidBody.hpp>

#include <qlistview.h>

EntityWidget::EntityWidget(HexEngine::Entity* entity, QWidget* parent) :
	BaseWidget(parent),
	_entity(entity)
{
	ui.setupUi(this);

	ui.lineEdit->setText(entity->GetName().c_str());

	connect(ui.lineEdit, &QLineEdit::textChanged, this, &EntityWidget::NameChanged);

	connect(ui.addComponent, &QPushButton::pressed, this, &EntityWidget::AddComponent);

	resize(0, 0);
}

void EntityWidget::NameChanged(const QString& value)
{
	QString oldName(_entity->GetName().c_str());

	_entity->SetName(value.toUtf8().constData());

	g_pEditor->OnEntityRenamed(_entity, oldName);
}

void EntityWidget::AddComponent()
{
	auto widget = new QWidget(this);
	widget->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);

	QRect rect = ui.addComponent->geometry();
	QPoint bottomLeft = this->mapToGlobal(rect.bottomLeft());
	widget->setGeometry(QRect(bottomLeft, QSize(rect.width(), 200)));

	QStandardItemModel* model = new QStandardItemModel(widget);

	QListView* list = new QListView(widget);
	list->setModel(model);

	model->appendRow(new QStandardItem("RigidBody"));

	connect(list, &QListView::clicked, this, &EntityWidget::OnAddComponent);

	//widget->resize(0, 0);
	widget->show();
}

void EntityWidget::OnAddComponent(const QModelIndex& index)
{
	auto componentName = index.data(Qt::DisplayRole).toString();

	if (componentName == "RigidBody")
	{
		// default to a static body
		auto rb = _entity->AddComponent<HexEngine::RigidBody>(HexEngine::IRigidBody::BodyType::Static);

		gInspector.AppendWidget(new RigidBodyWidget(_entity, nullptr));
	}
}