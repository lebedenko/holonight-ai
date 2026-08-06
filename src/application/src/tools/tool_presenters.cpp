#include "holonight_application/tools/tool_presenters.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <holonight_domain/tool_activity.h>

namespace holonight_application {
namespace {

[[nodiscard]] QString statusToText(holonight_domain::ToolInvocationStatus status) {
  using Status = holonight_domain::ToolInvocationStatus;

  switch (status) {
    case Status::Requested:
      return QStringLiteral("Requested");
    case Status::AwaitingApproval:
      return QStringLiteral("Awaiting Approval");
    case Status::Running:
      return QStringLiteral("Running");
    case Status::Completed:
      return QStringLiteral("Completed");
    case Status::Failed:
      return QStringLiteral("Failed");
    case Status::Denied:
      return QStringLiteral("Denied");
    case Status::Cancelled:
      return QStringLiteral("Cancelled");
  }

  return QStringLiteral("Unknown");
}

[[nodiscard]] QString statusSummary(holonight_domain::ToolInvocationStatus status, const QString& title,
                                    bool is_error = false) {
  using Status = holonight_domain::ToolInvocationStatus;

  if (title.isEmpty()) {
    return statusToText(status);
  }

  if (status == Status::Completed) {
    return QStringLiteral("%1 completed").arg(title);
  }
  if (status == Status::Failed || status == Status::Denied || status == Status::Cancelled || is_error) {
    return QStringLiteral("%1 failed").arg(title);
  }

  if (status == Status::Running) {
    return QStringLiteral("%1 running").arg(title);
  }
  if (status == Status::AwaitingApproval) {
    return QStringLiteral("%1 awaiting approval").arg(title);
  }

  if (is_error) {
    return QStringLiteral("%1 failed").arg(title);
  }

  return QStringLiteral("%1 requested").arg(title);
}

[[nodiscard]] QString jsonCompact(const QJsonValue& value) {
  if (value.isObject()) {
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
  }
  if (value.isArray()) {
    return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
  }
  if (value.isUndefined() || value.isNull()) {
    return {};
  }

  QJsonObject wrapper;
  wrapper.insert(QStringLiteral("value"), QJsonValue::fromVariant(value.toVariant()));
  return QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QVariant valueToVariant(const QJsonValue& value) {
  if (value.isObject()) {
    return value.toObject().toVariantMap();
  }
  if (value.isArray()) {
    return value.toArray().toVariantList();
  }
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    return value.toDouble();
  }
  if (value.isBool()) {
    return value.toBool();
  }
  if (value.isNull()) {
    return {};
  }
  return QVariant::fromValue(value.toVariant());
}

[[nodiscard]] QString displayName(const holonight_domain::ToolInvocation& invocation) {
  if (!invocation.function_name.isEmpty()) {
    return invocation.function_name;
  }
  if (!invocation.tool_id.isEmpty()) {
    return invocation.tool_id;
  }
  return QStringLiteral("Tool activity");
}

[[nodiscard]] QString toolPathFromInvocation(const holonight_domain::ToolInvocation& invocation) {
  if (!invocation.arguments.isObject()) {
    return QStringLiteral("(current)");
  }
  const QJsonValue pathValue = invocation.arguments.toObject().value(QStringLiteral("path"));
  return pathValue.toString(QStringLiteral("(current)"));
}

[[nodiscard]] QString fileFolderSummary(int file_count, int directory_count) {
  if (file_count == 0 && directory_count == 0) {
    return QStringLiteral("No entries");
  }
  return QStringLiteral("%1 files, %2 folders").arg(file_count).arg(directory_count);
}

[[nodiscard]] QString normalizedType(const QString& type, QString& icon) {
  if (type == QStringLiteral("directory") || type == QStringLiteral("dir")) {
    icon = QStringLiteral("folder");
    return QStringLiteral("directory");
  }

  if (type == QStringLiteral("file")) {
    icon = QStringLiteral("description");
    return QStringLiteral("file");
  }

  icon = QStringLiteral("help");
  return QStringLiteral("unknown");
}

[[nodiscard]] QVariantList parseListFilesEntries(const QJsonArray& entries, int& fileCount, int& directoryCount) {
  fileCount = 0;
  directoryCount = 0;
  QVariantList parsed;
  parsed.reserve(static_cast<int>(entries.size()));

  for (const auto& value : entries) {
    QVariantMap entryData;
    QString icon = QStringLiteral("help");
    QString kind = QStringLiteral("unknown");
    QString name;

    if (!value.isObject()) {
      name = QStringLiteral("(malformed entry)");
      kind = QStringLiteral("unknown");
    } else {
      const QJsonObject object = value.toObject();
      name = object.value(QStringLiteral("name")).toString();
      kind = normalizedType(object.value(QStringLiteral("type")).toString(), icon);
      if (kind == QStringLiteral("directory")) {
        ++directoryCount;
      } else if (kind == QStringLiteral("file")) {
        ++fileCount;
      }
      if (name.isEmpty()) {
        name = QStringLiteral("(missing name)");
      }
    }

    entryData.insert(QStringLiteral("name"), name);
    entryData.insert(QStringLiteral("kind"), kind);
    entryData.insert(QStringLiteral("iconName"), icon);
    parsed.append(entryData);
  }

  return parsed;
}

namespace {

[[nodiscard]] bool hasListFilesError(const holonight_domain::ToolInvocation& invocation, bool& terminalError,
                                     QString& errorCode, QString& errorMessage) {
  bool foundError = false;
  if (invocation.error.has_value()) {
    foundError = true;
    terminalError = true;
    errorCode = invocation.error->code;
    errorMessage = invocation.error->message;
    return foundError;
  }

  if (!invocation.result.has_value()) {
    return false;
  }

  const auto& result = *invocation.result;
  if (!result.isObject()) {
    return false;
  }

  const QJsonObject resultObject = result.toObject();
  const QJsonValue error = resultObject.value(QStringLiteral("error"));
  if (error.isObject()) {
    foundError = true;
    terminalError = true;
    const QJsonObject errorObject = error.toObject();
    errorCode = errorObject.value(QStringLiteral("code")).toString();
    errorMessage = errorObject.value(QStringLiteral("message")).toString();
  }

  return foundError;
}

}  // namespace

}  // namespace

ToolPresentation GenericToolPresenter::present(const holonight_domain::ToolInvocation& invocation) const {
  const QString title = displayName(invocation);
  const bool resultHasError = invocation.result.has_value() && invocation.result->isObject() &&
                              invocation.result->toObject().contains(QStringLiteral("error"));
  const bool isError = invocation.error.has_value() || resultHasError;
  const bool isTerminal = invocation.status == holonight_domain::ToolInvocationStatus::Completed ||
                          invocation.status == holonight_domain::ToolInvocationStatus::Failed ||
                          invocation.status == holonight_domain::ToolInvocationStatus::Denied ||
                          invocation.status == holonight_domain::ToolInvocationStatus::Cancelled;

  QVariantMap detailData;
  detailData.insert(QStringLiteral("toolId"), invocation.tool_id);
  detailData.insert(QStringLiteral("functionName"), invocation.function_name);
  detailData.insert(QStringLiteral("arguments"), valueToVariant(invocation.arguments));
  if (invocation.result.has_value()) {
    detailData.insert(QStringLiteral("result"), valueToVariant(*invocation.result));
  }
  detailData.insert(QStringLiteral("statusText"), statusToText(invocation.status));

  return ToolPresentation{
      .renderer_key = invocation.tool_id.isEmpty() ? QStringLiteral("generic") : invocation.tool_id,
      .icon_name = QStringLiteral("code"),
      .title = title,
      .summary = isTerminal ? statusSummary(invocation.status, title, isError)
                            : statusSummary(invocation.status, title, isError),
      .status_text = statusToText(invocation.status),
      .detail_data = std::move(detailData),
      .raw_arguments_json = jsonCompact(invocation.arguments),
      .raw_result_json = invocation.result.has_value() ? jsonCompact(*invocation.result) : QString(),
      .is_error = isError};
}

ToolPresentation ListFilesPresenter::present(const holonight_domain::ToolInvocation& invocation) const {
  const QString toolPath = toolPathFromInvocation(invocation);
  const QString baseTitle = toolPath.isEmpty() ? QStringLiteral("Directory") : toolPath;

  int fileCount = 0;
  int directoryCount = 0;
  QVariantList entries;
  QString errorCode;
  QString errorMessage;
  bool isTerminalFailure = false;
  if (invocation.result.has_value() && invocation.result->isObject()) {
    const QJsonObject resultObject = invocation.result->toObject();
    const QJsonArray resultEntries = resultObject.value(QStringLiteral("entries")).toArray();
    if (!resultEntries.isEmpty() || resultObject.contains(QStringLiteral("entries"))) {
      entries = parseListFilesEntries(resultEntries, fileCount, directoryCount);
    }
  }

  const bool hasResultError = hasListFilesError(invocation, isTerminalFailure, errorCode, errorMessage);
  if (invocation.status == holonight_domain::ToolInvocationStatus::Failed ||
      invocation.status == holonight_domain::ToolInvocationStatus::Denied ||
      invocation.status == holonight_domain::ToolInvocationStatus::Cancelled) {
    isTerminalFailure = true;
  }

  const bool malformedResult = invocation.result.has_value() && !invocation.result->isObject() &&
                               !invocation.result->isUndefined() && !invocation.result->isNull();
  if (!isTerminalFailure && malformedResult && !hasResultError) {
    isTerminalFailure = true;
    errorCode = QStringLiteral("INVALID_RESULT");
    errorMessage = QStringLiteral("ListFiles result was not an object.");
  }

  if (!isTerminalFailure && hasResultError) {
    isTerminalFailure = true;
  }

  QString title;
  if (invocation.status == holonight_domain::ToolInvocationStatus::Requested ||
      invocation.status == holonight_domain::ToolInvocationStatus::AwaitingApproval ||
      invocation.status == holonight_domain::ToolInvocationStatus::Running) {
    title = QStringLiteral("Listing %1…").arg(baseTitle);
  } else if (isTerminalFailure) {
    title = QStringLiteral("Couldn’t list %1").arg(baseTitle);
  } else {
    title = QStringLiteral("Listed %1").arg(baseTitle);
  }

  const bool terminal = isTerminalFailure;
  const QString summary =
      terminal ? (errorMessage.isEmpty() ? QStringLiteral("Listing failed") : QStringLiteral("%1").arg(errorMessage))
               : fileFolderSummary(fileCount, directoryCount);

  QVariantMap detailData;
  detailData.insert(QStringLiteral("path"), toolPath);
  detailData.insert(QStringLiteral("fileCount"), fileCount);
  detailData.insert(QStringLiteral("directoryCount"), directoryCount);
  detailData.insert(QStringLiteral("entries"), entries);
  if (!errorCode.isEmpty()) {
    detailData.insert(QStringLiteral("errorCode"), errorCode);
  }
  if (!errorMessage.isEmpty()) {
    detailData.insert(QStringLiteral("errorMessage"), errorMessage);
  }

  return ToolPresentation{
      .renderer_key = QStringLiteral("filesystem.list"),
      .icon_name = QStringLiteral("folder"),
      .title = title,
      .summary = summary,
      .status_text = statusToText(invocation.status),
      .detail_data = std::move(detailData),
      .raw_arguments_json = jsonCompact(invocation.arguments),
      .raw_result_json = invocation.result.has_value() ? jsonCompact(*invocation.result) : QString(),
      .is_error = terminal,
      .can_cancel = false};
}

}  // namespace holonight_application
