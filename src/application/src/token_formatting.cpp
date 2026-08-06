#include "holonight_application/token_formatting.h"

namespace holonight_application {

QString formatTokenCount(const QVariant& tokens) {
  if (!tokens.isValid() || tokens.isNull()) {
    return {};
  }
  const qint64 value = tokens.toLongLong();
  if (value >= 1000) {
    return QStringLiteral("%1K").arg(static_cast<double>(value) / 1000.0, 0, 'f', 1);
  }
  return QString::number(value);
}

}  // namespace holonight_application
