#pragma once

#define _MATH_DEFINES_DEFINED

#include <QtWidgets/QMainWindow>
#include "ui_HexEngineEditor.h"

#include <qstandarditemmodel.h>

#include "Explorer\FolderExplorer.h"
#include "Explorer\AssetsExplorer.h"

#include "Tools\ToolsManager.hpp"

#define DISABLE_MEM_TRACKING 1
#include <HexEngine.Core/Scene/IEntityListener.hpp>
#define DISABLE_MEM_TRACKING 0

class HexEngineEditor : public QMainWindow, public HexEngine::IEntityListener
{
    Q_OBJECT

public:
    HexEngineEditor(QWidget *parent = Q_NULLPTR);
    ~HexEngineEditor();

    AssetsExplorer* GetAssetsExplorer();
    Ui::HexEngineEditorClass& GetUi();

    void OnEntityRenamed(HexEngine::Entity* entity, const QString& oldName);

private:
    void CreateHexEngine(QWidget* targetWidget);

    virtual void OnAddEntity(HexEngine::Entity* entity) override;
    virtual void OnRemoveEntity(HexEngine::Entity* entity) override;
    virtual void OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};
    virtual void OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};    

    virtual bool nativeEvent(const QByteArray& eventType, void* message_, qintptr* result) override;
    virtual void closeEvent(QCloseEvent* event) override;

    void CreatePointLight(bool checked);
    void ImportTerrain(bool checked);
    void NewScene(bool checked);
    void SaveScene(bool checked);
    void LoadScene(bool checked);
    void SelectEntityInTree(const QItemSelection& selected, const QItemSelection& deselected);
    void CreateHeightMap();

private slots:
    void onCustomContextMenu(const QPoint& point);
    void DeleteEntity();
    void ShowRendererSettings();

private:
    Ui::HexEngineEditorClass ui;
    QStandardItemModel* _treeModel = nullptr;

    FolderExplorer* _folderExplorer = nullptr;
    AssetsExplorer* _assetsExplorer = nullptr;

    QMenu* _sceneContextMenu = nullptr;
    QAction* _createPrefabAction = nullptr;

public:

    ToolsManager* _toolsManager = nullptr;
};

inline HexEngineEditor* g_pEditor = nullptr;
