#include "holonight_application/tools/list_files_tool.h"

#include "holonight_application/tools/tool_presenters.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

namespace holonight_application {

namespace {

QJsonObject makeError(const QString& code, const QString& message) {
  return QJsonObject{
      {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject notFoundError() {
  return makeError(QStringLiteral("NOT_FOUND"), QStringLiteral("No such file or directory."));
}

struct Entry {
  QString name;
  QString type;
};

class ListFilesNoopExecutionHandle : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class ListFilesExecutor : public IToolExecutor {
 public:
  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    if (!request.parameters.isObject()) {
      ToolOutcome outcome;
      outcome.error = holonight_domain::ToolError{.code = QStringLiteral("INVALID_PARAMETERS"),
                                                  .message = QStringLiteral("\"path\" must be a string."),
                                                  .details = request.parameters};
      outcome.result =
          QJsonObject{{QStringLiteral("error"),
                       QJsonObject{{QStringLiteral("code"), QStringLiteral("INVALID_PARAMETERS")},
                                   {QStringLiteral("message"), QStringLiteral("\"path\" must be a string.")}}}};
      if (on_finished) {
        on_finished(outcome);
      }
      return std::make_shared<ListFilesNoopExecutionHandle>();
    }

    const QJsonObject parameters = request.parameters.toObject();
    const std::shared_ptr<ToolOutcomeCallback> on_finished_ptr =
        std::make_shared<ToolOutcomeCallback>(std::move(on_finished));
    const auto worker = [on_finished_ptr = on_finished_ptr, parameters = parameters]() {
      ListFilesTool tool;
      ToolOutcome outcome;
      try {
        const QJsonObject result = tool.execute(parameters);
        outcome.result = result;
        const QJsonValue error = result.value(QStringLiteral("error"));
        if (error.isObject()) {
          const QJsonObject errorObject = error.toObject();
          outcome.error = holonight_domain::ToolError{
              .code = errorObject.value(QStringLiteral("code")).toString(),
              .message = errorObject.value(QStringLiteral("message")).toString(),
              .details = errorObject.value(QStringLiteral("details")),
          };
        }
      } catch (...) {
        outcome.error = holonight_domain::ToolError{.code = QStringLiteral("INTERNAL_ERROR"),
                                                    .message = QStringLiteral("ListFiles execution failed."),
                                                    .details = {}};
      }
      if (on_finished_ptr && *on_finished_ptr) {
        (*on_finished_ptr)(outcome);
      }
    };
    std::thread(worker).detach();
    return std::make_shared<ListFilesNoopExecutionHandle>();
  }
};

std::filesystem::path canonicalizeHomeOrFallback(const std::filesystem::path& rawHome) {
  std::error_code errorCode;
  const std::filesystem::path canonical = std::filesystem::canonical(rawHome, errorCode);
  // Falling back to the raw (uncanonicalized) path if canonicalization fails is not expected in
  // practice, but fails closed rather than leaving home_ empty -- an empty prefix would make
  // every path pass the boundary check in execute().
  return errorCode ? rawHome : canonical;
}

}  // namespace

ToolRegistration listFilesToolRegistration() {
  return ToolRegistration{
      .definition =
          ToolDefinition{
              .id = QStringLiteral("filesystem.list"),
              .function_name = QStringLiteral("list_files"),
              .display_name = QStringLiteral("List files"),
              .renderer_key = QStringLiteral("filesystem.list"),
              .description = QStringLiteral(
                  "Lists the immediate files and subdirectories of a directory within the user's home directory."),
              .input_schema =
                  QJsonObject{
                      {QStringLiteral("type"), QStringLiteral("object")},
                      {QStringLiteral("properties"),
                       QJsonObject{
                           {QStringLiteral("path"),
                            QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                        {QStringLiteral("description"),
                                         QStringLiteral(
                                             "Directory path to list, relative to or within the user's home directory "
                                             "(e.g. '~/Documents', '.', 'projects/foo').")}}}}},
                      {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}},
                  },
              .risk = ToolDefinition::ToolRisk::Safe},
      .executor = std::make_shared<ListFilesExecutor>(),
      .presenter = std::make_shared<ListFilesPresenter>(),
      .legacy_aliases = {QStringLiteral("ListFiles")}};
}

ListFilesTool::ListFilesTool()
    : home_(canonicalizeHomeOrFallback(std::filesystem::path(QDir::homePath().toStdString()))) {}

ListFilesTool::ListFilesTool(const std::filesystem::path& homeOverride)
    : home_(canonicalizeHomeOrFallback(homeOverride)) {}

QString ListFilesTool::name() const { return QStringLiteral("ListFiles"); }

QString ListFilesTool::description() const {
  return QStringLiteral(
      "Lists the immediate files and subdirectories of a directory within the user's home "
      "directory. Does not recurse into subdirectories or read file contents.");
}

QJsonObject ListFilesTool::schema() const {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("path"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                        {QStringLiteral("description"),
                         QStringLiteral("Directory path to list, relative to or within the user's home directory "
                                        "(e.g. '~/Documents', '.', 'projects/foo').")}}}}},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}},
  };
}

QJsonObject ListFilesTool::execute(const QJsonObject& parameters) {
  const QJsonValue pathValue = parameters.value(QStringLiteral("path"));
  if (!pathValue.isString()) {
    return makeError(QStringLiteral("INVALID_PARAMETERS"), QStringLiteral("\"path\" must be a string."));
  }

  QString rawPath = pathValue.toString();
  if (rawPath.startsWith(QLatin1Char('~'))) {
    rawPath = QDir::homePath() + rawPath.mid(1);
  }

  std::filesystem::path requested(rawPath.toStdString());
  if (requested.is_relative()) {
    requested = home_ / requested;
  }

  std::error_code errorCode;
  const std::filesystem::path canonical = std::filesystem::canonical(requested, errorCode);
  if (errorCode) {
    return notFoundError();
  }

  // Apply the home-directory boundary before revealing whether an existing path is a file or
  // directory. Otherwise an external file such as /etc/passwd returns NOT_A_DIRECTORY while a
  // missing path returns NOT_FOUND, creating an existence oracle.
  const std::string canonicalStr = canonical.string();
  const std::string homeStr = home_.string();
  const bool inBounds =
      canonicalStr == homeStr ||
      canonicalStr.starts_with(homeStr + static_cast<char>(std::filesystem::path::preferred_separator));
  if (!inBounds) {
    return notFoundError();
  }

  const bool isDirectory = std::filesystem::is_directory(canonical, errorCode);
  if (errorCode) {
    return notFoundError();
  }
  if (!isDirectory) {
    return makeError(QStringLiteral("NOT_A_DIRECTORY"), QStringLiteral("Path is not a directory."));
  }

  std::filesystem::directory_iterator iterator(canonical, errorCode);
  if (errorCode) {
    return makeError(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("Permission denied reading directory."));
  }

  std::vector<Entry> entries;
  const std::filesystem::directory_iterator end;
  while (iterator != end) {
    const QString entryName = QString::fromStdString(iterator->path().filename().string());
    if (!entryName.startsWith(QLatin1Char('.'))) {
      std::error_code typeEc;
      const bool entryIsDirectory = iterator->is_directory(typeEc);
      entries.push_back(Entry{
          .name = entryName,
          .type = (!typeEc && entryIsDirectory) ? QStringLiteral("directory") : QStringLiteral("file"),
      });
    }
    iterator.increment(errorCode);
    if (errorCode) {
      break;
    }
  }

  if (errorCode) {
    if (errorCode == std::errc::permission_denied) {
      return makeError(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("Permission denied reading directory."));
    }
    return notFoundError();
  }

  std::ranges::sort(entries, [](const Entry& lhs, const Entry& rhs) { return lhs.name < rhs.name; });

  QJsonArray array;
  for (const auto& entry : entries) {
    array.append(QJsonObject{{QStringLiteral("name"), entry.name}, {QStringLiteral("type"), entry.type}});
  }
  return QJsonObject{{QStringLiteral("entries"), array}};
}

}  // namespace holonight_application
