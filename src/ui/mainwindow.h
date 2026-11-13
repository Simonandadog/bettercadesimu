#pragma once

#include <QMainWindow>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "cad/palette.h"
#include "cad/project.h"

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
    void newFile();
    void openFileDialog();
    void saveFile();
    void saveFileAs();
    void exportCad();
    void reloadFile();
    void deleteSelection();
    void selectAll();
    void editProperties();
    void showAbout();
    void updateCursorPosition(const QPointF& filePosition);
    void updateInspector(const cad::Object* object);
    void showPaletteItem(QTreeWidgetItem* item, int column);
    void placePaletteItem(QTreeWidgetItem* item, int column);
    void documentChanged();

private:
    void createMenus();
    void createDock();
    void createPaletteDock();
    void createStatusBar();
    void updateWindowTitle();
    void updateDocumentLabel();
    void readSettings();
    void writeSettings();
    void rebuildRecentMenu();
    void addRecentFile(const QString& path);

    bool maybeSave();
    bool loadFile(const QString& path);
    bool saveTo(const QString& path);

    CanvasView* m_view = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_documentLabel = nullptr;
    QPlainTextEdit* m_inspector = nullptr;
    QTreeWidget* m_paletteTree = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_gridAction = nullptr;

    cad::Project m_project;
    cad::Palette m_palette;
    QString m_currentPath;
    QStringList m_recentFiles;
};

} // namespace ui
