#include <gtest/gtest.h>

#include <limits>

#include "CloudAttributes.h"

TEST(CloudAttributesTest, StoresTypedChannelsCaseInsensitively)
{
    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel intensity;
    intensity.name = QStringLiteral(" Intensity ");
    intensity.unit = QStringLiteral(" raw ");
    intensity.type = famp::cloud::AttributeValueType::UnsignedInteger;
    intensity.unsignedValues = {10, 20, 30};
    QString error;

    ASSERT_TRUE(attributes.insert(intensity, 3, &error))
        << error.toStdString();
    EXPECT_TRUE(attributes.contains(QStringLiteral("intensity")));
    EXPECT_EQ(attributes.names(), QStringList{QStringLiteral("Intensity")});
    const auto* stored = attributes.channel(QStringLiteral("INTENSITY"));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->unit, QStringLiteral("raw"));
    double value = 0.0;
    ASSERT_TRUE(stored->valueAsDouble(1, value));
    EXPECT_DOUBLE_EQ(value, 20.0);
    EXPECT_FALSE(stored->valueAsDouble(3, value));
}

TEST(CloudAttributesTest, SummarizesFiniteValuesAndPreservesMissingValues)
{
    famp::cloud::AttributeChannel elevation;
    elevation.name = QStringLiteral("elevation");
    elevation.unit = QStringLiteral("m");
    elevation.floatingValues = {
        12.5,
        std::numeric_limits<double>::quiet_NaN(),
        -3.0,
        std::numeric_limits<double>::infinity()};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(elevation, 4));

    const auto summaries = attributes.summaries();
    ASSERT_EQ(summaries.size(), 1);
    EXPECT_EQ(summaries.front().valueCount, 4);
    EXPECT_EQ(summaries.front().finiteValueCount, 2);
    EXPECT_TRUE(summaries.front().hasFiniteRange);
    EXPECT_DOUBLE_EQ(summaries.front().minimum, -3.0);
    EXPECT_DOUBLE_EQ(summaries.front().maximum, 12.5);
    EXPECT_TRUE(famp::cloud::validateAttributeSummary(summaries.front()));
}

TEST(CloudAttributesTest, RejectsMismatchedStorageAndPointCountAtomically)
{
    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel valid;
    valid.name = QStringLiteral("classification");
    valid.type = famp::cloud::AttributeValueType::UnsignedInteger;
    valid.unsignedValues = {1, 2};
    ASSERT_TRUE(attributes.insert(valid, 2));

    famp::cloud::AttributeChannel invalid = valid;
    invalid.unsignedValues = {4};
    QString error;
    EXPECT_FALSE(attributes.insert(invalid, 2, &error));
    EXPECT_FALSE(error.isEmpty());
    ASSERT_NE(attributes.channel(QStringLiteral("classification")), nullptr);
    EXPECT_EQ(attributes.channel(QStringLiteral("classification"))->valueCount(), 2);

    invalid = valid;
    invalid.signedValues = {-1, -2};
    EXPECT_FALSE(attributes.insert(invalid, 2, &error));
    EXPECT_EQ(attributes.size(), 1);
}

TEST(CloudAttributesTest, ConvertsStableTypeNames)
{
    for (const auto type : {
             famp::cloud::AttributeValueType::Float64,
             famp::cloud::AttributeValueType::SignedInteger,
             famp::cloud::AttributeValueType::UnsignedInteger})
    {
        const QString name = famp::cloud::attributeValueTypeName(type);
        EXPECT_FALSE(name.isEmpty());
        famp::cloud::AttributeValueType loaded{};
        EXPECT_TRUE(famp::cloud::attributeValueTypeFromName(name, loaded));
        EXPECT_EQ(loaded, type);
    }
    famp::cloud::AttributeValueType unchanged =
        famp::cloud::AttributeValueType::SignedInteger;
    EXPECT_FALSE(famp::cloud::attributeValueTypeFromName(
        QStringLiteral("float32"), unchanged));
    EXPECT_EQ(unchanged, famp::cloud::AttributeValueType::SignedInteger);
}

TEST(CloudAttributesTest, RejectsUnknownTypesAndNonFiniteSerializedRanges)
{
    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel channel;
    channel.name = QStringLiteral("unknown");
    channel.type = static_cast<famp::cloud::AttributeValueType>(999);
    QString error;
    EXPECT_FALSE(attributes.insert(channel, 0, &error));
    EXPECT_FALSE(error.isEmpty());

    famp::cloud::AttributeSummary summary;
    summary.name = QStringLiteral("empty");
    summary.minimum = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(famp::cloud::validateAttributeSummary(summary, &error));
}
