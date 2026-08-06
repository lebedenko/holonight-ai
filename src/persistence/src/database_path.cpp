#include "holonight_persistence/database_path.h"

#include <QDir>
#include <QProcessEnvironment>

namespace holonight_persistence {

QString resolveDatabaseFilePath() {
  const QString xdgDataHome = QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_DATA_HOME"));
  const QString dataHome = xdgDataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : xdgDataHome;

  const QString appDataDir = dataHome + QStringLiteral("/holonight-ai");
  QDir().mkpath(appDataDir);

  return appDataDir + QStringLiteral("/conversations.db");
}

}  // namespace holonight_persistence
