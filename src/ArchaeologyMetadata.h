#pragma once

#include <QMap>
#include <QString>
#include <QVector>

namespace famp::archaeology
{
struct FieldDefinition
{
    QString key;
    QString label;
    bool multiline = false;
};

const QVector<FieldDefinition>& standardFields();
QString canonicalStandardKey(const QString& key);
QString fieldLabel(const QString& key);

bool validateFields(const QMap<QString, QString>& fields,
                    QString* errorMessage = nullptr);
bool normalizeFields(const QMap<QString, QString>& fields,
                     QMap<QString, QString>& normalized,
                     QString* errorMessage = nullptr);
}
