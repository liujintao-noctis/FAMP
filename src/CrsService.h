#pragma once

#include <QString>

#include <memory>

namespace famp::crs
{
struct Info
{
    QString identifier;
    QString name;
    QString type;
    bool geographic = false;
};

struct Coordinate
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

class Transformer
{
public:
    Transformer();
    ~Transformer();
    Transformer(Transformer&& other) noexcept;
    Transformer& operator=(Transformer&& other) noexcept;

    Transformer(const Transformer&) = delete;
    Transformer& operator=(const Transformer&) = delete;

    bool initialize(const QString& sourceIdentifier,
                    const QString& targetIdentifier,
                    QString* errorMessage = nullptr);
    bool transform(const Coordinate& source,
                   Coordinate& target,
                   QString* errorMessage = nullptr) const;
    bool isValid() const;
    QString sourceIdentifier() const;
    QString targetIdentifier() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

QString normalizedEpsg(const QString& identifier);
QString runtimeVersion();
bool inspect(const QString& identifier,
             Info& info,
             QString* errorMessage = nullptr);
bool transform(const QString& sourceIdentifier,
               const QString& targetIdentifier,
               const Coordinate& source,
               Coordinate& target,
               QString* errorMessage = nullptr);
}
