#include "updateversion.h"

#include <QRegularExpression>

namespace UpdateVersion {

std::optional<int> parseUjNumber(const QString &version) {
    static const QRegularExpression re(QStringLiteral("-uj\\.(\\d+)$"));
    const QRegularExpressionMatch match = re.match(version);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const int n = match.captured(1).toInt(&ok);
    if (!ok)
        return std::nullopt;
    return n;
}

} // namespace UpdateVersion
