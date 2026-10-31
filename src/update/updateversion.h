#ifndef UPDATEVERSION_H
#define UPDATEVERSION_H

#include <QString>
#include <optional>

// Fork (QK4-UJ) release versions look like "0.8.0-uj.13". Only the trailing number orders releases.
namespace UpdateVersion {

// GitHub repository that publishes fork releases and update.json.
inline constexpr char UPDATE_REPOSITORY[] = "w1ujay/QK4-UJ";

// The N of a "...-uj.N" version, or nullopt for anything else (upstream builds, "-uj.test-1", an empty
// or overflowing number).
std::optional<int> parseUjNumber(const QString &version);

} // namespace UpdateVersion

#endif // UPDATEVERSION_H
