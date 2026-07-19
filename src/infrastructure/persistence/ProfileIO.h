#pragma once

#include "ProfileAnalysis.h"

#include <QString>

namespace famp::profileio
{
inline constexpr quint16 SidecarSchemaVersion = 1;

QString pathWithProfileSuffix(const QString& path);

bool saveResultAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool loadResult(const QString& path,
                famp::profile::Result& result,
                QString* errorMessage = nullptr);

bool exportBinsCsvAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportSamplesCsvAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool exportSvgAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});
}
