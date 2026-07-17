#pragma once

#include <QMap>
#include <QString>

class QWidget;

namespace famp::archaeology
{
bool editFields(QWidget* parent,
                const QString& layerName,
                const QString& sourcePath,
                const QMap<QString, QString>& currentFields,
                QMap<QString, QString>& updatedFields);
}
