#include "AdditionalCloudLoader.h"

#include "QstringAndStringConvert.h"

#include <pcl/io/ply_io.h>
#include <pcl/common/point_tests.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>

#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>

namespace
{
constexpr std::size_t MaxTextPoints = 100'000'000;
constexpr qint64 CopyBufferSize = 1024 * 1024;
constexpr qint64 MaxHeaderBytes = 1024 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool plyHeaderHasColors(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    bool red = false;
    bool green = false;
    bool blue = false;
    bool vertexProperties = false;
    qint64 headerBytes = 0;
    while (!file.atEnd())
    {
        const QByteArray rawLine = file.readLine(64 * 1024);
        headerBytes += rawLine.size();
        if (headerBytes > MaxHeaderBytes
            || (!rawLine.endsWith('\n') && !file.atEnd()))
        {
            return false;
        }
        const QString line = QString::fromLatin1(rawLine).trimmed().toLower();
        const QStringList fields = line.split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.size() >= 2 && fields.at(0) == QStringLiteral("element"))
            vertexProperties = fields.at(1) == QStringLiteral("vertex");
        if (vertexProperties && fields.size() >= 3
            && fields.at(0) == QStringLiteral("property"))
        {
            const QString name = fields.back();
            red = red || name == QStringLiteral("red") || name == QStringLiteral("r");
            green = green || name == QStringLiteral("green") || name == QStringLiteral("g");
            blue = blue || name == QStringLiteral("blue") || name == QStringLiteral("b");
        }
        if (line == QStringLiteral("end_header"))
            break;
    }
    return red && green && blue;
}

bool copyIntoTemporaryFile(const QString& sourcePath,
                           QTemporaryFile& target,
                           const famp::tasks::CancellationCheck& shouldCancel)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    while (!source.atEnd())
    {
        if (famp::tasks::isCancellationRequested(shouldCancel))
            return false;
        const QByteArray chunk = source.read(CopyBufferSize);
        if ((chunk.isEmpty() && source.error() != QFile::NoError)
            || target.write(chunk) != chunk.size())
        {
            return false;
        }
    }
    if (!target.flush() || target.error() != QFile::NoError)
        return false;
    target.close();
    return true;
}

bool loadPlyFromStdPath(
    const std::string& path,
    bool hasColors,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud)
{
    auto converted = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    if (hasColors)
    {
        if (pcl::io::loadPLYFile(path, *converted) != 0)
            return false;
    }
    else
    {
        pcl::PointCloud<pcl::PointXYZ> xyz;
        if (pcl::io::loadPLYFile(path, xyz) != 0)
            return false;
        converted->reserve(xyz.size());
        for (const pcl::PointXYZ& source : xyz)
        {
            pcl::PointXYZRGB point;
            point.x = source.x;
            point.y = source.y;
            point.z = source.z;
            point.r = 255;
            point.g = 255;
            point.b = 255;
            converted->push_back(point);
        }
    }
    converted->erase(
        std::remove_if(converted->begin(), converted->end(),
                       [](const pcl::PointXYZRGB& point) {
                           return !pcl::isFinite(point);
                       }),
        converted->end());
    if (converted->empty())
        return false;
    converted->width = static_cast<std::uint32_t>(converted->size());
    converted->height = 1;
    converted->is_dense = true;
    outCloud = converted;
    return true;
}
}

bool loadPlyAsRgb(
    const QString& path,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (famp::tasks::isCancellationRequested(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("PLY 点云加载已取消。"));
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable())
    {
        setError(errorMessage, QStringLiteral("PLY 文件不存在或不可读：%1").arg(path));
        return false;
    }

    const bool hasColors = plyHeaderHasColors(info.absoluteFilePath());
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    if (!loadPlyFromStdPath(qstr2str(info.absoluteFilePath()), hasColors, loaded))
    {
        QTemporaryFile temporary(
            QDir::tempPath() + QStringLiteral("/FAMP-ply-XXXXXX.ply"));
        temporary.setAutoRemove(true);
        if (!temporary.open())
        {
            setError(errorMessage, QStringLiteral("无法创建 PLY 临时文件。"));
            return false;
        }
        const QString temporaryPath = temporary.fileName();
        if (!copyIntoTemporaryFile(
                info.absoluteFilePath(), temporary, shouldCancel)
            || !loadPlyFromStdPath(qstr2str(temporaryPath), hasColors, loaded))
        {
            setError(errorMessage,
                     famp::tasks::isCancellationRequested(shouldCancel)
                         ? QStringLiteral("PLY 点云加载已取消。")
                         : QStringLiteral("PLY 文件无效或不受支持：%1").arg(path));
            return false;
        }
    }
    if (famp::tasks::isCancellationRequested(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("PLY 点云加载已取消。"));
        return false;
    }
    outCloud = loaded;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool loadXyzTextAsRgb(
    const QString& path,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取 XYZ 文件：%1").arg(file.errorString()));
        return false;
    }

    auto loaded = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    QTextStream stream(&file);
    const QRegularExpression separator(QStringLiteral("[,;\\s]+"));
    qint64 lineNumber = 0;
    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        ++lineNumber;
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if ((loaded->size() & 0x0fffU) == 0U
            && famp::tasks::isCancellationRequested(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("XYZ 点云加载已取消。"));
            return false;
        }

        const QStringList fields = trimmed.split(separator, Qt::SkipEmptyParts);
        if (fields.size() != 3 && fields.size() != 6)
        {
            setError(errorMessage,
                     QStringLiteral("XYZ 第 %1 行应包含 3 个坐标或 3 个坐标加 RGB。")
                         .arg(lineNumber));
            return false;
        }
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const double x = fields.at(0).toDouble(&okX);
        const double y = fields.at(1).toDouble(&okY);
        const double z = fields.at(2).toDouble(&okZ);
        if (!okX || !okY || !okZ
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
            || x < -std::numeric_limits<float>::max()
            || x > std::numeric_limits<float>::max()
            || y < -std::numeric_limits<float>::max()
            || y > std::numeric_limits<float>::max()
            || z < -std::numeric_limits<float>::max()
            || z > std::numeric_limits<float>::max())
        {
            setError(errorMessage,
                     QStringLiteral("XYZ 第 %1 行包含无效坐标。").arg(lineNumber));
            return false;
        }

        int red = 255;
        int green = 255;
        int blue = 255;
        if (fields.size() == 6)
        {
            bool okRed = false;
            bool okGreen = false;
            bool okBlue = false;
            red = fields.at(3).toInt(&okRed);
            green = fields.at(4).toInt(&okGreen);
            blue = fields.at(5).toInt(&okBlue);
            if (!okRed || !okGreen || !okBlue
                || red < 0 || red > 255
                || green < 0 || green > 255
                || blue < 0 || blue > 255)
            {
                setError(errorMessage,
                         QStringLiteral("XYZ 第 %1 行的 RGB 必须是 0 到 255 的整数。")
                             .arg(lineNumber));
                return false;
            }
        }
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(x);
        point.y = static_cast<float>(y);
        point.z = static_cast<float>(z);
        point.r = static_cast<std::uint8_t>(red);
        point.g = static_cast<std::uint8_t>(green);
        point.b = static_cast<std::uint8_t>(blue);
        loaded->push_back(point);
        if (loaded->size() > MaxTextPoints)
        {
            setError(errorMessage, QStringLiteral("XYZ 点数超过安全上限。"));
            return false;
        }
    }
    if (stream.status() != QTextStream::Ok || loaded->empty())
    {
        setError(errorMessage, QStringLiteral("XYZ 文件为空或读取失败：%1").arg(path));
        return false;
    }
    loaded->width = static_cast<std::uint32_t>(loaded->size());
    loaded->height = 1;
    loaded->is_dense = true;
    outCloud = loaded;
    if (errorMessage)
        errorMessage->clear();
    return true;
}
