#include "HexEngineEditor.h"
#include "ui_NewProject.h"
#include "ui_RendererSettings.h"

#include <qdockwidget.h>
#include <qscreen.h>
#include <qwindow.h>
#include <qframe.h>
#include <qtreewidget.h>
#include <qfilesystemmodel.h>
#include <qfiledialog.h>

#define DISABLE_MEM_TRACKING 1
#include <HexEngine.Core/Environment/Game3DEnvironment.hpp>
#include <HexEngine.Core/Entity/Component/FirstPersonCameraController.hpp>
#include <HexEngine.Core\Entity\Component\PointLight.hpp>
#include <HexEngine.Core\FileSystem\SaveFile.hpp>
#include <HexEngine.Core/Entity/Component/MeshRenderer.hpp>
#include <HexEngine.Core\Entity\Water.hpp>
#include <HexEngine.Core\Scene\TerrainGenerator.hpp>
#define DISABLE_MEM_TRACKING 0

#include "Inspector\Inspector.h"
#include "RenderWidget.h"
#include "RendererSettings.hpp"

using namespace HexEngine;

IEnvironment* HexEngine::g_pEnv = nullptr;

HexEngineEditor::HexEngineEditor(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    ui.sceneTree->setHeaderHidden(true);

    _treeModel = new QStandardItemModel;
    ui.sceneTree->setModel(_treeModel);

    //QDockWidget* leftDockWidget = new QDockWidget("", this);
    //leftDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea);
    //leftDockWidget->setMinimumSize(250, 0);
    //this->addDockWidget(Qt::LeftDockWidgetArea, leftDockWidget);
    ////
    //QDockWidget* rightDockWidget = new QDockWidget(tr("Right Dock Widget"), this);
    //rightDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
    //rightDockWidget->setMinimumSize(250, 0);
    //this->addDockWidget(Qt::RightDockWidgetArea, rightDockWidget);

    //QDockWidget* bottomDock = new QDockWidget(tr("Bottom Dock Widget"), this);
    //bottomDock->setAllowedAreas(Qt::RightDockWidgetArea);
    //bottomDock->setMinimumSize(QGuiApplication::primaryScreen()->geometry().width(), 250);
    //this->addDockWidget(Qt::BottomDockWidgetArea, bottomDock);   

    auto* frame = new RenderWidget(ui.horizontalLayoutWidget);
    frame->setMinimumWidth(1024);
    frame->setMinimumHeight(768);
    frame->show();

    setCentralWidget(frame);

    CreateHexEngine(frame);

    _folderExplorer = new FolderExplorer(nullptr);
    _assetsExplorer = new AssetsExplorer(nullptr);

    ui.horizontalLayout_2->addWidget(_folderExplorer);
    ui.horizontalLayout_2->addWidget(_assetsExplorer);

    connect(ui.actionNew_Project, &QAction::triggered, this, &HexEngineEditor::NewScene);
    connect(ui.actionOpen_Project, &QAction::triggered, this, &HexEngineEditor::LoadScene);
    connect(ui.actionSave, &QAction::triggered, this, &HexEngineEditor::SaveScene);
    connect(ui.actionPoint_Light, &QAction::triggered, this, &HexEngineEditor::CreatePointLight);
    connect(ui.actionHeightmap, &QAction::triggered, this, &HexEngineEditor::CreateHeightMap);
    connect(ui.actionImportTerrain, &QAction::triggered, this, &HexEngineEditor::ImportTerrain);
    connect(ui.sceneTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &HexEngineEditor::SelectEntityInTree);

    connect(ui.actionRenderer_Settings, &QAction::triggered, this, &HexEngineEditor::ShowRendererSettings);

    ui.sceneTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.sceneTree, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(onCustomContextMenu(const QPoint&)));

    _sceneContextMenu = new QMenu(ui.sceneTree);
    auto deleteEntityAction = new QAction("Delete", _sceneContextMenu);
    _createPrefabAction = new QAction("Create Prefab", _sceneContextMenu);
    _sceneContextMenu->addAction(deleteEntityAction);
    _sceneContextMenu->addAction(_createPrefabAction);

    connect(deleteEntityAction, SIGNAL(triggered()), this, SLOT(DeleteEntity()));

    // Tools
    _toolsManager = new ToolsManager();
    _toolsManager->Init(this, ui.mainToolBar);
}

void HexEngineEditor::ShowRendererSettings()
{
    RendererSettings* rendererSettings = new RendererSettings(this);
    
    rendererSettings->show();
}

void HexEngineEditor::NewScene(bool checked)
{
    QDialog* newProject = new QDialog(this);
    Ui_NewProject newProjectUI;
    newProjectUI.setupUi(newProject);

    newProject->exec();

    if (auto scene = g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
    {
        if (_treeModel->hasChildren()) {
            _treeModel->removeRows(0, _treeModel->rowCount());
        }

        g_pEnv->_sceneManager->UnloadScene(g_pEnv->_sceneManager->GetCurrentScene());

        auto newScene = g_pEnv->_sceneManager->CreateEmptyScene(this);

        auto cameraController = newScene->GetMainCamera()->GetEntity()->AddComponent<FirstPersonCameraController>();

        newScene->GetMainCamera()->GetEntity()->SetPosition(math::Vector3(0.0f, 8.0f, 0.0f));
    }
}



void HexEngineEditor::LoadScene(bool checked)
{
    auto fileName = QFileDialog::getOpenFileName(this, "Open Scene", QDir::currentPath(), tr("Scene File (*.hsf)"));

    if (fileName.isEmpty())
        return;

    // Unload the current scene first, if there is one
    //
    if (auto scene = g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
    {
        if (_treeModel->hasChildren()) {
            _treeModel->removeRows(0, _treeModel->rowCount());
        }

        g_pEnv->_sceneManager->UnloadScene(g_pEnv->_sceneManager->GetCurrentScene());
    }

    g_pEnv->_sceneManager->CreateEmptyScene(this);

    std::wstring filePath = (wchar_t*)fileName.constData();

    HexEngine::SaveFile file(filePath, std::ios::in | std::ios::binary);

    if (!file.Load())
    {
        LOG_CRIT("Failed to load scene");
    }

    file.Close();
}

HexEngineEditor::~HexEngineEditor()
{
   
}

void HexEngineEditor::closeEvent(QCloseEvent* event)
{
    g_pEnv->OnRecieveQuitMessage();
    DestroyEnvironment();

    /*QMessageBox::StandardButton resBtn = QMessageBox::question(this, APP_NAME,
        tr("Are you sure?\n"),
        QMessageBox::Cancel | QMessageBox::No | QMessageBox::Yes,
        QMessageBox::Yes);
    if (resBtn != QMessageBox::Yes) {
        event->ignore();
    }
    else {
        event->accept();
    }*/
    event->accept();
}

void HexEngineEditor::CreateHexEngine(QWidget* targetWidget)
{
    Game3DOptions environmentOpts;

    environmentOpts.windowHandle = (HWND)targetWidget->winId();
    environmentOpts.windowWidth = targetWidget->width();
    environmentOpts.windowHeight = targetWidget->height();
    environmentOpts.fontEngine = FontEngine::None;// FreeType;
    environmentOpts.gameExtension = nullptr;
    environmentOpts.mouseMode = dx::Mouse::Mode::MODE_ABSOLUTE;

    if (Game3DEnvironment::Create(environmentOpts) == nullptr)
    {
        DestroyEnvironment();
        return;
    }

    auto scene = g_pEnv->_sceneManager->CreateEmptyScene(this);

    // Add the camera controller so we can move around
    //
    auto cameraController = scene->GetMainCamera()->GetEntity()->AddComponent<FirstPersonCameraController>();

    scene->GetMainCamera()->GetEntity()->SetPosition(math::Vector3(0.0f, 8.0f, 0.0f));

    g_pEnv->_inputSystem->EnableInput(false);
}

void HexEngineEditor::OnAddEntity(HexEngine::Entity* entity)
{
    _treeModel->appendRow(new QStandardItem(entity->GetName().c_str()));
}

void HexEngineEditor::OnEntityRenamed(HexEngine::Entity* entity, const QString& oldName)
{
    for (auto i = 0; i < _treeModel->rowCount(); ++i)
    {
        auto row = _treeModel->item(i);

        auto rowText = row->data(Qt::DisplayRole).toString();

        if (rowText == oldName)
        {
            row->setData(QString(entity->GetName().c_str()), Qt::DisplayRole);
            break;
        }
    }
}

void HexEngineEditor::OnRemoveEntity(HexEngine::Entity* entity)
{
    for (auto i = 0; i < _treeModel->rowCount(); ++i)
    {
        auto row = _treeModel->item(i);       

        auto rowText = row->data(Qt::DisplayRole).toString();        

        if (rowText == entity->GetName().c_str())
        {            
            auto index = row->index();
            _treeModel->removeRow(index.row());
            break;
        }
    }
}

AssetsExplorer* HexEngineEditor::GetAssetsExplorer()
{
    return _assetsExplorer;
}

Ui::HexEngineEditorClass& HexEngineEditor::GetUi()
{
    return ui;
}

bool HexEngineEditor::nativeEvent(const QByteArray& eventType, void* message_, qintptr* result)
{
    MSG* message = (MSG*)message_;

   

    switch (message->message)
    {
		case WM_ACTIVATEAPP:
			dx::Mouse::ProcessMessage(message->message, message->wParam, message->lParam);
			dx::Keyboard::ProcessMessage(message->message, message->wParam, message->lParam);
			break;

        case WM_KEYDOWN:
            if (message->wParam == VK_F9)
            {
                g_pEnv->_shaderLoader->ReloadAllShaders();
            }
            break;
	}

    return false;
}

void HexEngineEditor::CreatePointLight(bool checked)
{
    auto scene = g_pEnv->_sceneManager->GetCurrentScene();

    if (!scene)
        return;

    uint32_t numPointLights = scene->GetNumberOfComponentsOfType(PointLight::_GetComponentId());

    std::string entityName = "PointLight";

    if (numPointLights > 0)
    {
        entityName += "_" + std::to_string(numPointLights);
    }

    auto lightEntity = scene->CreateEntity(entityName);

    if (!lightEntity)
        return;

    auto pointLight = lightEntity->AddComponent<PointLight>();

    pointLight->SetDiffuseColour(math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    pointLight->SetRadius(10.0f);
}

void HexEngineEditor::ImportTerrain(bool checked)
{
    auto scene = g_pEnv->_sceneManager->GetCurrentScene();

    if (!scene)
        return;

    auto fileNames = QFileDialog::getOpenFileNames(this, "Open Terrain Tiles", QDir::currentPath(), tr("Terrain Mesh (*.obj)"));

    if (fileNames.isEmpty())
        return;

    for (auto& file : fileNames)
    {
        std::wstring filePath = (wchar_t*)file.constData();

        auto onResourceLoaded = [filePath, scene](IResource* resource)
        {
            auto terrainModel = (HexEngine::Model*)resource;

            // Parse the file name to get the tile index
            auto pos = filePath.find(L"_x");

            int32_t tileX = -1, tileZ = -1;
            if (pos != std::wstring::npos)
            {
                auto len = filePath.find('_', pos + 2) - (pos + 1);
                auto str = filePath.substr(pos + 2, len - 1);
                tileX = std::stoi(str);
            }

            auto pos2 = filePath.find(L"_y");

            if (pos2 != std::wstring::npos)
            {
                auto len = filePath.find('.', pos2 + 2) - (pos2 + 1);
                auto str = filePath.substr(pos2 + 2, len - 1);
                tileZ = std::stoi(str);
            }

            Entity* tile = scene->CreateEntity("Tile " + std::to_string(tileX) + "x" + std::to_string(tileZ));

            tile->SetScale(math::Vector3(8.0f));

            auto meshRenderer = tile->AddComponent<MeshRenderer>();

            meshRenderer->SetMeshes(terrainModel->GetMeshes());

            auto body = tile->AddComponent<RigidBody>();

            for (auto& mesh : terrainModel->GetMeshes())
            {
                body->AddTriangleMeshCollider(mesh, true);
            }

            // attempt to load the texture for this tile, if there is one
            fs::path fsPath = filePath;

            for (auto& p : fs::directory_iterator(fsPath.parent_path()))
            {
                auto comparator = filePath.substr(pos, filePath.find('.') - pos);

                auto extension = p.path().extension();

                auto stem = p.path().stem();

                auto result = stem.generic_wstring().find(comparator);

                if (/*p.is_regular_file() &&*/ extension == ".png" && result != std::string::npos)
                {
                    auto image = (ITexture2D*)g_pEnv->_resourceSystem->LoadResource(p);

                    if (stem.generic_wstring().find(L"Normal") != std::wstring::npos || stem.generic_wstring().find(L"normal") != std::wstring::npos)
                    {
                        meshRenderer->GetMesh(0)->GetMaterial()->SetTexture(Material::MaterialTexture::Normal, image);
                    }
                    else
                    {
                        meshRenderer->GetMesh(0)->GetMaterial()->SetTexture(Material::MaterialTexture::Diffuse, image);
                    }
                }
            }

            // create the water
            //
            if (true)
            {
                auto tileBB = tile->GetAABB();

                const float tileWidth = tileBB.Extents.x * 2.0f;

                const float waterHeight = 20.f;

                math::Vector3 position = math::Vector3(tileBB.Center.x, waterHeight, tileBB.Center.z);

                //position -= math::Vector3(tileBB.Extents.x * 2.0f, 0.0f, tileBB.Extents.z * 2.0f);

                Entity* water = scene->CreateEntity(
                    "Water " + std::to_string(tileX) + "x" + std::to_string(tileZ),
                    position);

                water->SetLayer(Layer::StaticGeometry | Layer::Water);


                // Set the material properties for the floor
                //
                auto meshComponent = water->AddComponent<MeshRenderer>();


                TerrainGenerationParams terrainParams;
                terrainParams.createInstance = true;
                terrainParams.ident = "Water_";
                terrainParams.position = water->GetPosition();
                terrainParams.resolution = 32;
                terrainParams.uvScale = 3.0f;
                terrainParams.width = tileWidth;

                // create a mesh to hold the terrain vertices etc
                //
                Mesh* mesh2 = CreateTerrain(terrainParams);

                auto material = mesh2->CreateMaterial();

                // calculate the aabb
                //
                std::vector<math::Vector3> points;

                for (auto&& vertex : mesh2->GetVertices())
                {
                    points.push_back((math::Vector3)vertex._position);
                }

                meshComponent->SetMeshes({ mesh2 });

                dx::BoundingBox aabb;
                dx::BoundingBox::CreateFromPoints(aabb, points.size(), points.data(), sizeof(math::Vector3));
                water->SetAABB(aabb);

                dx::BoundingOrientedBox obb;
                dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
                water->SetOBB(obb);

                mesh2->CreateInstance();

                auto material2 = mesh2->GetMaterial();

                material2->SetTexture(Material::MaterialTexture::Diffuse, ITexture2D::GetDefaultTexture());// (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_COLOR.jpg"));
                material2->SetTexture(Material::MaterialTexture::Normal, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_NORM.jpg"));
                //material2->SetTexture(Material::MaterialTexture::Specular, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_SPEC.jpg"));

                meshComponent->SetShader("Shaders/Water.hcs");

                HexEngine::Material::Properties& materialProps = material->_properties;

                auto mesh = meshComponent->GetMesh(0);

                //material.diffuseColour = math::Vector4(RGBA_TO_FLOAT4(22, 53, 86, 190));
                materialProps.shininess = 520.0f;
                materialProps.shininessStrength = 30.0f;
                materialProps.hasTransparency = 1;
                materialProps.isWater = 1;



            }

            SAFE_UNLOAD(terrainModel);
        };

#if 1
        auto resource = g_pEnv->_resourceSystem->LoadResource(filePath);

        onResourceLoaded(resource);

#else
        g_pEnv->_resourceSystem->LoadResourceAsync(filePath, onResourceLoaded);

#endif
    }
}

void HexEngineEditor::CreateHeightMap()
{
    const auto seed = HexEngine::GetRandomInt();
    const int32_t gridSize = 15;
    const int32_t halfGridSize = (gridSize / 2);
    const float width = 512.0f;
    const float totalSize = width * gridSize;

    LOG_DEBUG("Creating a new heightmap terrain with seed %d", seed);

    for (int i = 0; i < gridSize; ++i)
    {
        for (int j = 0; j < gridSize; ++j)
        {
            
            math::Vector3 position((float)(i - halfGridSize) * width, 0.0f, (float)(j - halfGridSize) * width);

            Entity* tile = nullptr;

            bool shouldCreateWaterTile = false;

            {
                HexEngine::HeightMapGenerationParams params;

                params.seed = seed;
                params.resolution = 32;
                params.width = width;
                params.position = position;
                params.heightScale = 3.2f;

                // edge weighting
               // params.edgeHeight = 300.0f;
                //params.edgeWidth = totalSize - width;

                auto heightMap = HexEngine::GenerateHeightMap(params);

                if (heightMap.size() == 0)
                {
                    LOG_CRIT("A heightmap failed to generate correctly");
                    return;
                }

                HexEngine::TerrainGenerationParams terrainParams;
                terrainParams.ident = "Terrain_" + std::to_string(i) + "_" + std::to_string(j);
                terrainParams.resolution = params.resolution;
                terrainParams.position = params.position;
                terrainParams.heightMap = heightMap;
                terrainParams.uvScale = 8.0f;
                terrainParams.width = params.width;
                terrainParams.createInstance = true;

                auto mesh = HexEngine::CreateTerrain(terrainParams);

                if (!mesh)
                {
                    LOG_CRIT("Failed to generate terrain mesh!");
                    return;
                }

                mesh->CreateInstance();
                if (auto material = mesh->CreateMaterial(); material != nullptr)
                {
                    HexEngine::Material::Properties& materialProps = material->_properties;

                    //material.diffuseColour = math::Vector4(RGBA_TO_FLOAT4(22, 53, 86, 190));
                    //material.specularColour 
                    materialProps.shininess = 800.0f;
                    materialProps.shininessStrength = 1.0f;

                    /* material->SetTexture(HexEngine::Material::MaterialTexture::Diffuse, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/faceless/sfjmafua_4K_Albedo.jpg"));
                     material->SetTexture(HexEngine::Material::MaterialTexture::Normal, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/faceless/sfjmafua_4K_Normal.jpg"));
                     material->SetTexture(HexEngine::Material::MaterialTexture::Specular, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/faceless/sfjmafua_4K_Roughness.jpg"));*/

                    material->SetTexture(HexEngine::Material::MaterialTexture::Diffuse, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Grass_001_COLOR.jpg"));
                    material->SetTexture(HexEngine::Material::MaterialTexture::Normal, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Grass_001_NORM.jpg"));
                    material->SetTexture(HexEngine::Material::MaterialTexture::Specular, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Grass_001_ROUGH.jpg"));
                }

                tile = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(terrainParams.ident, position);

                // tile->SetScale(math::Vector3(8.0f));

                auto meshRenderer = tile->AddComponent<MeshRenderer>();

                meshRenderer->SetMeshes({ mesh });

                // calculate the aabb
                //
                std::vector<math::Vector3> points;

                for (auto&& vertex : mesh->GetVertices())
                {
                    points.push_back((math::Vector3)vertex._position);

                    if (vertex._position.y <= 0.0f)
                        shouldCreateWaterTile = true;
                }

                dx::BoundingBox aabb;
                dx::BoundingBox::CreateFromPoints(aabb, points.size(), points.data(), sizeof(math::Vector3));
                tile->SetAABB(aabb);

                dx::BoundingOrientedBox obb;
                dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
                tile->SetOBB(obb);

                // add a collider
                if (auto body = tile->AddComponent<RigidBody>(); body != nullptr)
                {
                    body->AddTriangleMeshCollider(mesh, true);
                }
            }

            // create the water
            //
            if (shouldCreateWaterTile)
            {
                auto tileBB = tile->GetAABB();

                const float tileWidth = tileBB.Extents.x * 2.0f;

                const float waterHeight = 0.f;

               // math::Vector3 waterPosition = math::Vector3(tileBB.Center.x, waterHeight, tileBB.Center.z);

                //position -= math::Vector3(tileBB.Extents.x * 2.0f, 0.0f, tileBB.Extents.z * 2.0f);

                Entity* water = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(
                    "Water_" + std::to_string(i) + "_" + std::to_string(j),
                    position);

                water->SetLayer(Layer::StaticGeometry | Layer::Water);


                // Set the material properties for the floor
                //
                auto meshComponent = water->AddComponent<MeshRenderer>();


                TerrainGenerationParams terrainParams;
                terrainParams.createInstance = true;
                terrainParams.ident = "Water_";
                terrainParams.position = water->GetPosition();
                terrainParams.resolution = 16;
                terrainParams.uvScale = 8.0f;
                terrainParams.width = tileWidth;

                // create a mesh to hold the terrain vertices etc
                //
                Mesh* mesh2 = CreateTerrain(terrainParams);

                mesh2->CreateMaterial();

                // calculate the aabb
                //
                std::vector<math::Vector3> points;

                for (auto&& vertex : mesh2->GetVertices())
                {
                    points.push_back((math::Vector3)vertex._position);
                }

                meshComponent->SetMeshes({ mesh2 });

                dx::BoundingBox aabb;
                dx::BoundingBox::CreateFromPoints(aabb, points.size(), points.data(), sizeof(math::Vector3));
                water->SetAABB(aabb);

                dx::BoundingOrientedBox obb;
                dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
                water->SetOBB(obb);

                mesh2->CreateInstance();

                auto material2 = mesh2->GetMaterial();

                material2->SetTexture(Material::MaterialTexture::Diffuse, ITexture2D::GetDefaultTexture());// (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_COLOR.jpg"));
                material2->SetTexture(Material::MaterialTexture::Normal, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_NORM.jpg"));
                //material2->SetTexture(Material::MaterialTexture::Specular, (ITexture2D*)g_pEnv->_resourceSystem->LoadResource("Textures/Water_001_SPEC.jpg"));

                meshComponent->SetShader("Shaders/Water.hcs");

                HexEngine::Material::Properties& waterPropsmaterial = material2->_properties;

                //auto mesh = meshComponent->GetMesh(0);

                //material.diffuseColour = math::Vector4(RGBA_TO_FLOAT4(22, 53, 86, 190));
                waterPropsmaterial.shininess = 200.0f;
                waterPropsmaterial.shininessStrength = 5.0f;
                waterPropsmaterial.hasTransparency = 1;
                waterPropsmaterial.isWater = 1;
            }
        }
    }
}

void HexEngineEditor::SelectEntityInTree(const QItemSelection& selected, const QItemSelection& deselected)
{
    //auto index = selected.at(0).indexes().at(0);

    auto nameInScene = ui.sceneTree->selectionModel()->currentIndex().data(Qt::DisplayRole).toString();

    std::string stdName = nameInScene.toLocal8Bit().constData();

    auto entity = g_pEnv->_sceneManager->GetCurrentScene()->GetEntityByName(stdName);

    if (!entity)
        return;

    gInspector.InspectEntity(entity);
}

void HexEngineEditor::onCustomContextMenu(const QPoint& point)
{
    auto selectedIndexes = ui.sceneTree->selectionModel()->selectedIndexes();

    if (selectedIndexes.size() > 1)
    {
        _createPrefabAction->setDisabled(false);
    }
    else
    {
        _createPrefabAction->setDisabled(true);
    }

    QModelIndex index = ui.sceneTree->indexAt(point);

    if (index.isValid())
    {
        _sceneContextMenu->exec(ui.sceneTree->viewport()->mapToGlobal(point));
    }
}

void HexEngineEditor::DeleteEntity()
{
    auto nameInScene = ui.sceneTree->selectionModel()->currentIndex().data(Qt::DisplayRole).toString();

    std::string stdName = nameInScene.toLocal8Bit().constData();

    auto entity = g_pEnv->_sceneManager->GetCurrentScene()->GetEntityByName(stdName);

    if (!entity)
        return;

    entity->DeleteMe();
}

void HexEngineEditor::SaveScene(bool checked)
{
    auto fileName = QFileDialog::getSaveFileName(this, "Save Scene", QDir::currentPath(), tr("Scene File (*.hsf)"));

    std::wstring filePath = (wchar_t*)fileName.constData();

    HexEngine::SaveFile file(filePath, std::ios::out | std::ios::binary);

    if (!file.Save())
    {
        bool a = false;
    }

    file.Close();
}