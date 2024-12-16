

#include "FolderExplorer.h"
#include <qapplication.h>
#include <qheaderview.h>

#include "../HexEngineEditor.h"

FolderExplorer::FolderExplorer(QWidget* parent) :
	QTreeView(parent)
{  

    auto appPath = QDir::currentPath();

    _folderModel = new QFileSystemModel();
    
    auto modelIndex = _folderModel->setRootPath(appPath);
    _folderModel->setFilter(QDir::AllEntries);

    auto fileExtensions = HexEngine::g_pEnv->_resourceSystem->GetSupportedFileExtensions();

    QStringList filters;

    for (auto&& extension : fileExtensions)
    {
        std::string ext = "*" + extension;
        filters << ext.c_str();
    }

    _folderModel->setNameFilters(filters);

    setMinimumWidth(150);
    setMinimumHeight(240);
    setMaximumWidth(250);

    setModel(_folderModel);    

    hideColumn(1);
    hideColumn(2);
    hideColumn(3);

    setRootIndex(_folderModel->index(appPath));

    //setColumnWidth(0, 100);
    
    //resizeColumnToContents(0);

   // header()->resizeSections(QHeaderView::ResizeMode::ResizeToContents);

    connect(this, &QTreeView::clicked, this, &FolderExplorer::onClicked);
}

void FolderExplorer::onClicked(const QModelIndex& index)
{
    QString mPath = _folderModel->fileInfo(index).absoluteFilePath();

    //g_pEditor->GetFileExplorer()->setRootIndex(((QFileSystemModel*)g_pEditor->GetFileExplorer()->model())->setRootPath(mPath));

    //g_pEditor->GetFileExplorer()->header()->resizeSections(QHeaderView::ResizeMode::ResizeToContents);
}