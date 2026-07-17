#include "ControlPoints.h"

#include <Eigen/Dense>

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace
{
constexpr qsizetype MaxPointCount = 10000;
constexpr int MaxNameLength = 128;
constexpr double MaxCoordinateMagnitude = 1.0e15;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finiteCoordinate(const famp::cloud::Point3d& point)
{
    return std::all_of(
        point.cbegin(), point.cend(), [](double value) {
            return std::isfinite(value)
                && std::abs(value) <= MaxCoordinateMagnitude;
        });
}

int geometricRank(const Eigen::MatrixXd& points)
{
    const Eigen::Vector3d center = points.rowwise().mean();
    const Eigen::MatrixXd centered = points.colwise() - center;
    const Eigen::JacobiSVD<Eigen::MatrixXd> decomposition(centered);
    const auto singularValues = decomposition.singularValues();
    if (singularValues.size() == 0 || singularValues(0) <= 0.0)
        return 0;
    const double threshold = std::max(
        1.0e-12,
        singularValues(0)
            * static_cast<double>(std::max(points.rows(), points.cols()))
            * 1.0e-12);
    int rank = 0;
    for (Eigen::Index index = 0; index < singularValues.size(); ++index)
    {
        if (singularValues(index) > threshold)
            ++rank;
    }
    return rank;
}
}

namespace famp::control
{
QString createPointId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool isValidPointId(const QString& id)
{
    const QString trimmed = id.trimmed();
    const QUuid uuid(trimmed);
    return !uuid.isNull()
        && uuid.toString(QUuid::WithoutBraces)
               .compare(trimmed, Qt::CaseInsensitive) == 0;
}

bool pointsEqual(const QVector<Point>& first,
                 const QVector<Point>& second) noexcept
{
    if (first.size() != second.size())
        return false;
    for (qsizetype index = 0; index < first.size(); ++index)
    {
        const Point& left = first.at(index);
        const Point& right = second.at(index);
        if (left.id != right.id || left.name != right.name
            || left.local != right.local || left.target != right.target
            || left.enabled != right.enabled)
        {
            return false;
        }
    }
    return true;
}

bool validatePoints(const QVector<Point>& points, QString* errorMessage)
{
    if (points.size() > MaxPointCount)
    {
        setError(errorMessage,
                 QStringLiteral("控制点数量超过 10000 个安全上限。"));
        return false;
    }

    QSet<QString> ids;
    QSet<QString> names;
    for (const Point& point : points)
    {
        const QString id = point.id.trimmed().toLower();
        const QString name = point.name.trimmed();
        const QString normalizedName = name.toCaseFolded();
        if (!isValidPointId(point.id) || ids.contains(id))
        {
            setError(errorMessage,
                     QStringLiteral("控制点 ID 无效或重复：%1")
                         .arg(point.id));
            return false;
        }
        if (name.isEmpty() || point.name.size() > MaxNameLength
            || names.contains(normalizedName))
        {
            setError(errorMessage,
                     QStringLiteral("控制点名称不能为空、重复或超过 128 个字符：%1")
                         .arg(point.name));
            return false;
        }
        if (!finiteCoordinate(point.local) || !finiteCoordinate(point.target))
        {
            setError(errorMessage,
                     QStringLiteral("控制点坐标无效或超出安全范围：%1")
                         .arg(name));
            return false;
        }
        ids.insert(id);
        names.insert(normalizedName);
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool evaluate(const QVector<Point>& points,
              const famp::cloud::SpatialReference& spatial,
              Quality& quality,
              QString* errorMessage)
{
    if (!validatePoints(points, errorMessage))
        return false;

    Quality candidate;
    long double squaredSum = 0.0L;
    long double distanceSum = 0.0L;
    for (const Point& point : points)
    {
        if (!point.enabled)
            continue;
        famp::cloud::Point3d predicted;
        QString coordinateError;
        if (!famp::cloud::localToReal(
                spatial, point.local, predicted, &coordinateError))
        {
            setError(errorMessage,
                     QStringLiteral("无法计算控制点 %1 的残差：%2")
                         .arg(point.name, coordinateError));
            return false;
        }
        Residual residual;
        residual.pointId = point.id;
        residual.pointName = point.name.trimmed();
        long double squaredDistance = 0.0L;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            residual.delta[axis] = predicted[axis] - point.target[axis];
            const long double component = residual.delta[axis];
            squaredDistance += component * component;
        }
        residual.distance = std::sqrt(static_cast<double>(squaredDistance));
        if (!std::isfinite(residual.distance))
        {
            setError(errorMessage,
                     QStringLiteral("控制点 %1 的残差超出数值范围。")
                         .arg(point.name));
            return false;
        }
        squaredSum += squaredDistance;
        distanceSum += residual.distance;
        candidate.maximum = std::max(candidate.maximum, residual.distance);
        candidate.residuals.append(residual);
    }

    candidate.enabledPointCount = candidate.residuals.size();
    if (candidate.enabledPointCount == 0)
    {
        setError(errorMessage, QStringLiteral("至少需要启用一个控制点。"));
        return false;
    }
    candidate.rootMeanSquare = std::sqrt(
        static_cast<double>(squaredSum / candidate.enabledPointCount));
    candidate.mean = static_cast<double>(
        distanceSum / candidate.enabledPointCount);
    if (!std::isfinite(candidate.rootMeanSquare)
        || !std::isfinite(candidate.mean))
    {
        setError(errorMessage, QStringLiteral("控制点残差统计超出数值范围。"));
        return false;
    }
    quality = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool transformDisplayPoint(
    const famp::cloud::SpatialReference& before,
    const famp::cloud::SpatialReference& after,
    const famp::cloud::Point3d& input,
    famp::cloud::Point3d& output,
    QString* errorMessage)
{
    if (!finiteCoordinate(input))
    {
        setError(errorMessage, QStringLiteral("点云显示坐标无效。"));
        return false;
    }
    Eigen::Matrix3d beforeLinear;
    Eigen::Matrix3d afterLinear;
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            beforeLinear(row, column) =
                before.transform[static_cast<std::size_t>(row * 4 + column)];
            afterLinear(row, column) =
                after.transform[static_cast<std::size_t>(row * 4 + column)];
        }
    }
    const Eigen::FullPivLU<Eigen::Matrix3d> decomposition(beforeLinear);
    if (!beforeLinear.allFinite() || !afterLinear.allFinite()
        || !decomposition.isInvertible())
    {
        setError(errorMessage,
                 QStringLiteral("点云显示变换无效或不可逆。"));
        return false;
    }
    const Eigen::Vector3d candidate = afterLinear
        * decomposition.inverse()
        * Eigen::Vector3d(input[0], input[1], input[2]);
    const famp::cloud::Point3d converted{
        candidate.x(), candidate.y(), candidate.z()};
    if (!candidate.allFinite() || !finiteCoordinate(converted))
    {
        setError(errorMessage,
                 QStringLiteral("点云显示变换结果超出安全范围。"));
        return false;
    }
    output = converted;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool solveRigid(const QVector<Point>& points,
                Solution& solution,
                QString* errorMessage)
{
    if (!validatePoints(points, errorMessage))
        return false;

    int enabledCount = 0;
    for (const Point& point : points)
    {
        if (point.enabled)
            ++enabledCount;
    }
    if (enabledCount < 3)
    {
        setError(errorMessage,
                 QStringLiteral("三维刚体解算至少需要 3 个启用的控制点。"));
        return false;
    }

    Eigen::MatrixXd sourceLocal(3, enabledCount);
    Eigen::MatrixXd target(3, enabledCount);
    int column = 0;
    for (const Point& point : points)
    {
        if (!point.enabled)
            continue;
        for (int axis = 0; axis < 3; ++axis)
        {
            sourceLocal(axis, column) =
                point.local[static_cast<std::size_t>(axis)];
            target(axis, column) =
                point.target[static_cast<std::size_t>(axis)];
        }
        ++column;
    }

    if (geometricRank(sourceLocal) < 2 || geometricRank(target) < 2)
    {
        setError(errorMessage,
                 QStringLiteral("启用的控制点不能全部重合或共线。"));
        return false;
    }

    const Eigen::Matrix4d localTransform =
        Eigen::umeyama(sourceLocal, target, false);
    const Eigen::Matrix3d rotation = localTransform.block<3, 3>(0, 0);
    if (!localTransform.allFinite()
        || (rotation.transpose() * rotation - Eigen::Matrix3d::Identity())
               .norm() > 1.0e-8
        || rotation.determinant() <= 0.0
        || std::abs(rotation.determinant() - 1.0) > 1.0e-8)
    {
        setError(errorMessage, QStringLiteral("控制点刚体解算结果无效。"));
        return false;
    }

    Solution candidate;
    for (int row = 0; row < 4; ++row)
    {
        for (int axis = 0; axis < 4; ++axis)
        {
            candidate.spatial.transform[static_cast<std::size_t>(row * 4 + axis)]
                = localTransform(row, axis);
        }
    }
    if (!evaluate(points, candidate.spatial, candidate.quality, errorMessage))
        return false;
    solution = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}
}
