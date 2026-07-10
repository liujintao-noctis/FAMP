#pragma once

#include <cmath>

namespace famp::metric
{
inline constexpr double MillimetersPerInch = 25.4;
inline constexpr double MillimetersPerMeter = 1000.0;
inline constexpr double DefaultDotsPerInch = 96.0;

inline double bestAvailableDotsPerInch(double physicalDotsPerInch,
                                       double logicalDotsPerInch) noexcept
{
    if (std::isfinite(physicalDotsPerInch) && physicalDotsPerInch > 0.0)
        return physicalDotsPerInch;
    if (std::isfinite(logicalDotsPerInch) && logicalDotsPerInch > 0.0)
        return logicalDotsPerInch;
    return DefaultDotsPerInch;
}

// Qt reports QScreen physical DPI in device-independent dots. These values
// therefore map directly to QPainter coordinates used by QWidget classes.
constexpr double deviceIndependentPixelsPerMillimeter(double dotsPerInch) noexcept
{
    return dotsPerInch > 0.0 ? dotsPerInch / MillimetersPerInch : 0.0;
}

// Point-cloud coordinates are treated as metres. At scale 1:N, one metre is
// represented by 1000/N physical millimetres on the display.
constexpr double pixelsPerMeterAtScale(double pixelsPerMillimeter,
                                       int scaleDenominator) noexcept
{
    return pixelsPerMillimeter > 0.0 && scaleDenominator > 0
        ? pixelsPerMillimeter * MillimetersPerMeter / scaleDenominator
        : 0.0;
}
}
