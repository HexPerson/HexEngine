

#include "AssetsExplorer.h"
#include <qheaderview.h>
#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\Entity\Component\MeshRenderer.hpp>

AssetsExplorer::AssetsExplorer(QWidget* parent) :
    QTableView(parent)
{
    _folderModel = new QFileSystemModel;

    //_folderModel->item

    

    setModel(_folderModel);

    setMinimumWidth(250);
    setMinimumHeight(240);
    //setMaximumWidth(500);


    this->horizontalHeader()->hide();
    this->verticalHeader()->hide();

    auto appPath = QDir::currentPath();

    auto modelIndex = _folderModel->setRootPath(appPath);
    _folderModel->setFilter(QDir::AllEntries);

    connect(this, &QTableView::clicked, this, &AssetsExplorer::onClicked);
}

void AssetsExplorer::onClicked(const QModelIndex& index)
{
    //QString mPath = _folderModel->fileInfo(index).absoluteFilePath();

    //fs::path path = (wchar_t*)mPath.constData();

    //HexEngine::Scene* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

    //if (auto resource = HexEngine::g_pEnv->_resourceSystem->LoadResource(path); resource != nullptr)
    //{
    //    auto name = std::string(typeid(*resource).name());
    //    if(name == "class HexEngine::Model")
    //    //if (path.extension() == ".obj")
    //    {
    //        HexEngine::Model* model = (HexEngine::Model*)resource;

    //        path = path.stem();

    //        if (scene->GetEntityByName(path.u8string()))
    //        {

    //            path += "_";

    //            fs::path oldPath = path;

    //            path += "1";

    //            int idx = 1;

    //            while (scene->GetEntityByName(path.u8string()))
    //            {
    //                path = oldPath;
    //                path += std::to_string(idx);

    //                idx++;
    //            }
    //        }

    //        HexEngine::Entity* entity = scene->CreateEntity(path.u8string());

    //        HexEngine::MeshRenderer* meshComponent = entity->AddComponent<HexEngine::MeshRenderer>();
    //        meshComponent->SetMeshes(model->GetMeshes());
    //    }
    //}
}