#include "holonight_config/config_path.h"

#include <QDir>
#include <QStandardPaths>

namespace holonight_config {

QString resolveConfigFilePath() {
  const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);

  const QString appConfigDir = configHome + QStringLiteral("/holonight-ai");
  QDir().mkpath(appConfigDir);

  return appConfigDir + QStringLiteral("/config.json");
}

}  // namespace holonight_config
