#include "CrsService.h"

#include <proj.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cmath>
#include <algorithm>
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
class Transformer::Impl
{
public:
    ContextPtr context{nullptr, &proj_context_destroy};
    ProjPtr operation{nullptr, &proj_destroy};
    QString source;
    QString target;
};

Transformer::Transformer()
    : impl_(std::make_unique<Impl>())
{
}

Transformer::~Transformer() = default;
Transformer::Transformer(Transformer&& other) noexcept = default;
Transformer& Transformer::operator=(Transformer&& other) noexcept = default;

bool Transformer::initialize(const QString& sourceIdentifier,
                             const QString& targetIdentifier,
                             QString* errorMessage)
{
    const QString sourceCrs = normalizedEpsg(sourceIdentifier);
    const QString targetCrs = normalizedEpsg(targetIdentifier);
    if (sourceCrs.isEmpty() || targetCrs.isEmpty())
    {
        setError(errorMessage, QStringLiteral("源坐标系和目标坐标系必须是有效的 EPSG 编码。"));
        return false;
    }

    auto candidate = std::make_unique<Impl>();
    candidate->context = createContext();
    if (!candidate->context)
    {
        setError(errorMessage, QStringLiteral("无法初始化 PROJ。"));
        return false;
    }

    const QByteArray sourceEncoded = sourceCrs.toUtf8();
    const QByteArray targetEncoded = targetCrs.toUtf8();
    ProjPtr rawOperation(
        proj_create_crs_to_crs(candidate->context.get(),
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

    candidate->operation.reset(proj_normalize_for_visualization(
        candidate->context.get(), rawOperation.get()));
    if (!candidate->operation)
    {
        setError(errorMessage, QStringLiteral("无法规范化 PROJ 坐标轴顺序。"));
        return false;
    }
    candidate->source = sourceCrs;
    candidate->target = targetCrs;
    impl_ = std::move(candidate);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool Transformer::transform(const Coordinate& source,
                            Coordinate& target,
                            QString* errorMessage) const
{
    if (!isValid())
    {
        setError(errorMessage, QStringLiteral("坐标转换器尚未初始化。"));
        return false;
    }
    if (!std::isfinite(source.x)
        || !std::isfinite(source.y)
        || !std::isfinite(source.z))
    {
        setError(errorMessage, QStringLiteral("待转换坐标必须是有限数值。"));
        return false;
    }

    proj_errno_reset(impl_->operation.get());
    const PJ_COORD transformed = proj_trans(
        impl_->operation.get(), PJ_FWD,
        proj_coord(source.x, source.y, source.z, 0.0));
    if (proj_errno(impl_->operation.get()) != 0
        || !std::isfinite(transformed.xyz.x)
        || !std::isfinite(transformed.xyz.y)
        || !std::isfinite(transformed.xyz.z))
    {
        setError(errorMessage,
                 QStringLiteral("坐标转换失败：%1")
                     .arg(projError(impl_->operation.get())));
        return false;
    }

    target = Coordinate{
        transformed.xyz.x, transformed.xyz.y, transformed.xyz.z};
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool Transformer::isValid() const
{
    return impl_ && impl_->context && impl_->operation;
}

QString Transformer::sourceIdentifier() const
{
    return impl_ ? impl_->source : QString();
}

QString Transformer::targetIdentifier() const
{
    return impl_ ? impl_->target : QString();
}

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
    inspected.projected = type == PJ_TYPE_PROJECTED_CRS;
    if (inspected.projected)
    {
        ProjPtr coordinateSystem(
            proj_crs_get_coordinate_system(context.get(), crs.get()),
            &proj_destroy);
        const char* unitName = nullptr;
        double unitToMetre = 0.0;
        double secondAxisUnitToMetre = 0.0;
        if (!coordinateSystem
            || proj_cs_get_axis_count(context.get(), coordinateSystem.get()) < 2
            || !proj_cs_get_axis_info(
                context.get(), coordinateSystem.get(), 0,
                nullptr, nullptr, nullptr, &unitToMetre, &unitName,
                nullptr, nullptr)
            || !proj_cs_get_axis_info(
                context.get(), coordinateSystem.get(), 1,
                nullptr, nullptr, nullptr, &secondAxisUnitToMetre, nullptr,
                nullptr, nullptr)
            || !std::isfinite(unitToMetre) || unitToMetre <= 0.0
            || !std::isfinite(secondAxisUnitToMetre)
            || secondAxisUnitToMetre <= 0.0
            || std::abs(unitToMetre - secondAxisUnitToMetre)
                > 1.0e-12 * std::max(
                    {1.0, std::abs(unitToMetre),
                     std::abs(secondAxisUnitToMetre)}))
        {
            setError(errorMessage,
                     QStringLiteral("无法读取投影坐标系 %1 的水平单位。")
                         .arg(normalized));
            return false;
        }
        inspected.horizontalUnitName = unitName
            ? QString::fromUtf8(unitName) : QStringLiteral("未知单位");
        inspected.horizontalUnitToMetre = unitToMetre;
    }
    info = inspected;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool transform(const QString& sourceIdentifier,
               const QString& targetIdentifier,
               const Coordinate& source,
               Coordinate& target,
               QString* errorMessage)
{
    Transformer transformer;
    return transformer.initialize(
               sourceIdentifier, targetIdentifier, errorMessage)
        && transformer.transform(source, target, errorMessage);
}
}
