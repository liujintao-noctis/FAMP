#include "LasLoader.h"
#include "QstringAndStringConvert.h"

#include <lasreader.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
constexpr std::uint64_t MaxPreallocatedPoints = 1'000'000;
constexpr qint64 CopyBufferSize = 1024 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool copyIntoTemporaryFile(const QString& sourcePath,
                           QTemporaryFile& target)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    while (!source.atEnd())
    {
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

bool loadLasFromStdPath(const std::string& path,
                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                        std::array<double, 3>* origin)
{
    LASreadOpener opener;
    std::vector<char> pathBuffer(path.begin(), path.end());
    pathBuffer.push_back('\0');
    opener.set_file_name(pathBuffer.data());

    auto closeReader = [](LASreader* reader) {
        if (reader)
        {
            reader->close();
            delete reader;
        }
    };
    std::unique_ptr<LASreader, decltype(closeReader)> reader(opener.open(), closeReader);
    if (!reader)
        return false;

    const double centerX = reader->get_min_x()
        + (reader->get_max_x() - reader->get_min_x()) / 2.0;
    const double centerY = reader->get_min_y()
        + (reader->get_max_y() - reader->get_min_y()) / 2.0;
    const double centerZ = reader->get_min_z()
        + (reader->get_max_z() - reader->get_min_z()) / 2.0;
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(centerZ))
        return false;

    auto loaded = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    const std::uint64_t declaredPointCount = std::max<std::uint64_t>(
        reader->header.number_of_point_records,
        reader->header.extended_number_of_point_records);
    loaded->points.reserve(static_cast<std::size_t>(
        std::min(declaredPointCount, MaxPreallocatedPoints)));

    while (reader->read_point())
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(reader->point.get_x() - centerX);
        point.y = static_cast<float>(reader->point.get_y() - centerY);
        point.z = static_cast<float>(reader->point.get_z() - centerZ);
        if (!std::isfinite(point.x)
            || !std::isfinite(point.y)
            || !std::isfinite(point.z))
        {
            continue;
        }
        point.r = static_cast<std::uint8_t>(reader->point.get_R() >> 8);
        point.g = static_cast<std::uint8_t>(reader->point.get_G() >> 8);
        point.b = static_cast<std::uint8_t>(reader->point.get_B() >> 8);
        loaded->points.push_back(point);
    }

    if (loaded->empty())
        return false;

    loaded->width = static_cast<std::uint32_t>(loaded->points.size());
    loaded->height = 1;
    loaded->is_dense = true;
    outCloud = loaded;
    if (origin)
        *origin = {centerX, centerY, centerZ};
    return true;
}
}

bool loadLasAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage,
                  std::array<double, 3>* origin)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        setError(errorMessage, QStringLiteral("LAS 文件不存在：%1").arg(path));
        return false;
    }

    if (loadLasFromStdPath(
            qstr2str(fileInfo.absoluteFilePath()), outCloud, origin))
        return true;

    // LAStools uses a narrow path API. On Windows, retry through a temporary
    // ASCII-named copy so paths containing Chinese or other Unicode text work.
    QTemporaryFile temporaryFile(
        QDir::tempPath() + QStringLiteral("/FAMP-las-XXXXXX.las"));
    temporaryFile.setAutoRemove(true);
    if (!temporaryFile.open())
    {
        setError(errorMessage,
                 QStringLiteral("无法创建 LAS 临时文件：%1")
                     .arg(temporaryFile.errorString()));
        return false;
    }

    const QString temporaryPath = temporaryFile.fileName();
    if (!copyIntoTemporaryFile(
            fileInfo.absoluteFilePath(), temporaryFile))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取 LAS 文件：%1").arg(path));
        return false;
    }

    const bool loaded = loadLasFromStdPath(
        qstr2str(temporaryPath), outCloud, origin);
    if (!loaded)
    {
        setError(errorMessage,
                 QStringLiteral("LAS 文件无效、为空或不受支持：%1").arg(path));
        return false;
    }
    return true;
}
