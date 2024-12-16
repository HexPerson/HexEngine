#define _MATH_DEFINES_DEFINED

#include "HexEngineEditor.h"
#include <QtWidgets/QApplication>
#include <qfile.h>

//#pragma comment(lib, "delayimp")

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QFile styleSheet("./Combinear.qss");
    styleSheet.open(QFile::ReadOnly);

    QString style = QLatin1String(styleSheet.readAll());

    a.setStyleSheet(style);

    g_pEditor = new HexEngineEditor;
    g_pEditor->showMaximized();
    return a.exec();
}
