#include "holonight_persistence/migration_runner.h"
#include "test_support.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <gtest/gtest.h>

namespace holonight_persistence {
namespace {

class MigrationRunnerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    connection_name_ = uniqueTestConnectionName();
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
    database.setDatabaseName(QStringLiteral(":memory:"));
    ASSERT_TRUE(database.open());
  }

  void TearDown() override {
    QSqlDatabase::database(connection_name_, false).close();
    QSqlDatabase::removeDatabase(connection_name_);
  }

  [[nodiscard]] QSqlDatabase database() const { return QSqlDatabase::database(connection_name_); }

 private:
  QString connection_name_;
};

TEST_F(MigrationRunnerTest, BuiltInMigrationsContainsVersionOneNamedInit) {
  const auto migrations = MigrationRunner::builtInMigrations();

  ASSERT_FALSE(migrations.empty());
  const auto& first = migrations.front();
  EXPECT_EQ(first.version, 1);
  EXPECT_EQ(first.name, QStringLiteral("0001_init"));
  EXPECT_FALSE(first.sql.isEmpty());
}

TEST_F(MigrationRunnerTest, AppliesInitMigrationToFreshDatabase) {
  QSqlDatabase testDatabase = database();

  const auto result = MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations());

  ASSERT_TRUE(result.has_value()) << result.error().toStdString();
  EXPECT_EQ(*result, 8);

  QSqlQuery tables(testDatabase);
  ASSERT_TRUE(tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")));
  QStringList tableNames;
  while (tables.next()) {
    tableNames.append(tables.value(0).toString());
  }
  EXPECT_TRUE(tableNames.contains(QStringLiteral("conversations")));
  EXPECT_TRUE(tableNames.contains(QStringLiteral("messages")));
  EXPECT_TRUE(tableNames.contains(QStringLiteral("schema_version")));

  QSqlQuery columns(testDatabase);
  ASSERT_TRUE(columns.exec(QStringLiteral("PRAGMA table_info(conversations)")));
  QStringList columnNames;
  while (columns.next()) {
    columnNames.append(columns.value(1).toString());
  }
  EXPECT_EQ(columnNames, (QStringList{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("created_at"),
                                      QStringLiteral("updated_at"), QStringLiteral("last_model_id"),
                                      QStringLiteral("title_source"), QStringLiteral("pinned_at")}));
}

TEST_F(MigrationRunnerTest, RecordsSchemaVersionRowAfterApplying) {
  QSqlDatabase testDatabase = database();
  ASSERT_TRUE(MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations()).has_value());

  QSqlQuery query(testDatabase);
  ASSERT_TRUE(query.exec(QStringLiteral("SELECT version FROM schema_version ORDER BY version")));
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 1);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 2);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 3);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 4);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 5);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 6);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 7);
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 8);
  EXPECT_FALSE(query.next());
}

TEST_F(MigrationRunnerTest, PreExistingConversationsDefaultToManualTitleSourceAfterMigration) {
  QSqlDatabase testDatabase = database();
  const auto allMigrations = MigrationRunner::builtInMigrations();
  const std::vector<MigrationRunner::Migration> preTitleSourceMigrations(allMigrations.begin(),
                                                                         allMigrations.begin() + 2);
  ASSERT_TRUE(MigrationRunner::apply(testDatabase, preTitleSourceMigrations).has_value());

  QSqlQuery insert(testDatabase);
  insert.prepare(QStringLiteral(
      "INSERT INTO conversations (id, title, created_at, updated_at) VALUES ('pre-existing', 'Old Chat', "
      "'2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')"));
  ASSERT_TRUE(insert.exec());

  ASSERT_TRUE(MigrationRunner::apply(testDatabase, allMigrations).has_value());

  QSqlQuery select(testDatabase);
  ASSERT_TRUE(select.exec(QStringLiteral("SELECT title_source FROM conversations WHERE id = 'pre-existing'")));
  ASSERT_TRUE(select.next());
  EXPECT_EQ(select.value(0).toString(), QStringLiteral("Manual"));
}

TEST_F(MigrationRunnerTest, CreateUsageTableInOrder) {
  QSqlDatabase testDatabase = database();

  const auto result = MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations());

  ASSERT_TRUE(result.has_value()) << result.error().toStdString();
  EXPECT_EQ(*result, 8);

  QSqlQuery columns(testDatabase);
  ASSERT_TRUE(columns.exec(QStringLiteral("PRAGMA table_info(usage)")));
  QStringList columnNames;
  while (columns.next()) {
    columnNames.append(columns.value(1).toString());
  }
  EXPECT_EQ(
      columnNames,
      (QStringList{QStringLiteral("id"), QStringLiteral("message_id"), QStringLiteral("conversation_id"),
                   QStringLiteral("model_identifier"), QStringLiteral("input_tokens"), QStringLiteral("output_tokens"),
                   QStringLiteral("reasoning_tokens"), QStringLiteral("cache_creation_tokens"),
                   QStringLiteral("cache_read_tokens"), QStringLiteral("total_tokens"), QStringLiteral("duration_ms"),
                   QStringLiteral("ollama_total_duration_ns"), QStringLiteral("ollama_load_duration_ns"),
                   QStringLiteral("ollama_prompt_eval_duration_ns"), QStringLiteral("ollama_eval_duration_ns"),
                   QStringLiteral("created_at")}));
}

TEST_F(MigrationRunnerTest, SkipsAlreadyAppliedMigrationsOnSecondCall) {
  QSqlDatabase testDatabase = database();
  ASSERT_TRUE(MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations()).has_value());

  const auto second = MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations());

  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 8);

  QSqlQuery query(testDatabase);
  ASSERT_TRUE(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_version")));
  ASSERT_TRUE(query.next());
  EXPECT_EQ(query.value(0).toInt(), 8);
}

TEST_F(MigrationRunnerTest, AddsToolCallsColumnToMessagesAndReachesVersionEight) {
  QSqlDatabase testDatabase = database();

  const auto result = MigrationRunner::apply(testDatabase, MigrationRunner::builtInMigrations());

  ASSERT_TRUE(result.has_value()) << result.error().toStdString();
  EXPECT_EQ(*result, 8);

  QSqlQuery columns(testDatabase);
  ASSERT_TRUE(columns.exec(QStringLiteral("PRAGMA table_info(messages)")));
  QStringList columnNames;
  while (columns.next()) {
    columnNames.append(columns.value(1).toString());
  }
  EXPECT_TRUE(columnNames.contains(QStringLiteral("tool_calls")));
}

}  // namespace
}  // namespace holonight_persistence
