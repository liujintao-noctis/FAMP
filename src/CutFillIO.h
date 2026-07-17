#pragma once

#include "CutFillAnalysis.h"

#include <QString>

namespace famp::cutfillio
{
inline constexpr quint16 SidecarSchemaVersion = 1;

QString pathWithVolumeSuffix(const QString& path);

bool saveResultAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool loadResult(const QString& path,
                famp::cutfill::Result& result,
                QString* errorMessage = nullptr);

bool exportSummaryCsvAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportCellsCsvAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportSvgAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});
}
