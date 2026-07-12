#pragma once

#include <array>

namespace famp::cloud
{
struct SpatialReference
{
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 16> transform{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0};
};
}
