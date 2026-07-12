#include "ArchaeologyReport.h"

#include "Measurement.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPrinter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTextStream>

#include <cmath>

namespace famp::report
{
namespace
{
struct MeasurementRow
{
    QString kind;
    QString value;
};

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

QString escaped(const QString& value)
{
    return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

bool readPoint(const QJsonValue& value, QPointF& point)
{
    if (!value.isArray())
        return false;
    const QJsonArray values = value.toArray();
    if (values.size() != 2 || !values.at(0).isDouble() || !values.at(1).isDouble())
        return false;
    point = QPointF(values.at(0).toDouble(), values.at(1).toDouble());
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

void collectMeasurements(const QJsonArray& items,
                         QVector<MeasurementRow>& rows)
{
    for (const QJsonValue& value : items)
    {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("group"))
        {
            collectMeasurements(item.value(QStringLiteral("children")).toArray(), rows);
            continue;
        }
        if (item.value(QStringLiteral("type")).toString()
            != QStringLiteral("measurement"))
        {
            continue;
        }
        famp::measurement::Kind kind = famp::measurement::Kind::Distance;
        QString kindName = QStringLiteral("距离");
        const QString storedKind = item.value(QStringLiteral("kind")).toString();
        if (storedKind == QStringLiteral("area"))
        {
            kind = famp::measurement::Kind::Area;
            kindName = QStringLiteral("面积");
        }
        else if (storedKind == QStringLiteral("angle"))
        {
            kind = famp::measurement::Kind::Angle;
            kindName = QStringLiteral("角度");
        }
        QVector<QPointF> points;
        for (const QJsonValue& pointValue : item.value(QStringLiteral("meterPoints")).toArray())
        {
            QPointF point;
            if (!readPoint(pointValue, point))
            {
                points.clear();
                break;
            }
            points.append(point);
        }
        if (points.size() < famp::measurement::minimumPointCount(kind))
            continue;
        rows.append({kindName, famp::measurement::formatSummary(kind, points)});
    }
}

QString originText(const famp::cloud::SpatialReference& spatial)
{
    return QStringLiteral("%1, %2, %3")
        .arg(spatial.origin[0], 0, 'g', 12)
        .arg(spatial.origin[1], 0, 'g', 12)
        .arg(spatial.origin[2], 0, 'g', 12);
}

bool writeBytesAtomically(const QString& path,
                          const QByteArray& bytes,
                          QString* errorMessage)
{
    if (path.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("报告保存路径不能为空。"));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage, QStringLiteral("无法创建报告 %1：%2")
                                   .arg(path, file.errorString()));
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        setError(errorMessage, QStringLiteral("无法完成报告 %1：%2")
                                   .arg(path, file.errorString()));
        return false;
    }
    return true;
}
}

QString toHtml(const Data& data, QString* errorMessage)
{
    if (!data.generatedAt.isValid())
    {
        setError(errorMessage, QStringLiteral("报告生成时间无效。"));
        return {};
    }
    QVector<MeasurementRow> measurements;
    collectMeasurements(data.graphicsState.value(QStringLiteral("items")).toArray(),
                        measurements);
    QString html;
    QTextStream stream(&html);
    stream << "<!doctype html><html><head><meta charset=\"utf-8\">"
              "<style>body{font-family:sans-serif;color:#222;margin:36px}"
              "h1{border-bottom:2px solid #555;padding-bottom:8px}"
              "table{border-collapse:collapse;width:100%;margin:12px 0 24px}"
              "th,td{border:1px solid #aaa;padding:7px;text-align:left;vertical-align:top}"
              "th{background:#eee}.muted{color:#666;font-size:90%}</style></head><body>";
    stream << "<h1>田野考古制图项目报告</h1>";
    stream << "<table><tr><th>项目名称</th><td>" << escaped(data.projectName)
           << "</td></tr><tr><th>项目文件</th><td>" << escaped(data.projectPath)
           << "</td></tr><tr><th>坐标参考系</th><td>"
           << escaped(data.projectCrs.isEmpty() ? QStringLiteral("未声明") : data.projectCrs)
           << "</td></tr><tr><th>制图比例尺</th><td>" << escaped(data.mapScale)
           << "</td></tr><tr><th>生成时间</th><td>"
           << escaped(data.generatedAt.toLocalTime().toString(Qt::ISODate))
           << "</td></tr><tr><th>软件版本</th><td>"
           << escaped(data.applicationVersion) << "</td></tr></table>";

    stream << "<h2>点云清单</h2><table><tr><th>#</th><th>文件</th><th>点数</th>"
              "<th>状态</th><th>原始坐标原点 X, Y, Z</th></tr>";
    for (qsizetype index = 0; index < data.clouds.size(); ++index)
    {
        const auto& cloud = data.clouds.at(index);
        stream << "<tr><td>" << index + 1 << "</td><td>" << escaped(cloud.path)
               << "</td><td>" << static_cast<qulonglong>(cloud.pointCount)
               << "</td><td>" << (cloud.visible ? QStringLiteral("可见") : QStringLiteral("隐藏"))
               << "</td><td>" << escaped(originText(cloud.spatial)) << "</td></tr>";
    }
    if (data.clouds.isEmpty())
        stream << "<tr><td colspan=\"5\">无已加载点云</td></tr>";
    stream << "</table><h2>二维测量成果</h2><table><tr><th>#</th><th>类型</th><th>结果</th></tr>";
    for (qsizetype index = 0; index < measurements.size(); ++index)
    {
        stream << "<tr><td>" << index + 1 << "</td><td>"
               << escaped(measurements.at(index).kind) << "</td><td>"
               << escaped(measurements.at(index).value) << "</td></tr>";
    }
    if (measurements.isEmpty())
        stream << "<tr><td colspan=\"3\">无测量成果</td></tr>";
    stream << "</table><p class=\"muted\">本报告由 FAMP 自动生成；点云坐标与测量成果应结合现场控制点和项目坐标系复核。</p>"
              "</body></html>";
    if (errorMessage)
        errorMessage->clear();
    return html;
}

bool saveHtml(const QString& path, const Data& data, QString* errorMessage)
{
    const QString html = toHtml(data, errorMessage);
    return !html.isEmpty()
        && writeBytesAtomically(path, html.toUtf8(), errorMessage);
}

bool savePdf(const QString& path, const Data& data, QString* errorMessage)
{
    const QString html = toHtml(data, errorMessage);
    if (html.isEmpty())
        return false;
    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        setError(errorMessage, QStringLiteral("无法创建 PDF 临时目录。"));
        return false;
    }
    const QString temporaryPath = temporary.filePath(QStringLiteral("report.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(temporaryPath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QTextDocument document;
    document.setHtml(html);
    document.print(&printer);
    QFile file(temporaryPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage, QStringLiteral("PDF 报告生成失败。"));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (!bytes.startsWith("%PDF-") || bytes.size() < 100)
    {
        setError(errorMessage, QStringLiteral("生成的 PDF 报告无效。"));
        return false;
    }
    return writeBytesAtomically(path, bytes, errorMessage);
}
}
