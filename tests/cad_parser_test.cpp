#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "cad/cadfile.h"
#include "cad/palette.h"

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
    testFixture();

    if (g_failures == 0) {
        std::printf("all cad_parser_test checks passed\n");
        return 0;
    }

    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
