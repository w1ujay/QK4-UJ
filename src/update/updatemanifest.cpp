#include "updatemanifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <limits>

namespace {

constexpr int SUPPORTED_SCHEMA = 1;

// A JSON number holding a whole value within [0, max]; rejects strings, fractions and negatives.
std::optional<qint64> wholeNumber(const QJsonValue &v, qint64 max) {
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < 0 || d > static_cast<double>(max) || d != std::floor(d))
        return std::nullopt;
    return static_cast<qint64>(d);
}

std::optional<QString> nonEmptyString(const QJsonValue &v) {
    if (!v.isString() || v.toString().isEmpty())
        return std::nullopt;
    return v.toString();
}

std::optional<QStringList> stringList(const QJsonValue &v) {
    if (!v.isArray())
        return std::nullopt;
    QStringList out;
    for (const QJsonValue &item : v.toArray()) {
        if (!item.isString())
            return std::nullopt;
        out.append(item.toString());
    }
    return out;
}

std::optional<UpdateAsset> parseAsset(const QJsonValue &v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const auto name = nonEmptyString(o.value("name"));
    const auto url = nonEmptyString(o.value("url"));
    const auto sha = nonEmptyString(o.value("sha256"));
    const auto size = wholeNumber(o.value("size"), std::numeric_limits<qint64>::max() / 2);
    if (!name || !url || !sha || !size)
        return std::nullopt;
    UpdateAsset a;
    a.name = *name;
    a.url = QUrl(*url, QUrl::StrictMode);
    if (!a.url.isValid())
        return std::nullopt;
    a.size = *size;
    a.sha256Hex = sha->toLatin1();
    return a;
}

std::optional<UpdateRelease> parseRelease(const QJsonValue &v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const auto uj = wholeNumber(o.value("uj"), std::numeric_limits<int>::max());
    const auto version = nonEmptyString(o.value("version"));
    const auto date = nonEmptyString(o.value("date"));
    const auto added = stringList(o.value("added"));
    const auto fixed = stringList(o.value("fixed"));
    const auto changed = stringList(o.value("changed"));
    if (!uj || !version || !date || !added || !fixed || !changed)
        return std::nullopt;
    UpdateRelease r;
    r.uj = static_cast<int>(*uj);
    r.version = *version;
    r.date = *date;
    r.added = *added;
    r.fixed = *fixed;
    r.changed = *changed;
    return r;
}

} // namespace

std::optional<UpdateManifest> parseUpdateManifest(const QByteArray &json) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject root = doc.object();

    const auto schema = wholeNumber(root.value("schema"), std::numeric_limits<int>::max());
    if (!schema || *schema != SUPPORTED_SCHEMA)
        return std::nullopt;

    const auto uj = wholeNumber(root.value("uj"), std::numeric_limits<int>::max());
    const auto version = nonEmptyString(root.value("version"));
    if (!uj || !version || !root.value("assets").isObject() || !root.value("history").isArray())
        return std::nullopt;

    UpdateManifest m;
    m.uj = static_cast<int>(*uj);
    m.version = *version;

    const QJsonObject assets = root.value("assets").toObject();
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        const auto asset = parseAsset(it.value());
        if (!asset)
            return std::nullopt;
        m.assets.insert(it.key(), *asset);
    }

    for (const QJsonValue &entry : root.value("history").toArray()) {
        const auto release = parseRelease(entry);
        if (!release)
            return std::nullopt;
        m.history.append(*release);
    }
    return m;
}
