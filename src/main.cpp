#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTextStream>

#include "cad/cadfile.h"
#include "ui/canvasview.h"
#include "ui/mainwindow.h"

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
        .arg(object.fields[14].value)
        .arg(object.fields[15].value);
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
        if (argument == QLatin1String("--dump") || argument == QLatin1String("--render"))
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
        QStringLiteral("Clean-room viewer for CADe SIMU .cad schematics."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption dumpOption(
        QStringLiteral("dump"),
        QStringLiteral("Parse the file and print its objects to stdout."));
    QCommandLineOption renderOption(
        QStringLiteral("render"),
        QStringLiteral("Render the file to a PNG image."),
        QStringLiteral("output.png"));
    parser.addOption(dumpOption);
    parser.addOption(renderOption);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("A .cad document to open."));

    parser.process(app);

    const bool dump = parser.isSet(dumpOption);
    const bool render = parser.isSet(renderOption);
    const QStringList positional = parser.positionalArguments();

    if (dump && render) {
        writeError(QStringLiteral("--dump and --render cannot be used together"));
        return 2;
    }

    if (dump || render) {
        if (positional.isEmpty()) {
            writeError(QStringLiteral("a .cad file is required"));
            return 2;
        }

        QString error;
        const cad::Document document = cad::Document::load(positional.first(), &error);
        if (!error.isEmpty()) {
            writeError(error);
            return 1;
        }

        if (dump) {
            dumpDocument(document);
            return 0;
        }

        const QString output = parser.value(renderOption);
        if (!ui::renderToPng(document, output, {}, &error)) {
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
