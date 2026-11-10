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
#include <QUrl>

#include "ui/canvasview.h"

namespace ui {
namespace {

QString fileFilter()
{
    return QObject::tr("CADe SIMU files (*.cad);;All files (*)");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);

    m_view = new CanvasView(this);
    setCentralWidget(m_view);

    createMenus();
    createDock();
    createStatusBar();

    connect(m_view, &CanvasView::cursorMoved, this, &MainWindow::updateCursorPosition);
    connect(m_view, &CanvasView::selectionChanged, this, &MainWindow::updateInspector);

    readSettings();
    updateWindowTitle();

    resize(1280, 860);
    statusBar()->showMessage(tr("Open a .cad file, or drop one onto the window."), 6000);
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip(tr("Open a CADe SIMU document"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);

    QAction* reloadAction = fileMenu->addAction(tr("&Reload"));
    reloadAction->setShortcut(QKeySequence::Refresh);
    reloadAction->setStatusTip(tr("Reload the current document from disk"));
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadFile);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(tr("E&xit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

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

void MainWindow::createStatusBar()
{
    m_positionLabel = new QLabel(this);
    m_positionLabel->setMinimumWidth(180);
    m_positionLabel->setText(tr("x: —   y: —"));
    statusBar()->addWidget(m_positionLabel);

    m_documentLabel = new QLabel(tr("No document"), this);
    statusBar()->addPermanentWidget(m_documentLabel);
}

void MainWindow::openFileDialog()
{
    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("files/lastDir"),
                                           QDir::homePath()).toString();

    const QString path = QFileDialog::getOpenFileName(this, tr("Open schematic"),
                                                      lastDir, fileFilter());
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, tr("Open schematic"),
                             tr("%1 does not exist.").arg(path));
        return false;
    }

    QString error;
    cad::Document document = cad::Document::load(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Open schematic"), error);
        return false;
    }

    m_document = document;
    m_currentPath = info.absoluteFilePath();
    m_view->setDocument(m_document);

    addRecentFile(m_currentPath);

    QSettings settings;
    settings.setValue(QStringLiteral("files/lastDir"), info.absolutePath());

    updateWindowTitle();
    updateInspector(nullptr);
    m_positionLabel->setText(tr("x: —   y: —"));

    QString summary = tr("%n object(s) · %1 record(s)", "", int(m_document.objects.size()))
                          .arg(m_document.recordCount);
    if (!m_document.warnings.isEmpty())
        summary += tr(" · %n warning(s)", "", int(m_document.warnings.size()));

    m_documentLabel->setText(summary);
    m_documentLabel->setToolTip(m_document.warnings.join(QLatin1Char('\n')));

    statusBar()->showMessage(tr("Opened %1").arg(info.fileName()), 4000);
    return true;
}

void MainWindow::reloadFile()
{
    if (m_currentPath.isEmpty()) {
        openFileDialog();
        return;
    }

    openFile(m_currentPath);
    statusBar()->showMessage(tr("Reloaded %1").arg(QFileInfo(m_currentPath).fileName()), 4000);
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
    for (int i = 0; i < cad::kFieldCount; ++i) {
        const cad::Field& field = object->fields[i];
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
    lines << tr("record %1 of %2").arg(object->definitionIndex + 1).arg(m_document.recordCount);

    m_inspector->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::updateWindowTitle()
{
    if (m_currentPath.isEmpty()) {
        setWindowTitle(tr("bettercadesimu"));
        return;
    }

    setWindowTitle(tr("%1 — bettercadesimu").arg(QFileInfo(m_currentPath).fileName()));
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
