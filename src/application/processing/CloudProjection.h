#pragma once

#include <QMetaType>
#include <QString>
#include <QUuid>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <array>
#include <optional>

namespace famp::projection
{

enum class Plane
{
    YOZ,
    XOZ,
    XOY,
    Overlook
};

QString axisName(Plane plane);
QString displayName(Plane plane);
bool isOverlook(Plane plane) noexcept;
std::optional<Plane> planeFromMetadata(const QString& axis,
                                       bool overlook) noexcept;

struct Result
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points;
    Plane plane = Plane::XOY;
    std::array<double, 3> origin{};
    std::array<double, 3> normal{};
    QString error;

    bool succeeded() const noexcept;
};

// Projects a complete cloud to the minimum axis-aligned plane of its local
// coordinate bounds. Point order, RGB values, and point count are preserved so
// per-point attributes remain aligned with the source entity.
Result projectToMinimumPlane(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    Plane plane);

struct Source
{
    QUuid entityId;
    QString name;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points;
};

struct Preview
{
    Source source;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points;
    Plane plane = Plane::XOY;
};

// UI-independent state for the archaeology drafting pipeline. Selecting a
// different entity invalidates transient projection output, while reselecting
// the same entity preserves its preview for automatic drafting.
class Workflow
{
public:
    bool selectSource(const QUuid& entityId,
                      const QString& name,
                      const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
                      QString* errorMessage = nullptr);
    void clearSource();

    bool hasSource() const noexcept;
    const Source* source() const noexcept;

    bool setPreview(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
                    Plane plane,
                    QString* errorMessage = nullptr);
    void clearPreview();

    bool hasPreview() const noexcept;
    const Preview* preview() const noexcept;

private:
    std::optional<Source> source_;
    std::optional<Preview> preview_;
};

} // namespace famp::projection

Q_DECLARE_METATYPE(famp::projection::Plane)
