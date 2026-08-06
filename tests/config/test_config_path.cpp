#include "holonight_config/config_path.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <cstdlib>
#include <gtest/gtest.h>

namespace holonight_config {
namespace {

class ConfigPathTest : public ::testing::Test {
 protected:
  void SetUp() override { original_xdg_config_home_ = qgetenv("XDG_CONFIG_HOME"); }

  void TearDown() override {
    if (original_xdg_config_home_.isNull()) {
      qunsetenv("XDG_CONFIG_HOME");
    } else {
      qputenv("XDG_CONFIG_HOME", original_xdg_config_home_);
    }
    if (!scratch_dir_.isEmpty()) {
      QDir(scratch_dir_).removeRecursively();
    }
  }

  QString makeScratchDir(const QString& suffix) {
    scratch_dir_ = QDir::tempPath() + QStringLiteral("/holonight_test_config_%1_%2").arg(suffix).arg(rand());
    QDir().mkpath(scratch_dir_);
    return scratch_dir_;
  }

 private:
  QByteArray original_xdg_config_home_;
  QString scratch_dir_;
};

TEST_F(ConfigPathTest, ResolveConfigFilePathCreatesHolonightAiDirectory) {
  const QString scratch = makeScratchDir(QStringLiteral("dir"));
  qputenv("XDG_CONFIG_HOME", scratch.toUtf8());

  const QString path = resolveConfigFilePath();

  EXPECT_TRUE(QDir(scratch + QStringLiteral("/holonight-ai")).exists());
  Q_UNUSED(path)
}

TEST_F(ConfigPathTest, ResolveConfigFilePathEndsWithConfigJson) {
  const QString scratch = makeScratchDir(QStringLiteral("name"));
  qputenv("XDG_CONFIG_HOME", scratch.toUtf8());

  const QString path = resolveConfigFilePath();
  const QFileInfo info(path);

  EXPECT_EQ(info.fileName(), QStringLiteral("config.json"));
  EXPECT_EQ(info.dir().dirName(), QStringLiteral("holonight-ai"));
}

}  // namespace
}  // namespace holonight_config
