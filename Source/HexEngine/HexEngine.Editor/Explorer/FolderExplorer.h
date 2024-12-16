
#pragma once

#include <QWidget>
#include <qtreeview.h>
#include <qfilesystemmodel.h>

class FolderExplorer : public QTreeView
{
public:
	FolderExplorer(QWidget* parent);

private:
	void onClicked(const QModelIndex& index);

private:
	QFileSystemModel* _folderModel = nullptr;
};
