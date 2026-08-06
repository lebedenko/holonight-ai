#pragma once

#include <QSqlDatabase>
#include <QString>

#include <expected>
#include <vector>

namespace holonight_persistence {

// Stateless: applies an ordered list of .sql migrations to any QSqlDatabase handed to it.
class MigrationRunner {
 public:
  struct Migration {
    int version = 0;
    QString name;  // e.g. "0001_init" — logging/error messages only, never parsed
    QString sql;   // semicolon-separated DDL statements, no embedded literal ';'
  };

  // Reads every migration embedded via Qt resources, in ascending version order (REQ-C-003).
  [[nodiscard]] static std::vector<Migration> builtInMigrations();

  // Applies every migration whose version exceeds the database's current schema_version, each
  // inside its own transaction, in ascending order (REQ-F-003/004, REQ-NF-009). Bootstraps
  // schema_version's absence as version 0. Returns the resulting version, or the first failure.
  [[nodiscard]] static std::expected<int, QString> apply(QSqlDatabase& database,
                                                         const std::vector<Migration>& migrations);

 private:
  [[nodiscard]] static std::expected<int, QString> currentVersion(QSqlDatabase& database);
  [[nodiscard]] static std::expected<void, QString> applyOne(QSqlDatabase& database, const Migration& migration);
  [[nodiscard]] static std::expected<void, QString> executeStatements(QSqlDatabase& database, const QString& sql);
};

}  // namespace holonight_persistence
