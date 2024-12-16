
#pragma once

#include <QWidget>
#include <qtreeview.h>
#include <qtableview.h>
#include <qfilesystemmodel.h>
#include <qstandarditemmodel.h>

class AssetsExplorer : public QTableView
{
public:
	AssetsExplorer(QWidget* parent);

private:
	void onClicked(const QModelIndex& index);

private:
	QFileSystemModel* _folderModel = nullptr;
};
