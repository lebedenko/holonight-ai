#include "holonight_application/tools/list_files_tool.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <unistd.h>

namespace holonight_application {
namespace {

class ListFilesToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.isValid());
    QDir dir(temp_dir_.path());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("subfolder")));
    ASSERT_TRUE(writeFile(dir.filePath(QStringLiteral("notes.txt"))));
    ASSERT_TRUE(writeFile(dir.filePath(QStringLiteral("report.pdf"))));
    ASSERT_TRUE(writeFile(dir.filePath(QStringLiteral(".hidden"))));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("unreadable")));
    ASSERT_TRUE(QFile::setPermissions(dir.filePath(QStringLiteral("unreadable")), QFileDevice::Permission{}));

    tool_ = std::make_unique<ListFilesTool>(std::filesystem::path(temp_dir_.path().toStdString()));
  }

  void TearDown() override {
    // Restore permissions so QTemporaryDir can clean itself up.
    QDir dir(temp_dir_.path());
    QFile::setPermissions(dir.filePath(QStringLiteral("unreadable")),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  }

  [[nodiscard]] ListFilesTool& tool() { return *tool_; }

 private:
  static bool writeFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
  }

  QTemporaryDir temp_dir_;
  std::unique_ptr<ListFilesTool> tool_;
};

TEST_F(ListFilesToolTest, NameIsListFiles) { EXPECT_EQ(tool().name(), QStringLiteral("ListFiles")); }

TEST_F(ListFilesToolTest, SchemaRequiresPathString) {
  const QJsonObject schema = tool().schema();
  EXPECT_EQ(schema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
  const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
  ASSERT_EQ(required.size(), 1);
  EXPECT_EQ(required[0].toString(), QStringLiteral("path"));
}

TEST_F(ListFilesToolTest, ListsImmediateEntriesWithNameAndTypeExcludingDotfiles) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}});
  ASSERT_TRUE(result.contains(QStringLiteral("entries")));
  const QJsonArray entries = result.value(QStringLiteral("entries")).toArray();

  // notes.txt, report.pdf, subfolder, unreadable -- .hidden excluded.
  ASSERT_EQ(entries.size(), 4);

  QStringList names;
  for (const auto& value : entries) {
    names << value.toObject().value(QStringLiteral("name")).toString();
  }
  EXPECT_FALSE(names.contains(QStringLiteral(".hidden")));
  EXPECT_TRUE(names.contains(QStringLiteral("notes.txt")));
  EXPECT_TRUE(names.contains(QStringLiteral("subfolder")));

  for (const auto& value : entries) {
    const QJsonObject entry = value.toObject();
    if (entry.value(QStringLiteral("name")).toString() == QStringLiteral("subfolder")) {
      EXPECT_EQ(entry.value(QStringLiteral("type")).toString(), QStringLiteral("directory"));
    } else if (entry.value(QStringLiteral("name")).toString() != QStringLiteral("unreadable")) {
      EXPECT_EQ(entry.value(QStringLiteral("type")).toString(), QStringLiteral("file"));
    }
  }
}

TEST_F(ListFilesToolTest, EntriesAreSortedAlphabeticallyAndConsistentAcrossCalls) {
  const QJsonArray first = tool()
                               .execute(QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}})
                               .value(QStringLiteral("entries"))
                               .toArray();
  const QJsonArray second = tool()
                                .execute(QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}})
                                .value(QStringLiteral("entries"))
                                .toArray();
  ASSERT_EQ(first, second);

  QStringList names;
  for (const auto& value : first) {
    names << value.toObject().value(QStringLiteral("name")).toString();
  }
  QStringList sortedNames = names;
  std::ranges::sort(sortedNames);
  EXPECT_EQ(names, sortedNames);
}

TEST_F(ListFilesToolTest, PathOutsideRootReturnsNotFoundError) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("/etc")}});
  ASSERT_TRUE(result.contains(QStringLiteral("error")));
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("NOT_FOUND"));
}

TEST_F(ListFilesToolTest, ExternalFileDoesNotRevealThatItExists) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("/etc/passwd")}});
  ASSERT_TRUE(result.contains(QStringLiteral("error")));
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("NOT_FOUND"));
}

TEST_F(ListFilesToolTest, ParentTraversalEscapingRootReturnsSameErrorAsNonExistentPath) {
  const QJsonObject outside = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("../../etc")}});
  const QJsonObject missing =
      tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("definitely-does-not-exist")}});
  ASSERT_TRUE(outside.contains(QStringLiteral("error")));
  ASSERT_TRUE(missing.contains(QStringLiteral("error")));
  EXPECT_EQ(outside.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            missing.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString());
}

TEST_F(ListFilesToolTest, NonExistentPathReturnsNotFoundError) {
  const QJsonObject result =
      tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("does-not-exist-xyz")}});
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("NOT_FOUND"));
}

TEST_F(ListFilesToolTest, FilePathReturnsNotADirectoryError) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}});
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("NOT_A_DIRECTORY"));
}

TEST_F(ListFilesToolTest, UnreadableDirectoryReturnsPermissionDeniedError) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "root bypasses directory permission bits";
  }
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("unreadable")}});
  ASSERT_TRUE(result.contains(QStringLiteral("error")));
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("PERMISSION_DENIED"));
}

TEST_F(ListFilesToolTest, MissingPathParameterReturnsInvalidParametersError) {
  const QJsonObject result = tool().execute(QJsonObject{});
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("INVALID_PARAMETERS"));
}

TEST_F(ListFilesToolTest, NonStringPathParameterReturnsInvalidParametersError) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), 42}});
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("INVALID_PARAMETERS"));
}

TEST_F(ListFilesToolTest, RelativePathResolvesAgainstRoot) {
  const QJsonObject result = tool().execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("subfolder")}});
  ASSERT_TRUE(result.contains(QStringLiteral("entries")));
  EXPECT_EQ(result.value(QStringLiteral("entries")).toArray().size(), 0);
}

}  // namespace
}  // namespace holonight_application
