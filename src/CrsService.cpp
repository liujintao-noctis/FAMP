#include "CrsService.h"

#include <proj.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cmath>
#include <memory>
#include <vector>

namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

using ContextPtr = std::unique_ptr<PJ_CONTEXT, decltype(&proj_context_destroy)>;
using ProjPtr = std::unique_ptr<PJ, decltype(&proj_destroy)>;

ContextPtr createContext()
{
    ContextPtr context(proj_context_create(), &proj_context_destroy);
    if (!context)
        return context;

    proj_context_set_enable_network(context.get(), 0);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(applicationDirectory).absoluteFilePath(QStringLiteral("share/proj")),
        QDir(applicationDirectory).absoluteFilePath(QStringLiteral("../share/proj"))};
    for (const QString& candidate : candidates)
    {
        if (!QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("proj.db"))))
            continue;

        const QByteArray encodedPath = QFile::encodeName(QDir::cleanPath(candidate));
        const char* searchPath = encodedPath.constData();
        proj_context_set_search_paths(context.get(), 1, &searchPath);
        break;
    }
    return context;
}

QString typeName(PJ_TYPE type)
{
    switch (type)
    {
    case PJ_TYPE_GEOGRAPHIC_2D_CRS:
        return QStringLiteral("二维地理坐标系");
    case PJ_TYPE_GEOGRAPHIC_3D_CRS:
        return QStringLiteral("三维地理坐标系");
    case PJ_TYPE_PROJECTED_CRS:
        return QStringLiteral("投影坐标系");
    case PJ_TYPE_GEOCENTRIC_CRS:
        return QStringLiteral("地心坐标系");
    case PJ_TYPE_VERTICAL_CRS:
        return QStringLiteral("垂向坐标系");
    case PJ_TYPE_COMPOUND_CRS:
        return QStringLiteral("复合坐标系");
    default:
        return QStringLiteral("其他坐标系");
    }
}

QString projError(PJ* operation)
{
    const int errorCode = proj_errno(operation);
    const char* message = proj_errno_string(errorCode);
    return message
        ? QString::fromUtf8(message)
        : QStringLiteral("未知 PROJ 错误");
}
}

namespace famp::crs
{
QString runtimeVersion()
{
    const PJ_INFO info = proj_info();
    return info.version ? QString::fromLatin1(info.version) : QStringLiteral("未知");
}

QString normalizedEpsg(const QString& identifier)
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(?:EPSG\\s*:\\s*)?([1-9][0-9]{2,7})\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(identifier);
    if (!match.hasMatch())
        return {};
    return QStringLiteral("EPSG:%1").arg(match.captured(1));
}

bool inspect(const QString& identifier, Info& info, QString* errorMessage)
{
    const QString normalized = normalizedEpsg(identifier);
    if (normalized.isEmpty())
    {
        setError(errorMessage, QStringLiteral("请输入有效的 EPSG 编码，例如 EPSG:4490。"));
        return false;
    }

    ContextPtr context = createContext();
    if (!context)
    {
        setError(errorMessage, QStringLiteral("无法初始化 PROJ。"));
        return false;
    }
    const QByteArray encoded = normalized.toUtf8();
    ProjPtr crs(proj_create(context.get(), encoded.constData()), &proj_destroy);
    if (!crs || !proj_is_crs(crs.get()))
    {
        setError(errorMessage,
                 QStringLiteral("PROJ 无法识别坐标系 %1。").arg(normalized));
        return false;
    }

    Info inspected;
    inspected.identifier = normalized;
    const char* name = proj_get_name(crs.get());
    inspected.name = name ? QString::fromUtf8(name) : normalized;
    const PJ_TYPE type = proj_get_type(crs.get());
    inspected.type = typeName(type);
    inspected.geographic = type == PJ_TYPE_GEOGRAPHIC_2D_CRS
        || type == PJ_TYPE_GEOGRAPHIC_3D_CRS;
    info = inspected;
    return true;
}

bool transform(const QString& sourceIdentifier,
               const QString& targetIdentifier,
               const Coordinate& source,
               Coordinate& target,
               QString* errorMessage)
{
    const QString sourceCrs = normalizedEpsg(sourceIdentifier);
    const QString targetCrs = normalizedEpsg(targetIdentifier);
    if (sourceCrs.isEmpty() || targetCrs.isEmpty())
    {
        setError(errorMessage, QStringLiteral("源坐标系和目标坐标系必须是有效的 EPSG 编码。"));
        return false;
    }
    if (!std::isfinite(source.x)
        || !std::isfinite(source.y)
        || !std::isfinite(source.z))
    {
        setError(errorMessage, QStringLiteral("待转换坐标必须是有限数值。"));
        return false;
    }

    ContextPtr context = createContext();
    if (!context)
    {
        setError(errorMessage, QStringLiteral("无法初始化 PROJ。"));
        return false;
    }
    const QByteArray sourceEncoded = sourceCrs.toUtf8();
    const QByteArray targetEncoded = targetCrs.toUtf8();
    ProjPtr rawOperation(
        proj_create_crs_to_crs(context.get(),
                               sourceEncoded.constData(),
                               targetEncoded.constData(),
                               nullptr),
        &proj_destroy);
    if (!rawOperation)
    {
        setError(errorMessage,
                 QStringLiteral("无法创建 %1 到 %2 的坐标转换。")
                     .arg(sourceCrs, targetCrs));
        return false;
    }

    ProjPtr operation(
        proj_normalize_for_visualization(context.get(), rawOperation.get()),
        &proj_destroy);
    if (!operation)
    {
        setError(errorMessage, QStringLiteral("无法规范化 PROJ 坐标轴顺序。"));
        return false;
    }

    proj_errno_reset(operation.get());
    const PJ_COORD transformed = proj_trans(
        operation.get(), PJ_FWD, proj_coord(source.x, source.y, source.z, 0.0));
    if (proj_errno(operation.get()) != 0
        || !std::isfinite(transformed.xyz.x)
        || !std::isfinite(transformed.xyz.y)
        || !std::isfinite(transformed.xyz.z))
    {
        setError(errorMessage,
                 QStringLiteral("坐标转换失败：%1").arg(projError(operation.get())));
        return false;
    }

    Coordinate result;
    result.x = transformed.xyz.x;
    result.y = transformed.xyz.y;
    result.z = transformed.xyz.z;
    target = result;
    return true;
}
}
