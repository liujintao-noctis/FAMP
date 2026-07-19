#include "CloudAttributes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr int MaxAttributeChannels = 256;
constexpr int MaxAttributeNameLength = 128;
constexpr int MaxAttributeUnitLength = 64;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

QString normalizedKey(const QString& name)
{
    return name.trimmed().toCaseFolded();
}

bool validNameAndUnit(const QString& name,
                      const QString& unit,
                      QString* errorMessage)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || trimmedName.size() > MaxAttributeNameLength)
    {
        setError(errorMessage, QStringLiteral("点属性名称不能为空且不能超过 128 个字符。"));
        return false;
    }
    if (unit.trimmed().size() > MaxAttributeUnitLength)
    {
        setError(errorMessage, QStringLiteral("点属性单位不能超过 64 个字符。"));
        return false;
    }
    return true;
}

bool validateChannel(const famp::cloud::AttributeChannel& channel,
                     qint64 expectedPointCount,
                     QString* errorMessage)
{
    if (!validNameAndUnit(channel.name, channel.unit, errorMessage))
        return false;
    if (famp::cloud::attributeValueTypeName(channel.type).isEmpty())
    {
        setError(errorMessage, QStringLiteral("点属性数值类型无效。"));
        return false;
    }

    const bool hasFloating = !channel.floatingValues.isEmpty();
    const bool hasSigned = !channel.signedValues.isEmpty();
    const bool hasUnsigned = !channel.unsignedValues.isEmpty();
    switch (channel.type)
    {
    case famp::cloud::AttributeValueType::Float64:
        if (hasSigned || hasUnsigned)
        {
            setError(errorMessage, QStringLiteral("浮点属性包含了不匹配的整数存储。"));
            return false;
        }
        break;
    case famp::cloud::AttributeValueType::SignedInteger:
        if (hasFloating || hasUnsigned)
        {
            setError(errorMessage, QStringLiteral("有符号整数属性包含了不匹配的存储。"));
            return false;
        }
        break;
    case famp::cloud::AttributeValueType::UnsignedInteger:
        if (hasFloating || hasSigned)
        {
            setError(errorMessage, QStringLiteral("无符号整数属性包含了不匹配的存储。"));
            return false;
        }
        break;
    }

    const qint64 count = channel.valueCount();
    if (expectedPointCount >= 0 && count != expectedPointCount)
    {
        setError(errorMessage,
                 QStringLiteral("点属性 %1 的值数量 %2 与点数 %3 不一致。")
                     .arg(channel.name.trimmed())
                     .arg(count)
                     .arg(expectedPointCount));
        return false;
    }
    return true;
}
}

namespace famp::cloud
{
QString attributeValueTypeName(AttributeValueType type)
{
    switch (type)
    {
    case AttributeValueType::Float64:
        return QStringLiteral("float64");
    case AttributeValueType::SignedInteger:
        return QStringLiteral("int64");
    case AttributeValueType::UnsignedInteger:
        return QStringLiteral("uint64");
    }
    return {};
}

bool attributeValueTypeFromName(const QString& name, AttributeValueType& type)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("float64"))
        type = AttributeValueType::Float64;
    else if (normalized == QStringLiteral("int64"))
        type = AttributeValueType::SignedInteger;
    else if (normalized == QStringLiteral("uint64"))
        type = AttributeValueType::UnsignedInteger;
    else
        return false;
    return true;
}

bool validateAttributeSummary(const AttributeSummary& summary,
                              QString* errorMessage)
{
    if (!validNameAndUnit(summary.name, summary.unit, errorMessage))
        return false;
    if (attributeValueTypeName(summary.type).isEmpty())
    {
        setError(errorMessage, QStringLiteral("点属性摘要的数值类型无效。"));
        return false;
    }
    if (summary.valueCount < 0 || summary.finiteValueCount < 0
        || summary.finiteValueCount > summary.valueCount)
    {
        setError(errorMessage, QStringLiteral("点属性摘要的计数无效。"));
        return false;
    }
    if (summary.hasFiniteRange)
    {
        if (summary.finiteValueCount == 0
            || !std::isfinite(summary.minimum)
            || !std::isfinite(summary.maximum)
            || summary.minimum > summary.maximum)
        {
            setError(errorMessage, QStringLiteral("点属性摘要的数值范围无效。"));
            return false;
        }
    }
    else if (summary.finiteValueCount != 0)
    {
        setError(errorMessage, QStringLiteral("点属性摘要缺少有限值范围。"));
        return false;
    }
    if (!std::isfinite(summary.minimum) || !std::isfinite(summary.maximum))
    {
        setError(errorMessage, QStringLiteral("点属性摘要范围必须使用有限数值。"));
        return false;
    }
    return true;
}

qint64 AttributeChannel::valueCount() const
{
    switch (type)
    {
    case AttributeValueType::Float64:
        return floatingValues.size();
    case AttributeValueType::SignedInteger:
        return signedValues.size();
    case AttributeValueType::UnsignedInteger:
        return unsignedValues.size();
    }
    return 0;
}

bool AttributeChannel::valueAsDouble(qint64 index, double& value) const
{
    if (index < 0 || index >= valueCount())
        return false;
    switch (type)
    {
    case AttributeValueType::Float64:
        value = floatingValues.at(static_cast<int>(index));
        break;
    case AttributeValueType::SignedInteger:
        value = static_cast<double>(signedValues.at(static_cast<int>(index)));
        break;
    case AttributeValueType::UnsignedInteger:
        value = static_cast<double>(unsignedValues.at(static_cast<int>(index)));
        break;
    }
    return true;
}

AttributeSummary AttributeChannel::summary() const
{
    AttributeSummary result;
    result.name = name.trimmed();
    result.unit = unit.trimmed();
    result.type = type;
    result.valueCount = valueCount();

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (qint64 index = 0; index < result.valueCount; ++index)
    {
        double value = 0.0;
        if (!valueAsDouble(index, value) || !std::isfinite(value))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        ++result.finiteValueCount;
    }
    if (result.finiteValueCount > 0)
    {
        result.hasFiniteRange = true;
        result.minimum = minimum;
        result.maximum = maximum;
    }
    return result;
}

bool CloudAttributes::insert(AttributeChannel channel,
                             qint64 expectedPointCount,
                             QString* errorMessage)
{
    if (!validateChannel(channel, expectedPointCount, errorMessage))
        return false;
    const QString key = normalizedKey(channel.name);
    if (!channels_.contains(key) && channels_.size() >= MaxAttributeChannels)
    {
        setError(errorMessage, QStringLiteral("点属性通道数量超过 256 个安全上限。"));
        return false;
    }
    channel.name = channel.name.trimmed();
    channel.unit = channel.unit.trimmed();
    channels_.insert(key, std::move(channel));
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool CloudAttributes::remove(const QString& name)
{
    return channels_.remove(normalizedKey(name)) > 0;
}

void CloudAttributes::clear()
{
    channels_.clear();
}

bool CloudAttributes::contains(const QString& name) const
{
    return channels_.contains(normalizedKey(name));
}

int CloudAttributes::size() const
{
    return channels_.size();
}

bool CloudAttributes::isEmpty() const
{
    return channels_.isEmpty();
}

QStringList CloudAttributes::names() const
{
    QStringList result;
    result.reserve(channels_.size());
    for (const AttributeChannel& channel : channels_)
        result.append(channel.name);
    return result;
}

const AttributeChannel* CloudAttributes::channel(const QString& name) const
{
    const auto found = channels_.constFind(normalizedKey(name));
    return found == channels_.cend() ? nullptr : &found.value();
}

QVector<AttributeSummary> CloudAttributes::summaries() const
{
    QVector<AttributeSummary> result;
    result.reserve(channels_.size());
    for (const AttributeChannel& channel : channels_)
        result.append(channel.summary());
    return result;
}

bool CloudAttributes::select(const QVector<qint64>& sourceIndices,
                             CloudAttributes& output,
                             QString* errorMessage) const
{
    CloudAttributes candidate;
    for (auto iterator = channels_.cbegin(); iterator != channels_.cend();
         ++iterator)
    {
        const AttributeChannel& source = iterator.value();
        AttributeChannel selected;
        selected.name = source.name;
        selected.unit = source.unit;
        selected.type = source.type;
        switch (selected.type)
        {
        case AttributeValueType::Float64:
            selected.floatingValues.reserve(sourceIndices.size());
            break;
        case AttributeValueType::SignedInteger:
            selected.signedValues.reserve(sourceIndices.size());
            break;
        case AttributeValueType::UnsignedInteger:
            selected.unsignedValues.reserve(sourceIndices.size());
            break;
        }

        for (qint64 sourceIndex : sourceIndices)
        {
            if (sourceIndex < 0 || sourceIndex >= source.valueCount())
            {
                setError(
                    errorMessage,
                    QStringLiteral("点属性 %1 的源索引 %2 超出范围。")
                        .arg(source.name)
                        .arg(sourceIndex));
                return false;
            }
            const int index = static_cast<int>(sourceIndex);
            switch (selected.type)
            {
            case AttributeValueType::Float64:
                selected.floatingValues.append(source.floatingValues.at(index));
                break;
            case AttributeValueType::SignedInteger:
                selected.signedValues.append(source.signedValues.at(index));
                break;
            case AttributeValueType::UnsignedInteger:
                selected.unsignedValues.append(source.unsignedValues.at(index));
                break;
            }
        }
        if (!candidate.insert(
                std::move(selected), sourceIndices.size(), errorMessage))
        {
            return false;
        }
    }
    output = std::move(candidate);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool CloudAttributes::validate(qint64 expectedPointCount,
                               QString* errorMessage) const
{
    if (channels_.size() > MaxAttributeChannels)
    {
        setError(errorMessage, QStringLiteral("点属性通道数量超过安全上限。"));
        return false;
    }
    for (auto iterator = channels_.cbegin(); iterator != channels_.cend(); ++iterator)
    {
        if (iterator.key() != normalizedKey(iterator.value().name)
            || !validateChannel(iterator.value(), expectedPointCount, errorMessage))
        {
            if (errorMessage && errorMessage->isEmpty())
                *errorMessage = QStringLiteral("点属性索引无效。");
            return false;
        }
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}
}
