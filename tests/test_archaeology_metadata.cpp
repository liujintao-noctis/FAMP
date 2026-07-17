#include "ArchaeologyMetadata.h"

#include <gtest/gtest.h>

#include <QMap>

TEST(ArchaeologyMetadataTest, ExposesStableStandardFieldLabels)
{
    const auto& fields = famp::archaeology::standardFields();
    ASSERT_GE(fields.size(), 8);
    EXPECT_EQ(famp::archaeology::canonicalStandardKey(
                  QStringLiteral(" Context ")),
              QStringLiteral("context"));
    EXPECT_EQ(famp::archaeology::fieldLabel(QStringLiteral("context")),
              QStringLiteral("地层/堆积单位"));
    EXPECT_EQ(famp::archaeology::fieldLabel(QStringLiteral("custom-key")),
              QStringLiteral("custom-key"));
}

TEST(ArchaeologyMetadataTest, NormalizesStandardAndCustomFieldsAtomically)
{
    const QMap<QString, QString> source{
        {QStringLiteral(" Context "), QStringLiteral(" Locus-12 ")},
        {QStringLiteral("自定义编号"), QStringLiteral(" F12 ")},
        {QStringLiteral("empty"), QStringLiteral("   ")}};
    QMap<QString, QString> normalized{
        {QStringLiteral("preserved"), QStringLiteral("value")}};
    QString error;
    ASSERT_TRUE(famp::archaeology::normalizeFields(
        source, normalized, &error)) << error.toStdString();
    EXPECT_EQ(normalized.size(), 2);
    EXPECT_EQ(normalized.value(QStringLiteral("context")),
              QStringLiteral("Locus-12"));
    EXPECT_EQ(normalized.value(QStringLiteral("自定义编号")),
              QStringLiteral("F12"));
    EXPECT_FALSE(normalized.contains(QStringLiteral("preserved")));
}

TEST(ArchaeologyMetadataTest, RejectsCaseFoldedDuplicatesWithoutMutation)
{
    const QMap<QString, QString> duplicate{
        {QStringLiteral("Context"), QStringLiteral("A")},
        {QStringLiteral("context"), QStringLiteral("B")}};
    QMap<QString, QString> output{
        {QStringLiteral("preserved"), QStringLiteral("value")}};
    QString error;
    EXPECT_FALSE(famp::archaeology::normalizeFields(
        duplicate, output, &error));
    EXPECT_EQ(output.size(), 1);
    EXPECT_EQ(output.value(QStringLiteral("preserved")),
              QStringLiteral("value"));
    EXPECT_TRUE(error.contains(QStringLiteral("重复")));
}

TEST(ArchaeologyMetadataTest, EnforcesFieldCountAndLengthLimits)
{
    QMap<QString, QString> fields;
    for (int index = 0; index < 129; ++index)
    {
        fields.insert(QStringLiteral("field-%1").arg(index),
                      QStringLiteral("value"));
    }
    QString error;
    EXPECT_FALSE(famp::archaeology::validateFields(fields, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("128")));

    fields.clear();
    fields.insert(QString(129, QLatin1Char('x')), QStringLiteral("value"));
    EXPECT_FALSE(famp::archaeology::validateFields(fields, &error));
}
