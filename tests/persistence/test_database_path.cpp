#include "holonight_persistence/database_path.h"

#include <QDir>
#include <QString>

#include <cstdlib>
#include <gtest/gtest.h>

namespace holonight_persistence {
namespace {

class DatabasePathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    original_xdg_data_home_ = qgetenv("XDG_DATA_HOME");
    original_home_ = qgetenv("HOME");
  }

  void TearDown() override {
    if (original_xdg_data_home_.isNull()) {
      qunsetenv("XDG_DATA_HOME");
    } else {
      qputenv("XDG_DATA_HOME", original_xdg_data_home_);
    }
    if (!original_home_.isNull()) {
      qputenv("HOME", original_home_);
    }
    if (!scratch_dir_.isEmpty()) {
      QDir(scratch_dir_).removeRecursively();
    }
  }

  QString makeScratchDir(const QString& suffix) {
    scratch_dir_ = QDir::tempPath() + QStringLiteral("/holonight_test_xdg_%1_%2").arg(suffix).arg(rand());
    QDir().mkpath(scratch_dir_);
    return scratch_dir_;
  }

 private:
  QByteArray original_xdg_data_home_;
  QByteArray original_home_;
  QString scratch_dir_;
};

TEST_F(DatabasePathTest, UsesXdgDataHomeWhenSet) {
  const QString scratch = makeScratchDir(QStringLiteral("set"));
  qputenv("XDG_DATA_HOME", scratch.toUtf8());

  const QString path = resolveDatabaseFilePath();

  EXPECT_EQ(path, scratch + QStringLiteral("/holonight-ai/conversations.db"));
  EXPECT_TRUE(QDir(scratch + QStringLiteral("/holonight-ai")).exists());
}

TEST_F(DatabasePathTest, FallsBackToHomeLocalShareWhenUnset) {
  qunsetenv("XDG_DATA_HOME");
  const QString scratch = makeScratchDir(QStringLiteral("unset"));
  qputenv("HOME", scratch.toUtf8());

  const QString path = resolveDatabaseFilePath();

  EXPECT_EQ(path, scratch + QStringLiteral("/.local/share/holonight-ai/conversations.db"));
}

TEST_F(DatabasePathTest, FallsBackToHomeLocalShareWhenEmpty) {
  qputenv("XDG_DATA_HOME", "");
  const QString scratch = makeScratchDir(QStringLiteral("empty"));
  qputenv("HOME", scratch.toUtf8());

  const QString path = resolveDatabaseFilePath();

  EXPECT_EQ(path, scratch + QStringLiteral("/.local/share/holonight-ai/conversations.db"));
}

TEST_F(DatabasePathTest, CreatingDirectoryTwiceDoesNotError) {
  const QString scratch = makeScratchDir(QStringLiteral("idempotent"));
  qputenv("XDG_DATA_HOME", scratch.toUtf8());

  const QString first = resolveDatabaseFilePath();
  const QString second = resolveDatabaseFilePath();

  EXPECT_EQ(first, second);
  EXPECT_TRUE(QDir(scratch + QStringLiteral("/holonight-ai")).exists());
}

}  // namespace
}  // namespace holonight_persistence
