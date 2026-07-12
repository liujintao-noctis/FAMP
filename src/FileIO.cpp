#include "FileIO.h"

#include <QLocale>
#include <QSaveFile>
#include <QTextStream>

#include <cstdint>
#include <limits>

namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool openOutputFile(QSaveFile& file, QString* errorMessage)
{
    if (file.open(QIODevice::WriteOnly))
        return true;

    setError(errorMessage,
             QStringLiteral("无法创建文件 %1：%2")
                 .arg(file.fileName(), file.errorString()));
    return false;
}
}

namespace famp::io
{
QString pathWithRequiredSuffix(const QString& path, const QString& suffix)
{
    QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty())
        return trimmedPath;

    QString normalizedSuffix = suffix.trimmed();
    while (normalizedSuffix.startsWith(QLatin1Char('.')))
        normalizedSuffix.remove(0, 1);
    if (normalizedSuffix.isEmpty())
        return trimmedPath;

    const QString suffixWithDot = QLatin1Char('.') + normalizedSuffix;
    if (trimmedPath.endsWith(suffixWithDot, Qt::CaseInsensitive))
        return trimmedPath;
    while (trimmedPath.endsWith(QLatin1Char('.')))
        trimmedPath.chop(1);
    return trimmedPath + suffixWithDot;
}

bool saveImageAtomically(const QString& path,
                         const QImage& image,
                         const char* format,
                         int quality,
                         QString* errorMessage)
{
    if (path.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("保存路径不能为空。"));
        return false;
    }
    if (image.isNull())
    {
        setError(errorMessage, QStringLiteral("没有可保存的图像数据。"));
        return false;
    }

    QSaveFile file(path);
    if (!openOutputFile(file, errorMessage))
        return false;
    if (!image.save(&file, format, quality))
    {
        file.cancelWriting();
        setError(errorMessage,
                 QStringLiteral("无法编码图像文件：%1").arg(path));
        return false;
    }
    if (!file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法完成图像文件 %1：%2")
                     .arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool savePcdAsciiAtomically(
    const QString& path,
    const pcl::PointCloud<pcl::PointXYZRGB>& cloud,
    QString* errorMessage)
{
    if (path.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("保存路径不能为空。"));
        return false;
    }
    if (cloud.empty())
    {
        setError(errorMessage, QStringLiteral("没有可保存的点云数据。"));
        return false;
    }

    QSaveFile file(path);
    if (!openOutputFile(file, errorMessage))
        return false;

    QTextStream stream(&file);
    stream.setLocale(QLocale::c());
    stream.setRealNumberNotation(QTextStream::SmartNotation);
    stream.setRealNumberPrecision(std::numeric_limits<float>::max_digits10);
    stream << "# .PCD v0.7 - Point Cloud Data file format\n"
           << "VERSION 0.7\n"
           << "FIELDS x y z rgb\n"
           << "SIZE 4 4 4 4\n"
           << "TYPE F F F U\n"
           << "COUNT 1 1 1 1\n"
           << "WIDTH " << static_cast<qulonglong>(cloud.size()) << '\n'
           << "HEIGHT 1\n"
           << "VIEWPOINT 0 0 0 1 0 0 0\n"
           << "POINTS " << static_cast<qulonglong>(cloud.size()) << '\n'
           << "DATA ascii\n";

    for (const pcl::PointXYZRGB& point : cloud)
    {
        const std::uint32_t packedRgb =
            (static_cast<std::uint32_t>(point.r) << 16)
            | (static_cast<std::uint32_t>(point.g) << 8)
            | static_cast<std::uint32_t>(point.b);
        stream << point.x << ' ' << point.y << ' ' << point.z << ' '
               << packedRgb << '\n';
    }
    stream.flush();

    if (stream.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
    {
        const QString fileError = file.errorString();
        file.cancelWriting();
        setError(errorMessage,
                 QStringLiteral("写入点云文件 %1 失败：%2")
                     .arg(path, fileError));
        return false;
    }
    if (!file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法完成点云文件 %1：%2")
                     .arg(path, file.errorString()));
        return false;
    }
    return true;
}
}
