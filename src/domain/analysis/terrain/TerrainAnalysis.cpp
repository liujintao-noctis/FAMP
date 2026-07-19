#include "TerrainAnalysis.h"

#include <Eigen/Dense>

#include <pcl/kdtree/kdtree_flann.h>

#include <QByteArray>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t CancellationInterval = 4096;
constexpr qsizetype MaximumSpacingSamples = 4096;
constexpr int NearestNeighbourCandidates = 12;
constexpr double MaximumCoordinateMagnitude = 1.0e15;
constexpr double KeyScale = 1.0e9;

using Point2d = std::array<double, 2>;

struct IndexedElevation
{
    quint64 cellIndex = 0;
    double elevation = 0.0;
};

struct Segment
{
    Point2d first{0.0, 0.0};
    Point2d second{0.0, 0.0};
};

struct GraphEdge
{
    int first = -1;
    int second = -1;
    bool used = false;
};

struct GraphNode
{
    Point2d point{0.0, 0.0};
    QVector<int> edges;
};

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool cancelled(const famp::tasks::CancellationCheck& shouldCancel)
{
    return famp::tasks::isCancellationRequested(shouldCancel);
}

bool finiteCoordinate(const famp::cloud::Point3d& point)
{
    return std::all_of(point.cbegin(), point.cend(), [](double value) {
        return std::isfinite(value)
            && std::abs(value) <= MaximumCoordinateMagnitude;
    });
}

bool finitePoint(const Point2d& point)
{
    return std::isfinite(point[0]) && std::isfinite(point[1]);
}

bool spatialMatrix(const famp::cloud::SpatialReference& spatial,
                   Eigen::Matrix4d& matrix)
{
    for (double value : spatial.origin)
    {
        if (!std::isfinite(value))
            return false;
    }
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const double value = spatial.transform[static_cast<std::size_t>(
                row * 4 + column)];
            if (!std::isfinite(value))
                return false;
            matrix(row, column) = value;
        }
    }
    return true;
}

bool realCoordinate(const Eigen::Matrix4d& matrix,
                    const famp::cloud::SpatialReference& spatial,
                    const pcl::PointXYZRGB& point,
                    famp::cloud::Point3d& coordinate)
{
    const Eigen::Vector4d transformed = matrix * Eigen::Vector4d(
        static_cast<double>(point.x) + spatial.origin[0],
        static_cast<double>(point.y) + spatial.origin[1],
        static_cast<double>(point.z) + spatial.origin[2],
        1.0);
    if (!transformed.allFinite() || std::abs(transformed.w()) < 1.0e-12)
        return false;
    coordinate = {
        transformed.x() / transformed.w(),
        transformed.y() / transformed.w(),
        transformed.z() / transformed.w()};
    return finiteCoordinate(coordinate);
}

double median(QVector<double> values)
{
    if (values.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const qsizetype middle = values.size() / 2;
    if ((values.size() % 2) == 1)
        return values.at(middle);
    return (values.at(middle - 1) + values.at(middle)) / 2.0;
}

bool computeDimensions(double minimum,
                       double maximum,
                       double resolution,
                       double& origin,
                       int& count)
{
    origin = std::floor(minimum / resolution) * resolution;
    if (!std::isfinite(origin))
        return false;
    const long double cells = std::floor(
        (static_cast<long double>(maximum)
         - static_cast<long double>(origin))
        / static_cast<long double>(resolution)) + 1.0L;
    if (!std::isfinite(static_cast<double>(cells))
        || cells < 1.0L
        || cells > static_cast<long double>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    count = static_cast<int>(cells);
    return true;
}

bool fillSmallHoles(famp::terrain::Grid& grid,
                    int maximumHoleCells,
                    const famp::tasks::CancellationCheck& shouldCancel,
                    QString* errorMessage)
{
    if (maximumHoleCells <= 0)
        return true;
    const qsizetype cellCount = grid.elevations.size();
    QByteArray visited(cellCount, '\0');
    grid.filledCellCount = 0;
    const int rowOffsets[4]{-1, 1, 0, 0};
    const int columnOffsets[4]{0, 0, -1, 1};

    for (qsizetype start = 0; start < cellCount; ++start)
    {
        if ((start % static_cast<qsizetype>(CancellationInterval)) == 0
            && cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("DEM 小空洞填补已取消。"));
            return false;
        }
        if (visited[start] || std::isfinite(grid.elevations.at(start)))
            continue;

        QVector<qsizetype> component;
        component.reserve(maximumHoleCells + 1);
        std::queue<qsizetype> pending;
        pending.push(start);
        visited[start] = 1;
        bool touchesBoundary = false;
        bool tooLarge = false;
        qsizetype processedCells = 0;
        while (!pending.empty())
        {
            if ((processedCells++
                 % static_cast<qsizetype>(CancellationInterval)) == 0
                && cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("DEM 小空洞填补已取消。"));
                return false;
            }
            const qsizetype current = pending.front();
            pending.pop();
            if (component.size() <= maximumHoleCells)
                component.append(current);
            else
                tooLarge = true;
            const int row = static_cast<int>(current / grid.columns);
            const int column = static_cast<int>(current % grid.columns);
            touchesBoundary = touchesBoundary
                || row == 0 || row == grid.rows - 1
                || column == 0 || column == grid.columns - 1;
            for (int neighbour = 0; neighbour < 4; ++neighbour)
            {
                const int nextRow = row + rowOffsets[neighbour];
                const int nextColumn = column + columnOffsets[neighbour];
                if (nextRow < 0 || nextRow >= grid.rows
                    || nextColumn < 0 || nextColumn >= grid.columns)
                {
                    continue;
                }
                const qsizetype next = grid.index(nextRow, nextColumn);
                if (visited[next]
                    || std::isfinite(grid.elevations.at(next)))
                {
                    continue;
                }
                visited[next] = 1;
                pending.push(next);
            }
        }

        if (touchesBoundary || tooLarge
            || component.size() > maximumHoleCells)
            continue;

        QVector<double> neighbours;
        for (qsizetype cell : component)
        {
            const int row = static_cast<int>(cell / grid.columns);
            const int column = static_cast<int>(cell % grid.columns);
            for (int rowDelta = -1; rowDelta <= 1; ++rowDelta)
            {
                for (int columnDelta = -1; columnDelta <= 1; ++columnDelta)
                {
                    if (rowDelta == 0 && columnDelta == 0)
                        continue;
                    const int nextRow = row + rowDelta;
                    const int nextColumn = column + columnDelta;
                    if (nextRow < 0 || nextRow >= grid.rows
                        || nextColumn < 0 || nextColumn >= grid.columns)
                    {
                        continue;
                    }
                    const double value = grid.value(nextRow, nextColumn);
                    if (std::isfinite(value))
                        neighbours.append(value);
                }
            }
        }
        const double replacement = median(neighbours);
        if (!std::isfinite(replacement))
            continue;
        for (qsizetype cell : component)
            grid.elevations[cell] = replacement;
        grid.filledCellCount += component.size();
    }
    return true;
}

double niceInterval(double raw)
{
    if (!std::isfinite(raw) || raw <= 0.0)
        return 1.0;
    const double exponent = std::floor(std::log10(raw));
    const double scale = std::pow(10.0, exponent);
    const double normalized = raw / scale;
    double nice = 1.0;
    if (normalized > 5.0)
        nice = 10.0;
    else if (normalized > 2.0)
        nice = 5.0;
    else if (normalized > 1.0)
        nice = 2.0;
    return nice * scale;
}

Point2d interpolate(const Point2d& first,
                    const Point2d& second,
                    double firstValue,
                    double secondValue,
                    double level)
{
    const double difference = secondValue - firstValue;
    double amount = std::abs(difference) < 1.0e-15
        ? 0.5 : (level - firstValue) / difference;
    amount = std::clamp(amount, 0.0, 1.0);
    return {
        first[0] + amount * (second[0] - first[0]),
        first[1] + amount * (second[1] - first[1])};
}

void triangleSegment(const std::array<Point2d, 3>& points,
                     const std::array<double, 3>& values,
                     double level,
                     QVector<Segment>& output)
{
    QVector<Point2d> intersections;
    intersections.reserve(2);
    const int edges[3][2]{{0, 1}, {1, 2}, {2, 0}};
    for (const auto& edge : edges)
    {
        const bool firstHigh = values[edge[0]] >= level;
        const bool secondHigh = values[edge[1]] >= level;
        if (firstHigh == secondHigh)
            continue;
        intersections.append(interpolate(
            points[edge[0]], points[edge[1]],
            values[edge[0]], values[edge[1]], level));
    }
    if (intersections.size() != 2)
        return;
    const double dx = intersections[0][0] - intersections[1][0];
    const double dy = intersections[0][1] - intersections[1][1];
    if (dx * dx + dy * dy <= 1.0e-24)
        return;
    output.append(Segment{intersections[0], intersections[1]});
}

std::pair<qint64, qint64> pointKey(const Point2d& point,
                                   const famp::terrain::Grid& grid)
{
    return {
        static_cast<qint64>(std::llround(
            ((point[0] - grid.originX) / grid.resolution) * KeyScale)),
        static_cast<qint64>(std::llround(
            ((point[1] - grid.originY) / grid.resolution) * KeyScale))};
}

QVector<Point2d> smoothLine(QVector<Point2d> points, int iterations)
{
    if (iterations <= 0 || points.size() < 3)
        return points;
    const bool closed = points.front() == points.back();
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        if (points.size() > 500'000)
            break;
        QVector<Point2d> smoothed;
        smoothed.reserve(points.size() * 2);
        const qsizetype segmentCount = points.size() - 1;
        if (!closed)
            smoothed.append(points.front());
        for (qsizetype index = 0; index < segmentCount; ++index)
        {
            const Point2d& first = points.at(index);
            const Point2d& second = points.at(index + 1);
            smoothed.append(Point2d{
                first[0] * 0.75 + second[0] * 0.25,
                first[1] * 0.75 + second[1] * 0.25});
            smoothed.append(Point2d{
                first[0] * 0.25 + second[0] * 0.75,
                first[1] * 0.25 + second[1] * 0.75});
        }
        if (closed)
            smoothed.append(smoothed.front());
        else
            smoothed.append(points.back());
        points = smoothed;
    }
    return points;
}

QVector<famp::terrain::ContourLine> stitchSegments(
    const famp::terrain::Grid& grid,
    double elevation,
    const QVector<Segment>& segments,
    int smoothingIterations)
{
    QVector<GraphNode> nodes;
    QVector<GraphEdge> edges;
    std::map<std::pair<qint64, qint64>, int> nodeIndices;
    auto nodeIndex = [&](const Point2d& point) {
        const auto key = pointKey(point, grid);
        const auto found = nodeIndices.find(key);
        if (found != nodeIndices.end())
            return found->second;
        const int index = nodes.size();
        nodes.append(GraphNode{point, {}});
        nodeIndices.emplace(key, index);
        return index;
    };

    edges.reserve(segments.size());
    for (const Segment& segment : segments)
    {
        const int first = nodeIndex(segment.first);
        const int second = nodeIndex(segment.second);
        if (first == second)
            continue;
        const int edgeIndex = edges.size();
        edges.append(GraphEdge{first, second, false});
        nodes[first].edges.append(edgeIndex);
        nodes[second].edges.append(edgeIndex);
    }

    QVector<famp::terrain::ContourLine> lines;
    auto trace = [&](int startNode, int startEdge) {
        QVector<Point2d> points;
        int currentNode = startNode;
        int currentEdge = startEdge;
        points.append(nodes.at(currentNode).point);
        while (currentEdge >= 0 && !edges[currentEdge].used)
        {
            GraphEdge& edge = edges[currentEdge];
            edge.used = true;
            currentNode = edge.first == currentNode
                ? edge.second : edge.first;
            points.append(nodes.at(currentNode).point);
            int nextEdge = -1;
            for (int candidate : nodes.at(currentNode).edges)
            {
                if (!edges.at(candidate).used)
                {
                    nextEdge = candidate;
                    break;
                }
            }
            currentEdge = nextEdge;
        }
        if (points.size() >= 2)
        {
            famp::terrain::ContourLine line;
            line.elevation = elevation;
            line.points = smoothLine(
                std::move(points), smoothingIterations);
            lines.append(std::move(line));
        }
    };

    for (int node = 0; node < nodes.size(); ++node)
    {
        if (nodes.at(node).edges.size() == 2)
            continue;
        for (int edge : nodes.at(node).edges)
        {
            if (!edges.at(edge).used)
                trace(node, edge);
        }
    }
    for (int edge = 0; edge < edges.size(); ++edge)
    {
        if (!edges.at(edge).used)
            trace(edges.at(edge).first, edge);
    }
    return lines;
}
}

namespace famp::terrain
{
bool Grid::isValid() const
{
    if (columns <= 0 || rows <= 0
        || !std::isfinite(originX) || !std::isfinite(originY)
        || !std::isfinite(resolution) || resolution <= 0.0
        || !std::isfinite(horizontalUnitToMetre)
        || horizontalUnitToMetre <= 0.0
        || statistic < CellStatistic::Minimum
        || statistic > CellStatistic::Median)
    {
        return false;
    }
    const quint64 expected = static_cast<quint64>(columns)
        * static_cast<quint64>(rows);
    if (expected != static_cast<quint64>(elevations.size()))
        return false;
    int finiteCount = 0;
    for (double elevation : elevations)
    {
        if (std::isfinite(elevation))
            ++finiteCount;
        else if (!std::isnan(elevation))
            return false;
    }
    return finiteCount > 0
        && sourcePointCount >= 0
        && populatedCellCount >= 0
        && filledCellCount >= 0
        && sourcePointCount >= populatedCellCount
        && populatedCellCount + filledCellCount == finiteCount;
}

qsizetype Grid::index(int row, int column) const
{
    if (row < 0 || row >= rows || column < 0 || column >= columns)
        return -1;
    return static_cast<qsizetype>(row) * columns + column;
}

double Grid::value(int row, int column) const
{
    const qsizetype offset = index(row, column);
    return offset >= 0 && offset < elevations.size()
        ? elevations.at(offset)
        : std::numeric_limits<double>::quiet_NaN();
}

std::array<double, 2> Grid::cellCenter(int row, int column) const
{
    return {
        originX + (static_cast<double>(column) + 0.5) * resolution,
        originY + (static_cast<double>(row) + 0.5) * resolution};
}

QString statisticName(CellStatistic statistic)
{
    switch (statistic)
    {
    case CellStatistic::Minimum:
        return QStringLiteral("最低值");
    case CellStatistic::Maximum:
        return QStringLiteral("最高值");
    case CellStatistic::Mean:
        return QStringLiteral("平均值");
    case CellStatistic::Median:
        return QStringLiteral("中位数");
    }
    return QStringLiteral("未知");
}

bool validateGridOptions(const GridOptions& options, QString* errorMessage)
{
    if (!std::isfinite(options.horizontalUnitToMetre)
        || options.horizontalUnitToMetre <= 0.0
        || options.horizontalUnitToMetre > 1.0e12)
    {
        setError(errorMessage, QStringLiteral("DEM 水平坐标单位无效。"));
        return false;
    }
    if (!options.automaticResolution
        && (!std::isfinite(options.resolution)
            || options.resolution <= 0.0))
    {
        setError(errorMessage, QStringLiteral("DEM 网格分辨率必须大于 0。"));
        return false;
    }
    if (options.maximumHoleCells < 0 || options.maximumHoleCells > 3)
    {
        setError(errorMessage, QStringLiteral("DEM 小空洞最多只能填补 3 个连通网格。"));
        return false;
    }
    if (options.maximumCellCount == 0
        || options.maximumCellCount > 100'000'000)
    {
        setError(errorMessage, QStringLiteral("DEM 网格数安全上限无效。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool validateContourOptions(const ContourOptions& options,
                            QString* errorMessage)
{
    if (!options.automaticInterval
        && (!std::isfinite(options.interval) || options.interval <= 0.0))
    {
        setError(errorMessage, QStringLiteral("等高距必须大于 0。"));
        return false;
    }
    if (!options.automaticBase && !std::isfinite(options.baseElevation))
    {
        setError(errorMessage, QStringLiteral("等高线基准高程无效。"));
        return false;
    }
    if (options.smoothingIterations < 0
        || options.smoothingIterations > 3)
    {
        setError(errorMessage, QStringLiteral("等高线平滑次数必须在 0–3 之间。"));
        return false;
    }
    if (options.maximumSegmentCount == 0
        || options.maximumSegmentCount > 20'000'000)
    {
        setError(errorMessage, QStringLiteral("等高线线段数安全上限无效。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool suggestResolution(
    const QVector<famp::cloud::Point3d>& coordinates,
    double horizontalUnitToMetre,
    double& resolution,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (coordinates.size() < 3
        || !std::isfinite(horizontalUnitToMetre)
        || horizontalUnitToMetre <= 0.0)
    {
        setError(errorMessage, QStringLiteral("至少需要 3 个有效点才能估算 DEM 分辨率。"));
        return false;
    }
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("DEM 分辨率估算已取消。"));
        return false;
    }

    long double sumX = 0.0L;
    long double sumY = 0.0L;
    for (const auto& coordinate : coordinates)
    {
        if (!finiteCoordinate(coordinate))
        {
            setError(errorMessage, QStringLiteral("DEM 输入包含无效坐标。"));
            return false;
        }
        sumX += coordinate[0];
        sumY += coordinate[1];
    }
    const double centerX = static_cast<double>(sumX / coordinates.size());
    const double centerY = static_cast<double>(sumY / coordinates.size());
    std::vector<std::pair<float, float>> uniqueCoordinates;
    uniqueCoordinates.reserve(static_cast<std::size_t>(coordinates.size()));
    for (qsizetype index = 0; index < coordinates.size(); ++index)
    {
        if ((index % static_cast<qsizetype>(CancellationInterval)) == 0
            && cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("DEM 分辨率估算已取消。"));
            return false;
        }
        const double x = coordinates.at(index)[0] - centerX;
        const double y = coordinates.at(index)[1] - centerY;
        if (std::abs(x) > std::numeric_limits<float>::max()
            || std::abs(y) > std::numeric_limits<float>::max())
        {
            setError(errorMessage, QStringLiteral("DEM 坐标范围过大，无法估算点间距。"));
            return false;
        }
        uniqueCoordinates.emplace_back(
            static_cast<float>(x), static_cast<float>(y));
    }
    std::sort(uniqueCoordinates.begin(), uniqueCoordinates.end());
    uniqueCoordinates.erase(
        std::unique(uniqueCoordinates.begin(), uniqueCoordinates.end()),
        uniqueCoordinates.end());
    if (uniqueCoordinates.size() < 2)
    {
        setError(errorMessage, QStringLiteral("点云水平位置重复，无法估算 DEM 分辨率。"));
        return false;
    }
    auto cloud = pcl::PointCloud<pcl::PointXYZ>::Ptr(
        new pcl::PointCloud<pcl::PointXYZ>);
    cloud->resize(uniqueCoordinates.size());
    for (std::size_t index = 0; index < uniqueCoordinates.size(); ++index)
    {
        cloud->points[index] = pcl::PointXYZ(
            uniqueCoordinates[index].first,
            uniqueCoordinates[index].second,
            0.0F);
    }

    pcl::KdTreeFLANN<pcl::PointXYZ> tree;
    tree.setInputCloud(cloud);
    const qsizetype uniqueCount = static_cast<qsizetype>(cloud->size());
    const qsizetype sampleCount = std::min(
        uniqueCount, MaximumSpacingSamples);
    QVector<double> distances;
    distances.reserve(sampleCount);
    const int candidateCount = static_cast<int>(std::min<qsizetype>(
        NearestNeighbourCandidates, uniqueCount));
    std::vector<int> indices(static_cast<std::size_t>(candidateCount));
    std::vector<float> squaredDistances(
        static_cast<std::size_t>(candidateCount));
    for (qsizetype sample = 0; sample < sampleCount; ++sample)
    {
        if ((sample % 256) == 0 && cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("DEM 分辨率估算已取消。"));
            return false;
        }
        const qsizetype index = sampleCount == 1
            ? 0
            : sample * (uniqueCount - 1) / (sampleCount - 1);
        const int found = tree.nearestKSearch(
            cloud->points[static_cast<std::size_t>(index)],
            candidateCount, indices, squaredDistances);
        for (int neighbour = 1; neighbour < found; ++neighbour)
        {
            if (squaredDistances[static_cast<std::size_t>(neighbour)] > 0.0F)
            {
                distances.append(std::sqrt(static_cast<double>(
                    squaredDistances[static_cast<std::size_t>(neighbour)])));
                break;
            }
        }
    }
    const double spacing = median(distances);
    if (!std::isfinite(spacing) || spacing <= 0.0)
    {
        setError(errorMessage, QStringLiteral("点云水平位置重复，无法估算 DEM 分辨率。"));
        return false;
    }
    resolution = std::max(0.01 / horizontalUnitToMetre, 2.0 * spacing);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool buildGrid(
    const QVector<famp::cloud::Point3d>& coordinates,
    const GridOptions& options,
    Grid& grid,
    double* suggestedResolutionOutput,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    if (!validateGridOptions(options, errorMessage))
        return false;
    if (coordinates.size() < 3)
    {
        setError(errorMessage, QStringLiteral("至少需要 3 个有效点才能生成 DEM。"));
        return false;
    }
    if (coordinates.size() > std::numeric_limits<int>::max())
    {
        setError(errorMessage, QStringLiteral("DEM 输入点数超出可表示范围。"));
        return false;
    }
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
        return false;
    }

    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (qsizetype index = 0; index < coordinates.size(); ++index)
    {
        if ((index % static_cast<qsizetype>(CancellationInterval)) == 0)
        {
            if (cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
                return false;
            }
            if (reportProgress)
                reportProgress(0.1 * static_cast<double>(index)
                               / coordinates.size());
        }
        const auto& coordinate = coordinates.at(index);
        if (!finiteCoordinate(coordinate))
        {
            setError(errorMessage, QStringLiteral("DEM 输入包含无效坐标。"));
            return false;
        }
        minimumX = std::min(minimumX, coordinate[0]);
        maximumX = std::max(maximumX, coordinate[0]);
        minimumY = std::min(minimumY, coordinate[1]);
        maximumY = std::max(maximumY, coordinate[1]);
    }
    if (maximumX <= minimumX || maximumY <= minimumY)
    {
        setError(errorMessage, QStringLiteral("DEM 输入点云的水平范围退化为直线或单点。"));
        return false;
    }

    double suggested = 0.0;
    if (options.automaticResolution || suggestedResolutionOutput)
    {
        QString suggestionError;
        if (!suggestResolution(
                coordinates, options.horizontalUnitToMetre,
                suggested, &suggestionError, shouldCancel))
        {
            if (options.automaticResolution || cancelled(shouldCancel))
            {
                setError(errorMessage, suggestionError);
                return false;
            }
            suggested = 0.0;
        }
    }
    const double resolution = options.automaticResolution
        ? suggested : options.resolution;
    if (!std::isfinite(resolution) || resolution <= 0.0)
    {
        setError(errorMessage, QStringLiteral("DEM 网格分辨率无效。"));
        return false;
    }
    const double coordinateMagnitude = std::max(
        {std::abs(minimumX), std::abs(maximumX),
         std::abs(minimumY), std::abs(maximumY)});
    if (coordinateMagnitude > 0.0
        && coordinateMagnitude + resolution == coordinateMagnitude)
    {
        setError(errorMessage,
                 QStringLiteral("DEM 分辨率相对坐标量级过小，双精度数值无法区分相邻网格。"));
        return false;
    }
    if (suggestedResolutionOutput)
        *suggestedResolutionOutput = suggested;
    if (reportProgress)
        reportProgress(0.2);

    Grid candidate;
    candidate.resolution = resolution;
    candidate.horizontalUnitToMetre = options.horizontalUnitToMetre;
    candidate.statistic = options.statistic;
    candidate.sourcePointCount = coordinates.size();
    if (!computeDimensions(
            minimumX, maximumX, resolution,
            candidate.originX, candidate.columns)
        || !computeDimensions(
            minimumY, maximumY, resolution,
            candidate.originY, candidate.rows))
    {
        setError(errorMessage, QStringLiteral("DEM 网格范围超出可表示范围。"));
        return false;
    }
    const quint64 cellCount = static_cast<quint64>(candidate.columns)
        * static_cast<quint64>(candidate.rows);
    if (cellCount > options.maximumCellCount
        || cellCount > static_cast<quint64>(
            std::numeric_limits<qsizetype>::max()))
    {
        setError(errorMessage,
                 QStringLiteral("DEM 将生成 %1 个网格，超过 %2 个安全上限；请增大分辨率。")
                     .arg(cellCount)
                     .arg(options.maximumCellCount));
        return false;
    }

    QVector<IndexedElevation> samples;
    samples.reserve(coordinates.size());
    for (qsizetype pointIndex = 0;
         pointIndex < coordinates.size(); ++pointIndex)
    {
        if ((pointIndex % static_cast<qsizetype>(CancellationInterval)) == 0)
        {
            if (cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
                return false;
            }
            if (reportProgress)
            {
                reportProgress(0.2 + 0.15
                    * static_cast<double>(pointIndex) / coordinates.size());
            }
        }
        const auto& coordinate = coordinates.at(pointIndex);
        int column = static_cast<int>(std::floor(
            (coordinate[0] - candidate.originX) / resolution));
        int row = static_cast<int>(std::floor(
            (coordinate[1] - candidate.originY) / resolution));
        column = std::clamp(column, 0, candidate.columns - 1);
        row = std::clamp(row, 0, candidate.rows - 1);
        samples.append(IndexedElevation{
            static_cast<quint64>(row) * candidate.columns
                + static_cast<quint64>(column),
            coordinate[2]});
    }
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
        return false;
    }
    std::sort(samples.begin(), samples.end(),
              [](const IndexedElevation& first,
                 const IndexedElevation& second) {
        return first.cellIndex < second.cellIndex
            || (first.cellIndex == second.cellIndex
                && first.elevation < second.elevation);
    });
    if (reportProgress)
        reportProgress(0.5);

    candidate.elevations.fill(
        std::numeric_limits<double>::quiet_NaN(),
        static_cast<qsizetype>(cellCount));
    qsizetype begin = 0;
    while (begin < samples.size())
    {
        if (cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
            return false;
        }
        qsizetype end = begin + 1;
        while (end < samples.size()
               && samples.at(end).cellIndex == samples.at(begin).cellIndex)
        {
            if ((end % static_cast<qsizetype>(CancellationInterval)) == 0
                && cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
                return false;
            }
            ++end;
        }
        double value = 0.0;
        switch (options.statistic)
        {
        case CellStatistic::Minimum:
            value = samples.at(begin).elevation;
            break;
        case CellStatistic::Maximum:
            value = samples.at(end - 1).elevation;
            break;
        case CellStatistic::Mean:
        {
            long double sum = 0.0L;
            for (qsizetype index = begin; index < end; ++index)
            {
                if ((index % static_cast<qsizetype>(CancellationInterval)) == 0
                    && cancelled(shouldCancel))
                {
                    setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
                    return false;
                }
                sum += samples.at(index).elevation;
            }
            value = static_cast<double>(sum / (end - begin));
            break;
        }
        case CellStatistic::Median:
        {
            const qsizetype count = end - begin;
            const qsizetype middle = begin + count / 2;
            value = (count % 2) == 1
                ? samples.at(middle).elevation
                : (samples.at(middle - 1).elevation
                   + samples.at(middle).elevation) / 2.0;
            break;
        }
        }
        candidate.elevations[static_cast<qsizetype>(
            samples.at(begin).cellIndex)] = value;
        ++candidate.populatedCellCount;
        begin = end;
    }
    if (options.fillSmallHoles
        && !fillSmallHoles(
            candidate, options.maximumHoleCells,
            shouldCancel, errorMessage))
    {
        return false;
    }
    if (!candidate.isValid())
    {
        setError(errorMessage, QStringLiteral("DEM 网格生成结果无效。"));
        return false;
    }
    grid = std::move(candidate);
    if (reportProgress)
        reportProgress(1.0);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool buildGridFromCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const GridOptions& options,
    Grid& grid,
    double* suggestedResolutionOutput,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    if (!cloud || cloud->empty())
    {
        setError(errorMessage, QStringLiteral("没有可用于 DEM 的点云数据。"));
        return false;
    }
    if (cloud->size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())
        || cloud->size() > static_cast<std::size_t>(
            std::numeric_limits<qsizetype>::max()))
    {
        setError(errorMessage, QStringLiteral("DEM 输入点数超出可表示范围。"));
        return false;
    }
    if (!validateGridOptions(options, errorMessage))
        return false;

    Eigen::Matrix4d matrix;
    if (!spatialMatrix(spatial, matrix))
    {
        setError(errorMessage, QStringLiteral("点云空间参考包含无效数值。"));
        return false;
    }

    QVector<famp::cloud::Point3d> coordinates;
    coordinates.reserve(static_cast<qsizetype>(cloud->size()));
    for (std::size_t index = 0; index < cloud->size(); ++index)
    {
        if ((index % CancellationInterval) == 0)
        {
            if (cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("DEM 生成已取消。"));
                return false;
            }
            if (reportProgress)
            {
                reportProgress(0.15 * static_cast<double>(index)
                               / cloud->size());
            }
        }
        famp::cloud::Point3d coordinate;
        if (!realCoordinate(matrix, spatial, cloud->points[index], coordinate))
        {
            setError(errorMessage,
                     QStringLiteral("第 %1 个点的真实坐标无效。")
                         .arg(index + 1));
            return false;
        }
        coordinates.append(coordinate);
    }

    Grid candidate;
    double suggested = 0.0;
    if (!buildGrid(
            coordinates, options, candidate,
            suggestedResolutionOutput ? &suggested : nullptr,
            errorMessage, shouldCancel,
            reportProgress ? Progress([&](double progress) {
                reportProgress(0.15 + 0.85 * progress);
            }) : Progress{}))
    {
        return false;
    }
    grid = std::move(candidate);
    if (suggestedResolutionOutput)
        *suggestedResolutionOutput = suggested;
    if (reportProgress)
        reportProgress(1.0);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool generateContours(
    const Grid& grid,
    const ContourOptions& options,
    QVector<ContourLine>& contours,
    double* usedInterval,
    double* usedBase,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    if (!grid.isValid())
    {
        setError(errorMessage, QStringLiteral("无法从无效 DEM 网格生成等高线。"));
        return false;
    }
    if (!validateContourOptions(options, errorMessage))
        return false;
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("等高线生成已取消。"));
        return false;
    }

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (double value : grid.elevations)
    {
        if (!std::isfinite(value))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
    {
        setError(errorMessage, QStringLiteral("DEM 不包含有效高程。"));
        return false;
    }

    const double interval = options.automaticInterval
        ? niceInterval((maximum - minimum) / 20.0)
        : options.interval;
    const double base = options.automaticBase
        ? std::floor(minimum / interval) * interval
        : options.baseElevation;
    if (!std::isfinite(interval) || interval <= 0.0 || !std::isfinite(base))
    {
        setError(errorMessage, QStringLiteral("等高距或基准高程无效。"));
        return false;
    }
    if (usedInterval)
        *usedInterval = interval;
    if (usedBase)
        *usedBase = base;

    QMap<qint64, QVector<Segment>> segmentsByLevel;
    quint64 segmentCount = 0;
    if (grid.columns >= 2 && grid.rows >= 2 && maximum > minimum)
    {
        const quint64 squareCount = static_cast<quint64>(grid.columns - 1)
            * static_cast<quint64>(grid.rows - 1);
        quint64 squareIndex = 0;
        for (int row = 0; row < grid.rows - 1; ++row)
        {
            for (int column = 0; column < grid.columns - 1; ++column)
            {
                if ((squareIndex % CancellationInterval) == 0)
                {
                    if (cancelled(shouldCancel))
                    {
                        setError(errorMessage, QStringLiteral("等高线生成已取消。"));
                        return false;
                    }
                    if (reportProgress)
                    {
                        reportProgress(0.75 * static_cast<double>(squareIndex)
                                       / std::max<quint64>(1, squareCount));
                    }
                }
                ++squareIndex;
                const std::array<double, 4> values{
                    grid.value(row, column),
                    grid.value(row, column + 1),
                    grid.value(row + 1, column + 1),
                    grid.value(row + 1, column)};
                if (!std::all_of(values.cbegin(), values.cend(),
                                 [](double value) {
                                     return std::isfinite(value);
                                 }))
                {
                    continue;
                }
                const auto bottomLeft = grid.cellCenter(row, column);
                const auto bottomRight = grid.cellCenter(row, column + 1);
                const auto topRight = grid.cellCenter(row + 1, column + 1);
                const auto topLeft = grid.cellCenter(row + 1, column);
                const double squareMinimum = *std::min_element(
                    values.cbegin(), values.cend());
                const double squareMaximum = *std::max_element(
                    values.cbegin(), values.cend());
                const qint64 firstLevel = static_cast<qint64>(std::floor(
                    (squareMinimum - base) / interval)) + 1;
                const qint64 lastLevel = static_cast<qint64>(std::floor(
                    (squareMaximum - base) / interval));
                for (qint64 levelIndex = firstLevel;
                     levelIndex <= lastLevel; ++levelIndex)
                {
                    const double level = base + levelIndex * interval;
                    QVector<Segment>& levelSegments = segmentsByLevel[levelIndex];
                    const qsizetype before = levelSegments.size();
                    triangleSegment(
                        {bottomLeft, bottomRight, topRight},
                        {values[0], values[1], values[2]},
                        level, levelSegments);
                    triangleSegment(
                        {bottomLeft, topRight, topLeft},
                        {values[0], values[2], values[3]},
                        level, levelSegments);
                    segmentCount += static_cast<quint64>(
                        levelSegments.size() - before);
                    if (segmentCount > options.maximumSegmentCount)
                    {
                        setError(errorMessage,
                                 QStringLiteral("等高线线段数超过 %1 个安全上限；请增大等高距或 DEM 分辨率。")
                                     .arg(options.maximumSegmentCount));
                        return false;
                    }
                }
            }
        }
    }

    QVector<ContourLine> candidate;
    int processedLevels = 0;
    for (auto level = segmentsByLevel.cbegin();
         level != segmentsByLevel.cend(); ++level)
    {
        if (cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("等高线连线已取消。"));
            return false;
        }
        const double elevation = base + level.key() * interval;
        candidate += stitchSegments(
            grid, elevation, level.value(), options.smoothingIterations);
        ++processedLevels;
        if (reportProgress)
        {
            reportProgress(0.75 + 0.25 * processedLevels
                / std::max<qsizetype>(1, segmentsByLevel.size()));
        }
    }
    for (const ContourLine& line : candidate)
    {
        if (!std::isfinite(line.elevation) || line.points.size() < 2
            || !std::all_of(line.points.cbegin(), line.points.cend(),
                            finitePoint))
        {
            setError(errorMessage, QStringLiteral("等高线生成结果无效。"));
            return false;
        }
    }
    contours = std::move(candidate);
    if (reportProgress)
        reportProgress(1.0);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result analyze(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const GridOptions& gridOptions,
    const ContourOptions& contourOptions,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    Result result;
    if (!validateContourOptions(contourOptions, &result.error))
        return result;
    QString operationError;
    if (!buildGridFromCloud(
            cloud, spatial, gridOptions, result.grid,
            &result.suggestedResolution, &operationError,
            shouldCancel,
            reportProgress ? Progress([&](double progress) {
                reportProgress(0.65 * progress);
            }) : Progress{}))
    {
        result.cancelled = cancelled(shouldCancel);
        result.error = result.cancelled
            ? QStringLiteral("地形分析已取消。") : operationError;
        return result;
    }
    if (!generateContours(
            result.grid, contourOptions, result.contours,
            &result.contourInterval, &result.contourBase,
            &operationError, shouldCancel,
            reportProgress ? Progress([&](double progress) {
                reportProgress(0.65 + 0.35 * progress);
            }) : Progress{}))
    {
        result.cancelled = cancelled(shouldCancel);
        result.error = result.cancelled
            ? QStringLiteral("地形分析已取消。") : operationError;
        result.grid = Grid{};
        result.contours.clear();
        return result;
    }
    result.error.clear();
    if (reportProgress)
        reportProgress(1.0);
    return result;
}
}
