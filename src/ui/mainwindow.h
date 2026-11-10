#pragma once

#include <QMainWindow>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "cad/cadfile.h"
#include "cad/palette.h"

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace ui {

class CanvasView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openFileDialog();
    void reloadFile();
    void showAbout();
    void updateCursorPosition(const QPointF& filePosition);
    void updateInspector(const cad::Object* object);
    void showPaletteItem(QTreeWidgetItem* item, int column);

private:
    void createMenus();
    void createDock();
    void createPaletteDock();
    void createStatusBar();
    void updateWindowTitle();
    void readSettings();
    void writeSettings();
    void rebuildRecentMenu();
    void addRecentFile(const QString& path);

    CanvasView* m_view = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_documentLabel = nullptr;
    QPlainTextEdit* m_inspector = nullptr;
    QTreeWidget* m_paletteTree = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_gridAction = nullptr;

    cad::Document m_document;
    cad::Palette m_palette;
    QString m_currentPath;
    QStringList m_recentFiles;
};

} // namespace ui
