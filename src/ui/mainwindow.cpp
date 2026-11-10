#include "ui/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTreeWidget>
#include <QUrl>
#include <QtMath>

#include "ui/canvasview.h"
#include "ui/sheet.h"

namespace ui {
namespace {

QString fileFilter()
{
    return QObject::tr("bettercadesimu schematics (*.bcs);;CADe SIMU files (*.cad);;All files (*)");
}

// Wire and drawing primitives need dedicated drawing tools; they cannot be
// placed as standalone symbols.
bool isPlaceable(int type)
{
    if (type <= 8)
        return false;
    if (type >= 4000 && type <= 4019)
        return false;
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);

    m_view = new CanvasView(this);
    setCentralWidget(m_view);

    m_palette = cad::Palette::loadDefault();

    createMenus();
    createDock();
    createPaletteDock();
    createStatusBar();

    connect(m_view, &CanvasView::cursorMoved, this, &MainWindow::updateCursorPosition);
    connect(m_view, &CanvasView::selectionChanged, this, &MainWindow::updateInspector);
    connect(m_view, &CanvasView::documentEdited, this, &MainWindow::markModified);

    readSettings();
    updateWindowTitle();
    updateDocumentLabel();

    resize(1280, 860);
    statusBar()->showMessage(tr("Open a .cad file, or drop one onto the window."), 6000);
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* newAction = fileMenu->addAction(tr("&New"));
    newAction->setShortcut(QKeySequence::New);
    newAction->setStatusTip(tr("Start an empty schematic"));
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);

    QAction* openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip(tr("Open a schematic or a CADe SIMU document"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);

    QAction* saveAction = fileMenu->addAction(tr("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip(tr("Save the schematic"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFile);

    QAction* saveAsAction = fileMenu->addAction(tr("Save &As…"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setStatusTip(tr("Save the schematic under a new name"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveFileAs);

    QAction* reloadAction = fileMenu->addAction(tr("&Reload"));
    reloadAction->setShortcut(QKeySequence::Refresh);
    reloadAction->setStatusTip(tr("Reload the current document from disk"));
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadFile);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(tr("E&xit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    QAction* deleteAction = editMenu->addAction(tr("&Delete"));
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setStatusTip(tr("Delete the selected objects"));
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelection);

    QAction* selectAllAction = editMenu->addAction(tr("Select &All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, &MainWindow::selectAll);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    QAction* zoomInAction = viewMenu->addAction(tr("Zoom &In"));
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, m_view, &CanvasView::zoomIn);

    QAction* zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, m_view, &CanvasView::zoomOut);

    QAction* zoomAllAction = viewMenu->addAction(tr("Zoom to &Fit"));
    zoomAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomAllAction, &QAction::triggered, m_view, &CanvasView::zoomAll);

    viewMenu->addSeparator();

    m_gridAction = viewMenu->addAction(tr("Show &Grid"));
    m_gridAction->setCheckable(true);
    m_gridAction->setChecked(true);
    m_gridAction->setShortcut(QKeySequence(Qt::Key_G));
    connect(m_gridAction, &QAction::toggled, m_view, &CanvasView::setGridVisible);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About bettercadesimu"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createDock()
{
    auto* dock = new QDockWidget(tr("Inspector"), this);
    dock->setObjectName(QStringLiteral("inspectorDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_inspector = new QPlainTextEdit(dock);
    m_inspector->setReadOnly(true);
    m_inspector->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_inspector->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_inspector->setMinimumWidth(320);
    m_inspector->setPlaceholderText(tr("Select an object to inspect it."));
    m_inspector->setPlainText(tr("Select an object to inspect it."));

    dock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::createPaletteDock()
{
    auto* dock = new QDockWidget(tr("Symbols"), this);
    dock->setObjectName(QStringLiteral("paletteDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_paletteTree = new QTreeWidget(dock);
    m_paletteTree->setHeaderHidden(true);
    m_paletteTree->setIconSize(QSize(44, 44));
    m_paletteTree->setUniformRowHeights(true);
    m_paletteTree->setIndentation(14);
    m_paletteTree->setMinimumWidth(250);

    for (const cad::PaletteSection& section : m_palette.sections) {
        auto* sectionItem = new QTreeWidgetItem(m_paletteTree);
        sectionItem->setText(0, QStringLiteral("%1. %2").arg(section.number).arg(section.title));
        QFont font = sectionItem->font(0);
        font.setBold(true);
        sectionItem->setFont(0, font);
        sectionItem->setFirstColumnSpanned(true);

        for (const cad::PaletteItem& entry : section.items) {
            auto* item = new QTreeWidgetItem(sectionItem);
            item->setText(0, entry.displayName());
            item->setIcon(0, QIcon(symbolPreview(entry, QSize(44, 44))));
            item->setData(0, Qt::UserRole, entry.type);

            QStringList tooltip;
            tooltip << entry.displayName() << tr("type %1").arg(entry.type);
            if (!entry.pins.isEmpty())
                tooltip << tr("pins %1").arg(entry.pins.join(QLatin1Char(',')));
            tooltip << tr("%1 x %2 units").arg(entry.box.width()).arg(entry.box.height());
            item->setToolTip(0, tooltip.join(QLatin1Char('\n')));
        }
    }

    m_paletteTree->expandToDepth(0);
    connect(m_paletteTree, &QTreeWidget::itemClicked, this, &MainWindow::showPaletteItem);
    connect(m_paletteTree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::placePaletteItem);

    dock->setWidget(m_paletteTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::showPaletteItem(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item || item->data(0, Qt::UserRole).isNull()) {
        m_view->cancelInsert();
        return;
    }

    const int type = item->data(0, Qt::UserRole).toInt();
    const cad::PaletteItem* entry = m_palette.find(type);
    if (!entry)
        return;

    if (!isPlaceable(entry->type)) {
        m_view->cancelInsert();
        m_inspector->setPlainText(tr("%1\n\nThis palette entry is a drawing or "
                                     "wiring primitive and needs a dedicated tool.")
                                      .arg(entry->displayName()));
        statusBar()->showMessage(tr("%1 is not placeable yet").arg(entry->displayName()), 4000);
        return;
    }

    QStringList lines;
    lines << entry->displayName();
    lines << QString();
    lines << tr("type:  %1").arg(entry->type);
    if (!entry->pins.isEmpty())
        lines << tr("pins:  %1").arg(entry->pins.join(QLatin1Char(' ')));
    lines << tr("box:   %1 x %2 units").arg(entry->box.width()).arg(entry->box.height());
    lines << QString();
    lines << tr("Click the canvas to place this symbol.");
    lines << tr("Double-click the palette entry to place it in the centre.");
    lines << tr("Press Esc to cancel placement.");

    m_inspector->setPlainText(lines.join(QLatin1Char('\n')));
    m_view->beginInsert(entry->makeObject());
    statusBar()->showMessage(tr("Click the canvas to place %1 (Esc to cancel)")
                                 .arg(entry->displayName()),
                             5000);
}

void MainWindow::placePaletteItem(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item || item->data(0, Qt::UserRole).isNull())
        return;

    const cad::PaletteItem* entry = m_palette.find(item->data(0, Qt::UserRole).toInt());
    if (!entry || !isPlaceable(entry->type))
        return;

    m_view->cancelInsert();

    cad::Object object = entry->makeObject();
    const QPointF centre = m_view->viewCenterInUnits();
    const double step = 3.0;
    const int x = int(qRound(centre.x() / step) * step);
    const int y = int(qRound(centre.y() / step) * step);
    object.fields[object.pinFlagCount + 0].value = x;
    object.fields[object.pinFlagCount + 1].value = y;

    m_view->insertObject(object);
    statusBar()->showMessage(tr("Placed %1 at (%2, %3)")
                                 .arg(entry->displayName())
                                 .arg(x)
                                 .arg(y),
                             4000);
}

void MainWindow::createStatusBar()
{
    m_positionLabel = new QLabel(this);
    m_positionLabel->setMinimumWidth(180);
    m_positionLabel->setText(tr("x: —   y: —"));
    statusBar()->addWidget(m_positionLabel);

    m_documentLabel = new QLabel(tr("No document"), this);
    statusBar()->addPermanentWidget(m_documentLabel);
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;

    m_project = cad::Project{};
    m_currentPath.clear();
    m_view->setObjects({});

    updateWindowTitle();
    updateDocumentLabel();
    updateInspector(nullptr);
    m_positionLabel->setText(tr("x: —   y: —"));
    statusBar()->showMessage(tr("New schematic"), 3000);
}

void MainWindow::openFileDialog()
{
    if (!maybeSave())
        return;

    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("files/lastDir"),
                                           QDir::homePath()).toString();

    const QString path = QFileDialog::getOpenFileName(this, tr("Open schematic"),
                                                      lastDir, fileFilter());
    if (!path.isEmpty())
        loadFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    if (!maybeSave())
        return false;
    return loadFile(path);
}

bool MainWindow::loadFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, tr("Open schematic"),
                             tr("%1 does not exist.").arg(path));
        return false;
    }

    const bool native = info.suffix().compare(QStringLiteral("bcs"), Qt::CaseInsensitive) == 0;

    QString error;
    cad::Project project;
    if (native) {
        project = cad::Project::load(path, &error);
    } else {
        const cad::Document document = cad::Document::load(path, &error);
        if (error.isEmpty())
            project = cad::Project::fromDocument(document);
    }

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Open schematic"), error);
        return false;
    }

    m_project = project;
    m_project.modified = false;
    m_currentPath = info.absoluteFilePath();
    m_view->setObjects(m_project.objects);

    addRecentFile(m_currentPath);

    QSettings settings;
    settings.setValue(QStringLiteral("files/lastDir"), info.absolutePath());

    updateWindowTitle();
    updateDocumentLabel();
    updateInspector(nullptr);
    m_positionLabel->setText(tr("x: —   y: —"));

    statusBar()->showMessage(tr("Opened %1").arg(info.fileName()), 4000);
    return true;
}

void MainWindow::saveFile()
{
    if (m_currentPath.isEmpty()
        || QFileInfo(m_currentPath).suffix().compare(QStringLiteral("bcs"),
                                                     Qt::CaseInsensitive)
               != 0) {
        saveFileAs();
        return;
    }

    saveTo(m_currentPath);
}

void MainWindow::saveFileAs()
{
    QSettings settings;
    QString suggested = m_currentPath;
    if (suggested.isEmpty() || !suggested.endsWith(QStringLiteral(".bcs"), Qt::CaseInsensitive))
        suggested = QDir(settings.value(QStringLiteral("files/lastDir"), QDir::homePath()).toString())
                        .filePath(QStringLiteral("schematic.bcs"));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save schematic"), suggested,
        tr("bettercadesimu schematics (*.bcs);;All files (*)"));
    if (path.isEmpty())
        return;

    QString target = path;
    if (QFileInfo(target).suffix().isEmpty())
        target += QStringLiteral(".bcs");

    saveTo(target);
}

bool MainWindow::saveTo(const QString& path)
{
    cad::Project project = m_project;
    project.objects = m_view->objects();
    project.rebuildNextId();

    QString error;
    if (!project.save(path, &error)) {
        QMessageBox::warning(this, tr("Save schematic"), error);
        return false;
    }

    m_project = project;
    m_project.path = path;
    m_project.modified = false;
    m_currentPath = QFileInfo(path).absoluteFilePath();

    addRecentFile(m_currentPath);
    QSettings settings;
    settings.setValue(QStringLiteral("files/lastDir"), QFileInfo(path).absolutePath());

    updateWindowTitle();
    updateDocumentLabel();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_project.modified)
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Unsaved changes"),
        tr("The schematic has unsaved changes.\n\nSave them before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;

    saveFile();
    return !m_project.modified;
}

void MainWindow::reloadFile()
{
    if (m_currentPath.isEmpty()) {
        openFileDialog();
        return;
    }

    if (!maybeSave())
        return;

    const QString path = m_currentPath;
    if (loadFile(path))
        statusBar()->showMessage(tr("Reloaded %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::deleteSelection()
{
    m_view->deleteSelection();
}

void MainWindow::selectAll()
{
    m_view->selectAll();
}

void MainWindow::markModified()
{
    m_project.modified = true;
    updateWindowTitle();
    updateDocumentLabel();
}

void MainWindow::updateCursorPosition(const QPointF& filePosition)
{
    m_positionLabel->setText(tr("x: %1   y: %2")
                                 .arg(filePosition.x(), 0, 'f', 1)
                                 .arg(filePosition.y(), 0, 'f', 1));
}

void MainWindow::updateInspector(const cad::Object* object)
{
    if (!object) {
        m_inspector->setPlainText(tr("No object selected.\n\n"
                                     "Click an object to see its raw record.\n"
                                     "Drag to rubber-band select. Hold middle mouse to pan, "
                                     "scroll to zoom."));
        return;
    }

    QStringList lines;
    lines << tr("#%1  %2  (type %3)")
                 .arg(object->id)
                 .arg(cad::typeName(object->type))
                 .arg(object->type);

    if (!object->label.isEmpty())
        lines << tr("label:  %1").arg(object->label);
    if (!object->sublabel.isEmpty())
        lines << tr("sublabel: %1").arg(object->sublabel);
    if (!object->pins.isEmpty())
        lines << tr("pins:   %1").arg(object->pins.join(QLatin1Char(' ')));

    if (object->isLine()) {
        lines << tr("line:   (%1, %2) → (%3, %4)")
                     .arg(object->x())
                     .arg(object->y())
                     .arg(object->x2())
                     .arg(object->y2());
    } else {
        lines << tr("origin: (%1, %2)").arg(object->x()).arg(object->y());
    }

    lines << QString();
    lines << tr("type field:");
    lines << QStringLiteral("  %1").arg(object->typeParts.join(QLatin1Char('#')));

    lines << QString();
    lines << tr("fields:");
    for (int i = 0; i < object->fields.size(); ++i) {
        const cad::Field& field = object->fields.at(i);
        if (field.text.isEmpty()) {
            lines << QStringLiteral("  %1: %2").arg(i, 2).arg(field.value);
        } else {
            lines << QStringLiteral("  %1: %2  # %3")
                         .arg(i, 2)
                         .arg(field.value)
                         .arg(field.text);
        }
    }

    lines << QString();
    lines << tr("project: %1 object(s)").arg(m_view->objects().size());

    m_inspector->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::updateWindowTitle()
{
    QString name = m_currentPath.isEmpty() ? tr("Untitled")
                                           : QFileInfo(m_currentPath).fileName();
    if (m_project.modified)
        name += QLatin1Char('*');

    setWindowTitle(tr("%1 — bettercadesimu").arg(name));
}

void MainWindow::updateDocumentLabel()
{
    m_documentLabel->setText(tr("%n object(s)", "", int(m_view->objects().size())));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About bettercadesimu"),
                       tr("<h3>bettercadesimu</h3>"
                          "<p>A clean-room reader and editor for IEC motor-control "
                          "schematics, written from scratch with Qt 6.</p>"
                          "<p>This project is not affiliated with, endorsed by, or derived "
                          "from CADe SIMU. It reads the plain-text <code>.cad</code> "
                          "document format for interoperability and draws its own symbols.</p>"
                          "<p>Version %1</p>")
                           .arg(QApplication::applicationVersion()));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;

    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        if (openFile(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    writeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::readSettings()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    m_recentFiles = settings.value(QStringLiteral("files/recent")).toStringList();

    const bool grid = settings.value(QStringLiteral("view/grid"), true).toBool();
    if (m_gridAction)
        m_gridAction->setChecked(grid);
    m_view->setGridVisible(grid);
}

void MainWindow::writeSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("files/recent"), m_recentFiles);
    if (m_gridAction)
        settings.setValue(QStringLiteral("view/grid"), m_gridAction->isChecked());
}

void MainWindow::addRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10)
        m_recentFiles.removeLast();

    QSettings settings;
    settings.setValue(QStringLiteral("files/recent"), m_recentFiles);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();

    if (m_recentFiles.isEmpty()) {
        QAction* empty = m_recentMenu->addAction(tr("(No recent files)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString& path : m_recentFiles) {
        QAction* action = m_recentMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setStatusTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
    }

    m_recentMenu->addSeparator();
    QAction* clearAction = m_recentMenu->addAction(tr("Clear Recent Files"));
    connect(clearAction, &QAction::triggered, this, [this] {
        m_recentFiles.clear();
        QSettings settings;
        settings.remove(QStringLiteral("files/recent"));
    });
}

} // namespace ui
