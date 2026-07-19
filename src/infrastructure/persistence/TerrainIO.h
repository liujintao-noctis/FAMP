#pragma once

#include "TerrainAnalysis.h"

#include <QString>

namespace famp::terrainio
{
inline constexpr quint16 SidecarSchemaVersion = 1;

QString pathWithDemSuffix(const QString& path);

bool saveGridAtomically(const QString& path,
                        const famp::terrain::Grid& grid,
                        QString* errorMessage = nullptr,
                        const famp::tasks::CancellationCheck& shouldCancel = {});

bool loadGrid(const QString& path,
              famp::terrain::Grid& grid,
              QString* errorMessage = nullptr);

bool exportAsciiGridAtomically(const QString& path,
                               const famp::terrain::Grid& grid,
                               QString* errorMessage = nullptr,
                               const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportGridCsvAtomically(const QString& path,
                             const famp::terrain::Grid& grid,
                             QString* errorMessage = nullptr,
                             const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportContoursCsvAtomically(
    const QString& path,
    const QVector<famp::terrain::ContourLine>& contours,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportContoursSvgAtomically(
    const QString& path,
    const QVector<famp::terrain::ContourLine>& contours,
    const QString& sourceCrs = {},
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});
}
