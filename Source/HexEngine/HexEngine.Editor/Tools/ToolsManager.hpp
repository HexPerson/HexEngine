
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <qwidget.h>
#include <qaction.h>
#include <qtoolbar.h>

#include "FoliageTool.hpp"

class HexEngineEditor;

class ToolsManager : public QObject
{
public:
	void Init(HexEngineEditor* editor, QToolBar* menuBar);

	bool Update(int x, int y);

	bool IsUsingTool();

private slots:
	void SelectFoliageTool();

private:
	ToolBase* _currentTool = nullptr;
	FoliageTool* _foliageTool = nullptr;
};
