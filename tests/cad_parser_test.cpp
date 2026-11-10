#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "cad/cadfile.h"
#include "cad/palette.h"
#include "cad/project.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition)
        return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

QString record(int id, const QString& typeField, QStringList fields = {})
{
    while (fields.size() < cad::kFieldCount)
        fields.append(QStringLiteral("0"));

    QStringList parts;
    parts << QString::number(id) << typeField;
    parts += fields;
    return parts.join(QLatin1Char('*'));
}

QByteArray document(const QStringList& records)
{
    QByteArray data = QByteArrayLiteral("CADe_SIMU");
    for (const QString& item : records)
        data += '*' + item.toLatin1();
    return data;
}

void testRecordReplay()
{
    QStringList first;
    first << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0")
          << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0")
          << QStringLiteral("0") << QStringLiteral("0")
          << QStringLiteral("10") << QStringLiteral("20")
          << QStringLiteral("110") << QStringLiteral("20");
    while (first.size() < cad::kFieldCount)
        first << QStringLiteral("0");

    const QByteArray data = document({
        record(1, QStringLiteral("1#L1#########"), first),
        record(1, QStringLiteral("1#L1#########"), first),
        record(2, QStringLiteral("9000#-K1##A1#A2")),
    });

    QString error;
    const cad::Document parsed = cad::Document::parse(data, &error);

    CHECK(error.isEmpty());
    CHECK(parsed.recordCount == 3);
    CHECK(parsed.objects.size() == 2); // id 1 was replayed, not duplicated
    CHECK(parsed.objects.first().id == 1);
    CHECK(parsed.objects.first().label == QStringLiteral("L1"));
    CHECK(parsed.objects.first().isLine());
    CHECK(parsed.objects.last().label == QStringLiteral("-K1"));
    CHECK(parsed.objects.last().pins == QStringList({QStringLiteral("A1"), QStringLiteral("A2")}));
}

void testGeometryAndText()
{
    QStringList fields;
    fields << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0")
           << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0")
           << QStringLiteral("0") << QStringLiteral("0")
           << QStringLiteral("228") << QStringLiteral("156")
           << QStringLiteral("253") << QStringLiteral("159");
    while (fields.size() < cad::kFieldCount - 1)
        fields << QStringLiteral("0");
    fields << QStringLiteral("0#Motore Trifase");

    QString error;
    const cad::Document parsed = cad::Document::parse(
        document({record(49, QStringLiteral("8### 0# 0# 0# 0# 0# 0# 0#49"), fields)}),
        &error);

    CHECK(error.isEmpty());
    CHECK(parsed.objects.size() == 1);

    const cad::Object& object = parsed.objects.first();
    CHECK(object.type == 8);
    CHECK(object.isText());
    CHECK(object.x() == 228);
    CHECK(object.y() == 156);
    CHECK(object.x2() == 253);
    CHECK(object.y2() == 159);
    CHECK(object.text() == QStringLiteral("Motore Trifase"));
}

void testVariablePinCount()
{
    // A symbol with more than eight pins carries extra pin-flag fields, which
    // shifts everything after them (geometry, box, text). Regression for
    // section 6, whose 9-pin symbols used to truncate the rest of the file.
    QStringList fields;
    for (int i = 0; i < 9; ++i)
        fields << QStringLiteral("1");
    fields << QStringLiteral("12") << QStringLiteral("34"); // x, y
    while (fields.size() < 9 + cad::kTailFields)
        fields << QStringLiteral("0");

    const QByteArray data = document({
        record(1052, QStringLiteral("1052#-T##1#2#3#4#5#6#7#8#9"), fields),
        record(1053, QStringLiteral("1#L1")),
    });

    QString error;
    const cad::Document parsed = cad::Document::parse(data, &error);

    CHECK(error.isEmpty());
    CHECK(parsed.recordCount == 2);
    CHECK(parsed.objects.size() == 2);

    const cad::Object& first = parsed.objects.first();
    CHECK(first.pinFlagCount == 9);
    CHECK(first.pins.size() == 9);
    CHECK(first.x() == 12);
    CHECK(first.y() == 34);

    CHECK(parsed.objects.last().label == QStringLiteral("L1"));
    CHECK(parsed.objects.last().pinFlagCount == cad::kMinPinFlags);
}

void testMissingHeader()
{
    QString error;
    const cad::Document parsed = cad::Document::parse(
        QByteArrayLiteral("not a schematic*1*1#L1"), &error);

    CHECK(!error.isEmpty());
    CHECK(parsed.objects.isEmpty());
    CHECK(parsed.recordCount == 0);
}

void testTrailingTokens()
{
    QByteArray data = document({record(1, QStringLiteral("1#L1"))});
    data += QByteArrayLiteral("*stray");

    QString error;
    const cad::Document parsed = cad::Document::parse(data, &error);

    CHECK(error.isEmpty());
    CHECK(parsed.objects.size() == 1);
    CHECK(!parsed.warnings.isEmpty());
}

void testMetadata()
{
    QByteArray data = document({record(1, QStringLiteral("1#L1"))});
    data += QByteArrayLiteral("$$$some trailing metadata");

    QString error;
    const cad::Document parsed = cad::Document::parse(data, &error);

    CHECK(error.isEmpty());
    CHECK(parsed.objects.size() == 1);
    CHECK(!parsed.metadata.isEmpty());
}

void testPalette()
{
    const QByteArray json = QByteArrayLiteral(R"({
        "version": 1,
        "unitScale": 4.0,
        "sections": [
            {"number": 2, "title": "Fuses", "source": "section 2.cad",
             "items": [
                {"type": 5000, "name": "Fuse I", "label": "-F", "pins": ["1", "2"],
                 "origin": [15, 6], "box": [6, 15]},
                {"type": 5001, "name": "", "label": "-F", "pins": [],
                 "origin": [36, 6], "box": [12, 15]}
             ]}
        ]
    })");

    QString error;
    const cad::Palette palette = cad::Palette::parse(json, &error);
    CHECK(error.isEmpty());
    CHECK(palette.sections.size() == 1);
    CHECK(palette.itemCount() == 2);
    CHECK(palette.namedCount() == 1);

    const cad::PaletteItem* item = palette.find(5000);
    CHECK(item != nullptr);
    if (item) {
        CHECK(item->displayName() == QStringLiteral("Fuse I"));
        CHECK(item->box == QSize(6, 15));
        const cad::Object object = item->makeObject();
        CHECK(object.type == 5000);
        CHECK(object.boxWidth() == 6);
        CHECK(object.boxHeight() == 15);
        CHECK(object.pins == QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    }

    const cad::PaletteItem* unnamed = palette.find(5001);
    CHECK(unnamed != nullptr);
    if (unnamed)
        CHECK(unnamed->displayName() == QStringLiteral("Type 5001"));

    CHECK(palette.find(9999) == nullptr);

    const cad::Palette broken = cad::Palette::parse(QByteArrayLiteral("{"));
    CHECK(broken.sections.isEmpty());
}

void testProject()
{
    cad::Object object;
    object.id = -1;
    object.type = 5000;
    object.label = QStringLiteral("-F1");
    object.pins = QStringList{QStringLiteral("1"), QStringLiteral("2")};
    object.pinFlagCount = cad::kMinPinFlags;
    object.typeParts = QStringList{QStringLiteral("5000"), QStringLiteral("-F1"),
                                   QString(), QStringLiteral("1"), QStringLiteral("2")};
    object.fields.reserve(cad::kFieldCount);
    for (int i = 0; i < cad::kFieldCount; ++i)
        object.fields.append(cad::Field{});
    object.fields[cad::kMinPinFlags + 0].value = 21;
    object.fields[cad::kMinPinFlags + 1].value = 6;
    object.fields[cad::kMinPinFlags + 6].value = 6;
    object.fields[cad::kMinPinFlags + 7].value = 15;
    object.fields[cad::kMinPinFlags + 14].text = QStringLiteral("note");

    cad::Project project;
    const int id = project.addObject(object);
    CHECK(id == 1);
    CHECK(project.objects.size() == 1);
    CHECK(project.nextId == 2);

    const QByteArray json = project.toJson();
    QString error;
    const cad::Project restored = cad::Project::fromJson(json, &error);
    CHECK(error.isEmpty());
    CHECK(restored.objects.size() == 1);
    CHECK(restored.nextId == 2);

    const cad::Object* back = restored.find(1);
    CHECK(back != nullptr);
    if (back) {
        CHECK(back->type == 5000);
        CHECK(back->label == QStringLiteral("-F1"));
        CHECK(back->pins.size() == 2);
        CHECK(back->x() == 21);
        CHECK(back->y() == 6);
        CHECK(back->boxWidth() == 6);
        CHECK(back->boxHeight() == 15);
        CHECK(back->text() == QStringLiteral("note"));
    }

    // Objects with more than eight pins keep their extra pin-flag fields.
    cad::Object wide;
    wide.type = 10550;
    wide.pins = QStringList{QStringLiteral("50"), QStringLiteral("50"), QStringLiteral("-a0"),
                           QStringLiteral(""), QStringLiteral("-a1"), QStringLiteral("-a0"),
                           QStringLiteral(""), QStringLiteral("-a1"), QStringLiteral("-A+"),
                           QStringLiteral("extra")};
    wide.pinFlagCount = wide.pins.size();
    wide.fields.reserve(wide.pinFlagCount + cad::kTailFields);
    for (int i = 0; i < wide.pinFlagCount + cad::kTailFields; ++i)
        wide.fields.append(cad::Field{});
    wide.fields[wide.pinFlagCount + 6].value = 69;
    wide.fields[wide.pinFlagCount + 7].value = 57;

    cad::Project wideProject;
    wideProject.addObject(wide);
    const cad::Project wideBack = cad::Project::fromJson(wideProject.toJson(), &error);
    CHECK(error.isEmpty());
    const cad::Object* restoredWide = wideBack.find(1);
    CHECK(restoredWide != nullptr);
    if (restoredWide) {
        CHECK(restoredWide->pins.size() == 10);
        CHECK(restoredWide->pinFlagCount == 10);
        CHECK(restoredWide->boxWidth() == 69);
        CHECK(restoredWide->boxHeight() == 57);
    }

    CHECK(project.removeObject(1));
    CHECK(project.objects.isEmpty());
    CHECK(!project.removeObject(1));

    CHECK(cad::Project::fromJson(QByteArrayLiteral("{}")).objects.isEmpty());
    const cad::Project empty = cad::Project::fromJson(QByteArrayLiteral("not json"), &error);
    CHECK(empty.objects.isEmpty());
    CHECK(!error.isEmpty());
}

void testFixture()
{
    const QString fixture = qEnvironmentVariable("BETTERCADESIMU_FIXTURE");
    if (fixture.isEmpty()) {
        std::printf("fixture test skipped (set BETTERCADESIMU_FIXTURE to a .cad file)\n");
        return;
    }

    QString error;
    const cad::Document parsed = cad::Document::load(fixture, &error);

    CHECK(error.isEmpty());
    CHECK(parsed.recordCount > 0);
    CHECK(!parsed.objects.isEmpty());

    const QString expected = qEnvironmentVariable("BETTERCADESIMU_EXPECTED_RECORDS");
    if (!expected.isEmpty())
        CHECK(parsed.recordCount == expected.toInt());
    CHECK(parsed.warnings.isEmpty());
}

} // namespace

int main()
{
    testRecordReplay();
    testGeometryAndText();
    testVariablePinCount();
    testMissingHeader();
    testTrailingTokens();
    testMetadata();
    testPalette();
    testProject();
    testFixture();

    if (g_failures == 0) {
        std::printf("all cad_parser_test checks passed\n");
        return 0;
    }

    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
