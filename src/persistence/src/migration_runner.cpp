#include "holonight_persistence/migration_runner.h"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>

#include <algorithm>

namespace holonight_persistence {

namespace {

QString readResource(const QString& resourcePath) {
  QFile file(resourcePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QTextStream stream(&file);
  return stream.readAll();
}

}  // namespace

std::vector<MigrationRunner::Migration> MigrationRunner::builtInMigrations() {
  return {
      Migration{
          .version = 1,
          .name = QStringLiteral("0001_init"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0001_init.sql")),
      },
      Migration{
          .version = 2,
          .name = QStringLiteral("0002_add_message_model_id"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0002_add_message_model_id.sql")),
      },
      Migration{
          .version = 3,
          .name = QStringLiteral("0003_add_title_source"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0003_add_title_source.sql")),
      },
      Migration{
          .version = 4,
          .name = QStringLiteral("0004_add_usage"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0004_add_usage.sql")),
      },
      Migration{
          .version = 5,
          .name = QStringLiteral("0005_add_usage_cost_breakdown"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0005_add_usage_cost_breakdown.sql")),
      },
      Migration{
          .version = 6,
          .name = QStringLiteral("0006_drop_usage_cost_columns"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0006_drop_usage_cost_columns.sql")),
      },
      Migration{
          .version = 7,
          .name = QStringLiteral("0007_add_pinned_at"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0007_add_pinned_at.sql")),
      },
      Migration{
          .version = 8,
          .name = QStringLiteral("0008_add_message_tool_calls"),
          .sql = readResource(QStringLiteral(":/holonight_persistence/migrations/0008_add_message_tool_calls.sql")),
      },
  };
}

std::expected<int, QString> MigrationRunner::currentVersion(QSqlDatabase& database) {
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT MAX(version) FROM schema_version"))) {
    // schema_version doesn't exist yet — fresh database, nothing applied.
    return 0;
  }
  if (query.next()) {
    const QVariant value = query.value(0);
    if (!value.isNull()) {
      return value.toInt();
    }
  }
  return 0;
}

std::expected<void, QString> MigrationRunner::executeStatements(QSqlDatabase& database, const QString& sql) {
  const QStringList statements = sql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  for (const QString& raw : statements) {
    const QString statement = raw.trimmed();
    if (statement.isEmpty()) {
      continue;
    }
    QSqlQuery query(database);
    if (!query.exec(statement)) {
      return std::unexpected(QStringLiteral("statement failed: %1").arg(query.lastError().text()));
    }
  }
  return {};
}

std::expected<void, QString> MigrationRunner::applyOne(QSqlDatabase& database, const Migration& migration) {
  if (!database.transaction()) {
    return std::unexpected(QStringLiteral("migration '%1' failed to start transaction: %2")
                               .arg(migration.name, database.lastError().text()));
  }

  if (const auto result = executeStatements(database, migration.sql); !result.has_value()) {
    database.rollback();
    return std::unexpected(QStringLiteral("migration '%1' failed: %2").arg(migration.name, result.error()));
  }

  QSqlQuery insertVersion(database);
  insertVersion.prepare(
      QStringLiteral("INSERT INTO schema_version (version, applied_at) VALUES (:version, :applied_at)"));
  insertVersion.bindValue(QStringLiteral(":version"), migration.version);
  insertVersion.bindValue(QStringLiteral(":applied_at"), QDateTime::currentDateTimeUtc());
  if (!insertVersion.exec()) {
    database.rollback();
    return std::unexpected(QStringLiteral("migration '%1' failed to record schema_version: %2")
                               .arg(migration.name, insertVersion.lastError().text()));
  }

  if (!database.commit()) {
    database.rollback();
    return std::unexpected(
        QStringLiteral("migration '%1' failed to commit: %2").arg(migration.name, database.lastError().text()));
  }

  return {};
}

std::expected<int, QString> MigrationRunner::apply(QSqlDatabase& database, const std::vector<Migration>& migrations) {
  const auto versionResult = currentVersion(database);
  if (!versionResult.has_value()) {
    return std::unexpected(versionResult.error());
  }

  std::vector<Migration> ordered = migrations;
  std::ranges::sort(ordered, {}, &Migration::version);

  int version = *versionResult;
  for (const Migration& migration : ordered) {
    if (migration.version <= version) {
      continue;
    }
    if (const auto result = applyOne(database, migration); !result.has_value()) {
      return std::unexpected(result.error());
    }
    version = migration.version;
  }

  return version;
}

}  // namespace holonight_persistence
