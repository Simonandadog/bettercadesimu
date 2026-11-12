#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTextStream>

#include "cad/cadfile.h"
#include "cad/palette.h"
#include "cad/project.h"
#include "sim/simulator.h"
#include "ui/canvasview.h"
#include "ui/mainwindow.h"
#include "ui/sheet.h"

namespace {

void writeError(const QString& message)
{
    QTextStream err(stderr);
    err << "bettercadesimu: " << message << Qt::endl;
}

QString describeGeometry(const cad::Object& object)
{
    if (object.isLine()) {
        return QStringLiteral("line (%1, %2) -> (%3, %4)")
            .arg(object.x())
            .arg(object.y())
            .arg(object.x2())
            .arg(object.y2());
    }

    if (object.isText()) {
        return QStringLiteral("text box (%1, %2)-(%3, %4)")
            .arg(object.x())
            .arg(object.y())
            .arg(object.x2())
            .arg(object.y2());
    }

    return QStringLiteral("origin (%1, %2), box %3x%4")
        .arg(object.x())
        .arg(object.y())
        .arg(object.boxWidth())
        .arg(object.boxHeight());
}

void dumpPalette(const cad::Palette& palette)
{
    QTextStream out(stdout);
    for (const cad::PaletteSection& section : palette.sections) {
        out << section.number << ". " << section.title << " (" << section.items.size()
            << ")\n";
        for (const cad::PaletteItem& item : section.items) {
            out << QStringLiteral("  %1  %2").arg(item.type, 6).arg(item.displayName());
            if (!item.pins.isEmpty())
                out << "  [" << item.pins.join(QLatin1Char(',')) << ']';
            out << "  " << item.box.width() << 'x' << item.box.height() << '\n';
        }
    }
    out.flush();
}

bool loadAsProject(const QString& path, cad::Project* project, QString* error)
{
    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("bcs"), Qt::CaseInsensitive) == 0) {
        *project = cad::Project::load(path, error);
        return error->isEmpty();
    }

    const cad::Document document = cad::Document::load(path, error);
    if (!error->isEmpty())
        return false;

    *project = cad::Project::fromDocument(document);
    return true;
}

void dumpDocument(const cad::Document& document)
{
    QTextStream out(stdout);

    out << "file:     " << document.sourcePath << '\n';
    out << "records:  " << document.recordCount << '\n';
    out << "objects:  " << document.objects.size() << '\n';
    out << "metadata: " << (document.metadata.isEmpty() ? "none" : "present") << '\n';

    for (const QString& warning : document.warnings)
        out << "warning:  " << warning << '\n';

    out << '\n';

    for (const cad::Object& object : document.objects) {
        out << QStringLiteral("#%1  %2 (%3)")
                   .arg(object.id)
                   .arg(cad::typeName(object.type))
                   .arg(object.type);

        if (!object.label.isEmpty())
            out << "  label=" << object.label;
        if (!object.pins.isEmpty())
            out << "  pins=" << object.pins.join(QLatin1Char(','));

        out << '\n';
        out << "     " << describeGeometry(object) << '\n';

        if (!object.text().isEmpty())
            out << "     text: " << object.text() << '\n';
    }

    out.flush();
}

} // namespace

int main(int argc, char* argv[])
{
    bool cliMode = false;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QLatin1String("--dump") || argument == QLatin1String("--render")
            || argument == QLatin1String("--palette")
            || argument == QLatin1String("--palette-sheet")
            || argument == QLatin1String("--save-project"))
            cliMode = true;
    }

    // Rendering and dumping never open a window. Force the offscreen platform
    // so the same binary works from a headless shell.
    if (cliMode && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bettercadesimu"));
    QApplication::setApplicationDisplayName(QStringLiteral("bettercadesimu"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("bettercadesimu"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Clean-room IEC schematic editor for CADe SIMU .cad files."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption dumpOption(
        QStringLiteral("dump"),
        QStringLiteral("Parse the file and print its objects to stdout."));
    QCommandLineOption renderOption(
        QStringLiteral("render"),
        QStringLiteral("Render the file to a PNG image."),
        QStringLiteral("output.png"));
    QCommandLineOption paletteOption(
        QStringLiteral("palette"),
        QStringLiteral("Print the symbol palette catalogue."));
    QCommandLineOption paletteSheetOption(
        QStringLiteral("palette-sheet"),
        QStringLiteral("Render one symbol proof sheet per palette section."),
        QStringLiteral("directory"));
    QCommandLineOption saveProjectOption(
        QStringLiteral("save-project"),
        QStringLiteral("Convert a .cad document to the native JSON project format."),
        QStringLiteral("output.bcs"));
    QCommandLineOption simulateOption(
        QStringLiteral("simulate"),
        QStringLiteral("Solve the circuit before --render and highlight active parts."));
    QCommandLineOption pressOption(
        QStringLiteral("press"),
        QStringLiteral("Operate the push button or switch with this object ID (repeatable)."),
        QStringLiteral("id"));
    parser.addOption(dumpOption);
    parser.addOption(renderOption);
    parser.addOption(paletteOption);
    parser.addOption(paletteSheetOption);
    parser.addOption(saveProjectOption);
    parser.addOption(simulateOption);
    parser.addOption(pressOption);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("A .cad or .bcs document to open."));

    parser.process(app);

    const bool dump = parser.isSet(dumpOption);
    const bool render = parser.isSet(renderOption);
    const QStringList positional = parser.positionalArguments();

    if (parser.isSet(paletteOption)) {
        dumpPalette(cad::Palette::loadDefault());
        return 0;
    }

    if (parser.isSet(paletteSheetOption)) {
        QString error;
        if (!ui::renderPaletteSheet(cad::Palette::loadDefault(),
                                    parser.value(paletteSheetOption), &error)) {
            writeError(error);
            return 1;
        }
        QTextStream(stdout) << "wrote palette sheets to "
                            << QFileInfo(parser.value(paletteSheetOption)).absoluteFilePath()
                            << Qt::endl;
        return 0;
    }

    if (parser.isSet(saveProjectOption)) {
        if (positional.isEmpty()) {
            writeError(QStringLiteral("a .cad or .bcs file is required"));
            return 2;
        }

        cad::Project project;
        QString error;
        if (!loadAsProject(positional.first(), &project, &error)) {
            writeError(error);
            return 1;
        }

        const QString output = parser.value(saveProjectOption);
        if (!project.save(output, &error)) {
            writeError(error);
            return 1;
        }

        QTextStream(stdout) << "wrote " << QFileInfo(output).absoluteFilePath() << " ("
                            << project.objects.size() << " objects)" << Qt::endl;
        return 0;
    }

    if (dump && render) {
        writeError(QStringLiteral("--dump and --render cannot be used together"));
        return 2;
    }

    if (dump || render) {
        if (positional.isEmpty()) {
            writeError(QStringLiteral("a .cad or .bcs file is required"));
            return 2;
        }

        QString error;
        cad::Project project;
        if (!loadAsProject(positional.first(), &project, &error)) {
            writeError(error);
            return 1;
        }

        cad::Document document;
        document.objects = project.objects;
        document.recordCount = project.objects.size();
        document.sourcePath = positional.first();

        if (dump) {
            dumpDocument(document);
            return 0;
        }

        sim::State simulationState;
        bool simulated = false;
        if (parser.isSet(simulateOption)) {
            sim::Simulator simulator;
            simulator.setObjects(project.objects);
            for (const QString& value : parser.values(pressOption)) {
                bool ok = false;
                const int id = value.toInt(&ok);
                if (ok)
                    simulator.operate(id);
            }
            simulationState = simulator.compute();
            simulated = true;
            for (const QString& warning : simulationState.warnings)
                writeError(warning);
        }

        const QString output = parser.value(renderOption);
        if (!ui::renderToPng(document, output, {}, &error,
                             simulated ? &simulationState : nullptr)) {
            writeError(error);
            return 1;
        }

        QTextStream(stdout) << "wrote " << QFileInfo(output).absoluteFilePath() << Qt::endl;
        return 0;
    }

    ui::MainWindow window;
    window.show();

    if (!positional.isEmpty())
        window.openFile(positional.first());

    return app.exec();
}
