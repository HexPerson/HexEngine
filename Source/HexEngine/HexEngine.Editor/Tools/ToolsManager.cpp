
#include "ToolsManager.hpp"
#include <qmenu.h>

void ToolsManager::Init(HexEngineEditor* editor, QToolBar* menuBar)
{
    auto toolsMenu = new QMenu("Tool box");
    toolsMenu->setIcon(QIcon(":/HexEngineEditor/Icons/tool-box.png"));

    // Create the tools
    auto foliageTool = new QAction("Foliage Placer", toolsMenu);
    QObject::connect(foliageTool, &QAction::triggered, this, &ToolsManager::SelectFoliageTool);

    toolsMenu->addAction(foliageTool);


    menuBar->addAction(toolsMenu->menuAction());

    _foliageTool = new FoliageTool;
    _foliageTool->Create();

    // 
}

void ToolsManager::SelectFoliageTool()
{
    _currentTool = _foliageTool;
}

bool ToolsManager::Update(int x, int y)
{
    if (!_currentTool)
        return false;

    _currentTool->Update(x, y);
    return true;
}

bool ToolsManager::IsUsingTool()
{
    return _currentTool != nullptr;
}