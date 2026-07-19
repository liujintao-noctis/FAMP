#pragma once

#include <cmath>

namespace famp::metric
{
inline constexpr double MillimetersPerInch = 25.4;
inline constexpr double MillimetersPerMeter = 1000.0;
inline constexpr double DefaultDotsPerInch = 96.0;
inline constexpr double CalibrationReferenceMillimeters = 100.0;
inline constexpr double MinimumCalibrationFactor = 0.2;
inline constexpr double MaximumCalibrationFactor = 5.0;

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

inline bool isValidCalibrationFactor(double factor) noexcept
{
    return std::isfinite(factor)
        && factor >= MinimumCalibrationFactor
        && factor <= MaximumCalibrationFactor;
}

// A target rendered with the current calibration and measured as M mm must
// be rescaled by reference/M. Measuring a 100 mm target reduces ruler and
// single-pixel uncertainty to a practical level for field acceptance.
inline double calibrationAdjustment(double referenceMillimeters,
                                    double measuredMillimeters) noexcept
{
    if (!std::isfinite(referenceMillimeters)
        || !std::isfinite(measuredMillimeters)
        || referenceMillimeters <= 0.0
        || measuredMillimeters <= 0.0)
    {
        return 0.0;
    }
    return referenceMillimeters / measuredMillimeters;
}

inline double calibratedPixelsPerMillimeter(double reportedValue,
                                             double factor) noexcept
{
    return std::isfinite(reportedValue) && reportedValue > 0.0
            && isValidCalibrationFactor(factor)
        ? reportedValue * factor
        : 0.0;
}
}
