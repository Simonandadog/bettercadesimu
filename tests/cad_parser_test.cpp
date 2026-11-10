#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "cad/cadfile.h"

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
}

} // namespace

int main()
{
    testRecordReplay();
    testGeometryAndText();
    testMissingHeader();
    testTrailingTokens();
    testMetadata();
    testFixture();

    if (g_failures == 0) {
        std::printf("all cad_parser_test checks passed\n");
        return 0;
    }

    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
