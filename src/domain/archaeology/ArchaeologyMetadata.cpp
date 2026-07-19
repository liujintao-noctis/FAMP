#include "ArchaeologyMetadata.h"

#include <QSet>

namespace
{
constexpr int MaxFieldCount = 128;
constexpr int MaxKeyLength = 128;
constexpr int MaxValueLength = 16 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}
}

namespace famp::archaeology
{
const QVector<FieldDefinition>& standardFields()
{
    static const QVector<FieldDefinition> fields{
        {QStringLiteral("site"), QStringLiteral("遗址名称"), false},
        {QStringLiteral("area"), QStringLiteral("发掘区"), false},
        {QStringLiteral("trench"), QStringLiteral("探方/探沟"), false},
        {QStringLiteral("context"), QStringLiteral("地层/堆积单位"), false},
        {QStringLiteral("feature"), QStringLiteral("遗迹/现象编号"), false},
        {QStringLiteral("period"), QStringLiteral("年代/文化期"), false},
        {QStringLiteral("excavationDate"), QStringLiteral("发掘日期"), false},
        {QStringLiteral("excavator"), QStringLiteral("记录人/发掘人"), false},
        {QStringLiteral("description"), QStringLiteral("说明"), true}};
    return fields;
}

QString canonicalStandardKey(const QString& key)
{
    const QString normalized = key.trimmed().toCaseFolded();
    for (const FieldDefinition& field : standardFields())
    {
        if (field.key.toCaseFolded() == normalized)
            return field.key;
    }
    return {};
}

QString fieldLabel(const QString& key)
{
    const QString canonical = canonicalStandardKey(key);
    for (const FieldDefinition& field : standardFields())
    {
        if (field.key == canonical)
            return field.label;
    }
    return key.trimmed();
}

bool validateFields(const QMap<QString, QString>& fields,
                    QString* errorMessage)
{
    if (fields.size() > MaxFieldCount)
    {
        setError(errorMessage, QStringLiteral("考古字段数量超过 128 个安全上限。"));
        return false;
    }

    QSet<QString> normalizedKeys;
    for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator)
    {
        const QString key = iterator.key().trimmed();
        const QString normalizedKey = key.toCaseFolded();
        if (key.isEmpty())
        {
            setError(errorMessage, QStringLiteral("考古字段名称不能为空。"));
            return false;
        }
        if (iterator.key().size() > MaxKeyLength)
        {
            setError(errorMessage,
                     QStringLiteral("考古字段名称不能超过 128 个字符。"));
            return false;
        }
        if (iterator.value().size() > MaxValueLength)
        {
            setError(errorMessage,
                     QStringLiteral("考古字段 %1 的值不能超过 16384 个字符。")
                         .arg(key));
            return false;
        }
        if (normalizedKeys.contains(normalizedKey))
        {
            setError(errorMessage,
                     QStringLiteral("考古字段名称不能重复：%1").arg(key));
            return false;
        }
        normalizedKeys.insert(normalizedKey);
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool normalizeFields(const QMap<QString, QString>& fields,
                     QMap<QString, QString>& normalized,
                     QString* errorMessage)
{
    QMap<QString, QString> candidate;
    QSet<QString> normalizedKeys;
    for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator)
    {
        const QString inputKey = iterator.key().trimmed();
        const QString value = iterator.value().trimmed();
        if (inputKey.isEmpty() && value.isEmpty())
            continue;
        if (inputKey.isEmpty())
        {
            setError(errorMessage, QStringLiteral("考古字段名称不能为空。"));
            return false;
        }
        if (value.isEmpty())
            continue;

        const QString standardKey = canonicalStandardKey(inputKey);
        const QString outputKey = standardKey.isEmpty() ? inputKey : standardKey;
        const QString normalizedKey = outputKey.toCaseFolded();
        if (normalizedKeys.contains(normalizedKey))
        {
            setError(errorMessage,
                     QStringLiteral("考古字段名称不能重复：%1").arg(outputKey));
            return false;
        }
        normalizedKeys.insert(normalizedKey);
        candidate.insert(outputKey, value);
    }
    if (!validateFields(candidate, errorMessage))
        return false;
    normalized = candidate;
    return true;
}
}
