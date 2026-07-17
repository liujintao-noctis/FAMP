#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace famp::cloud
{
enum class AttributeValueType
{
    Float64,
    SignedInteger,
    UnsignedInteger
};

QString attributeValueTypeName(AttributeValueType type);
bool attributeValueTypeFromName(const QString& name, AttributeValueType& type);

struct AttributeSummary
{
    QString name;
    QString unit;
    AttributeValueType type = AttributeValueType::Float64;
    qint64 valueCount = 0;
    qint64 finiteValueCount = 0;
    bool hasFiniteRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
};

bool validateAttributeSummary(const AttributeSummary& summary,
                              QString* errorMessage = nullptr);

struct AttributeChannel
{
    QString name;
    QString unit;
    AttributeValueType type = AttributeValueType::Float64;
    QVector<double> floatingValues;
    QVector<qint64> signedValues;
    QVector<quint64> unsignedValues;

    qint64 valueCount() const;
    bool valueAsDouble(qint64 index, double& value) const;
    AttributeSummary summary() const;
};

class CloudAttributes
{
public:
    bool insert(AttributeChannel channel,
                qint64 expectedPointCount = -1,
                QString* errorMessage = nullptr);
    bool remove(const QString& name);
    void clear();

    bool contains(const QString& name) const;
    int size() const;
    bool isEmpty() const;
    QStringList names() const;
    const AttributeChannel* channel(const QString& name) const;
    QVector<AttributeSummary> summaries() const;

    bool validate(qint64 expectedPointCount = -1,
                  QString* errorMessage = nullptr) const;

private:
    QMap<QString, AttributeChannel> channels_;
};
}
