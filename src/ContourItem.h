#pragma once

#include "TerrainAnalysis.h"

#include <QGraphicsItem>
#include <QPainterPath>
#include <QPointF>
#include <QString>

struct ContourItemData
{
    double originX = 0.0;
    double originY = 0.0;
    double horizontalUnitToMetre = 1.0;
    QString sourceCrs;
    QString sourceLayerId;
    QString sourceLayerName;
    QString demPath;
    double interval = 1.0;
    double baseElevation = 0.0;
    QVector<famp::terrain::ContourLine> relativeLines;
};

class ContourItem final : public QGraphicsItem
{
public:
    static constexpr int Type = QGraphicsItem::UserType + 102;
    static constexpr quint64 MaximumDisplayPoints = 1'000'000;

    ContourItem(ContourItemData data,
                const QPointF& sceneUnitsPerMeter,
                QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

    const ContourItemData& contourData() const { return data_; }
    QPointF sceneUnitsPerMeter() const { return sceneUnitsPerMeter_; }
    void setSceneUnitsPerMeter(const QPointF& sceneUnitsPerMeter);
    QVector<famp::terrain::ContourLine> absoluteLines() const;
    quint64 pointCount() const;

    static bool createDataFromAbsolute(
        const QVector<famp::terrain::ContourLine>& lines,
        double horizontalUnitToMetre,
        const QString& sourceCrs,
        const QString& sourceLayerId,
        const QString& sourceLayerName,
        const QString& demPath,
        double interval,
        double baseElevation,
        ContourItemData& data,
        QString* errorMessage = nullptr);

    static bool validateData(const ContourItemData& data,
                             QString* errorMessage = nullptr);

private:
    void rebuildGeometry();

    ContourItemData data_;
    QPointF sceneUnitsPerMeter_;
    QPainterPath minorPath_;
    QPainterPath majorPath_;
    QRectF bounds_;
};
