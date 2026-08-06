#include "holonight_providers/openai_provider.h"

#include "cancellation_aware_handle.h"
#include "holonight_providers/openai_tool_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>

#include <algorithm>
#include <array>
#include <chrono>
#include <tuple>
#include <utility>

namespace holonight_providers {

using holonight_domain::ContentDelta;
using holonight_domain::Message;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;

namespace {

constexpr auto kModelDiscoveryTimeout = std::chrono::seconds{10};

struct StreamContext {
  QByteArray buffer;
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
  holonight_domain::Usage usage;
  std::optional<QString> model_identifier;
  QString provider_instance_id;
};

void failStream(const std::shared_ptr<StreamContext>& context, const std::function<void(const StreamEvent&)>& on_event,
                QString message) {
  if (context->terminal) {
    return;
  }

  context->terminal = true;
  if (context->handle) {
    context->handle->cancel();
  }
  on_event(StreamEvent{holonight_domain::Error{
      .message = std::move(message), .usage = context->usage, .model_identifier = context->model_identifier}});
}

// REQ-F-004: extracts the usage object nested under response.completed's "response" field.
void populateUsageFromResponseCompleted(const QJsonObject& response, holonight_domain::Usage& usage) {
  const QJsonObject usageObject = response.value(QStringLiteral("usage")).toObject();
  if (usageObject.contains(QStringLiteral("input_tokens"))) {
    usage.input_tokens = usageObject.value(QStringLiteral("input_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("output_tokens"))) {
    usage.output_tokens = usageObject.value(QStringLiteral("output_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("total_tokens"))) {
    usage.total_tokens = usageObject.value(QStringLiteral("total_tokens")).toInt();
  }

  const QJsonObject inputDetails = usageObject.value(QStringLiteral("input_tokens_details")).toObject();
  if (inputDetails.contains(QStringLiteral("cached_tokens"))) {
    usage.cache_read_tokens = inputDetails.value(QStringLiteral("cached_tokens")).toInt();
  }
  if (inputDetails.contains(QStringLiteral("cache_write_tokens"))) {
    usage.cache_creation_tokens = inputDetails.value(QStringLiteral("cache_write_tokens")).toInt();
  }

  const QJsonObject outputDetails = usageObject.value(QStringLiteral("output_tokens_details")).toObject();
  if (outputDetails.contains(QStringLiteral("reasoning_tokens"))) {
    usage.reasoning_tokens = outputDetails.value(QStringLiteral("reasoning_tokens")).toInt();
  }
}

// Fail-open denylist (REQ-F-017/018): any model id NOT matching one of these substrings is
// surfaced, so newly released OpenAI chat models appear automatically without a code release.
constexpr std::array<QLatin1StringView, 10> kDenylistedSubstrings{
    QLatin1StringView("embedding-"),      QLatin1StringView("tts-"),        QLatin1StringView("whisper-"),
    QLatin1StringView("dall-e"),          QLatin1StringView("gpt-image"),   QLatin1StringView("omni-moderation"),
    QLatin1StringView("text-moderation"), QLatin1StringView("davinci-002"), QLatin1StringView("babbage-002"),
    QLatin1StringView("sora-"),
};

bool isDenylistedModel(const QString& model_id) {
  return std::ranges::any_of(kDenylistedSubstrings,
                             [&model_id](const auto& substring) { return model_id.contains(substring); });
}

// Extracts the SSE "data:" payload from one blank-line-delimited block. Tolerates CRLF line
// endings and the optional single space after the colon (SSE spec); ignores event:/id:/comment
// lines since the payload's own "type" field is authoritative for routing. Multiple data: lines
// within one block are joined with '\n', per the SSE multi-line-data convention.
QString extractSseDataPayload(const QByteArray& block) {
  QString payload;
  const QList<QByteArray> lines = block.split('\n');
  for (const QByteArray& rawLine : lines) {
    QByteArray line = rawLine;
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (!line.startsWith("data:")) {
      continue;
    }
    QByteArray value = line.mid(5);
    if (value.startsWith(' ')) {
      value.remove(0, 1);
    }
    if (!payload.isEmpty()) {
      payload += QLatin1Char('\n');
    }
    payload += QString::fromUtf8(value);
  }
  return payload;
}

void routeSseEvent(const QJsonObject& event, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  const QString type = event.value(QStringLiteral("type")).toString();

  if (type == QStringLiteral("response.output_text.delta") || type == QStringLiteral("response.refusal.delta")) {
    on_event(StreamEvent{ContentDelta{event.value(QStringLiteral("delta")).toString()}});
    return;
  }

  if (type == QStringLiteral("response.completed")) {
    const QJsonObject response = event.value(QStringLiteral("response")).toObject();
    populateUsageFromResponseCompleted(response, context->usage);
    if (response.contains(QStringLiteral("model"))) {
      context->model_identifier = response.value(QStringLiteral("model")).toString();
    }
    const auto requests = OpenAIToolCodec::decodeRequests(context->provider_instance_id,
                                                          response.value(QStringLiteral("output")).toArray());
    if (!requests.has_value()) {
      failStream(context, on_event, requests.error());
      return;
    }
    for (const auto& request : *requests) {
      on_event(StreamEvent{request});
    }
    context->terminal = true;
    context->completed = true;
    on_event(StreamEvent{
        holonight_domain::Completed{.usage = context->usage, .model_identifier = context->model_identifier}});
    return;
  }

  if (type == QStringLiteral("response.failed") || type == QStringLiteral("response.incomplete")) {
    const QJsonObject response = event.value(QStringLiteral("response")).toObject();
    QJsonObject error = response.value(QStringLiteral("error")).toObject();
    if (error.isEmpty()) {
      error = event.value(QStringLiteral("error")).toObject();
    }
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty()) {
      message = QStringLiteral("OpenAI response did not complete");
    }
    failStream(context, on_event, message);
    return;
  }

  if (type == QStringLiteral("error") || (type.isEmpty() && event.contains(QStringLiteral("error")))) {
    const QJsonObject error = event.value(QStringLiteral("error")).toObject();
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty()) {
      message = QStringLiteral("OpenAI returned an error");
    }
    failStream(context, on_event, message);
    return;
  }

  // Everything else (response.created, response.in_progress, response.output_item.*,
  // response.content_part.*, response.function_call_arguments.*, annotation events, ...) is
  // silently ignored — no tool-calling/reasoning support in this cycle (REQ-F-015, REQ-C-001).
}

// OpenAI error responses are always JSON-shaped ({"error":{"message":...,"type":...}}). Extracts
// just the human-readable message so callers never surface the raw JSON blob (with its escaped
// braces/quotes/newlines) directly in the UI. Falls back to `fallback` (the raw transport-level
// message) when `raw_body` isn't parseable JSON or has no non-empty error.message.
QString extractOpenAiErrorMessage(const QByteArray& raw_body, const QString& fallback) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw_body, &parseError);
  if (parseError.error == QJsonParseError::NoError && doc.isObject() &&
      doc.object().contains(QStringLiteral("error"))) {
    const QJsonObject error = doc.object().value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
      return message;
    }
  }
  return fallback;
}

void processSseBlock(const QByteArray& block, const std::shared_ptr<StreamContext>& context,
                     const std::function<void(const StreamEvent&)>& on_event) {
  const QString payload = extractSseDataPayload(block);
  if (payload.isEmpty()) {
    return;
  }

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    failStream(context, on_event, QStringLiteral("Malformed response from OpenAI: %1").arg(parseError.errorString()));
    return;
  }

  routeSseEvent(doc.object(), context, on_event);
}

std::pair<qsizetype, qsizetype> findSseBlockDelimiter(const QByteArray& buffer) {
  const qsizetype lfDelimiter = buffer.indexOf("\n\n");
  const qsizetype crlfDelimiter = buffer.indexOf("\r\n\r\n");
  if (lfDelimiter == -1) {
    return {crlfDelimiter, 4};
  }
  if (crlfDelimiter == -1 || lfDelimiter < crlfDelimiter) {
    return {lfDelimiter, 2};
  }
  return {crlfDelimiter, 4};
}

}  // namespace

OpenAIProvider::OpenAIProvider(std::shared_ptr<HttpClient> http_client, QString base_url, QString instance_id)
    : http_client_(std::move(http_client)), instance_id_(std::move(instance_id)), base_url_(std::move(base_url)) {}

const std::vector<ModelId>& OpenAIProvider::availableModels() const { return available_models_; }

void OpenAIProvider::restoreAvailableModels(std::vector<ModelId> models) { available_models_ = std::move(models); }

void OpenAIProvider::refresh(const std::function<void()>& on_complete) {
  fetchModelList(on_complete, [on_complete](const QString& /*reason*/) {
    if (on_complete) {
      on_complete();
    }
  });
}

void OpenAIProvider::refresh(const std::function<void()>& on_success,
                             const std::function<void(const QString&)>& on_error) {
  fetchModelList(on_success, on_error);
}

void OpenAIProvider::setBaseUrl(QString base_url) { base_url_ = std::move(base_url); }

const QString& OpenAIProvider::baseUrl() const { return base_url_; }

void OpenAIProvider::setAuthToken(QString auth_token) { auth_token_ = std::move(auth_token); }

void OpenAIProvider::setTemperature(double temperature) { temperature_ = temperature; }

QHash<QString, QString> OpenAIProvider::authHeaders() const {
  if (auth_token_.isEmpty()) {
    return {};
  }
  return {{QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(auth_token_)}};
}

void OpenAIProvider::fetchModelList(const std::function<void()>& on_success,
                                    const std::function<void(const QString&)>& on_error) {
  const HttpRequest request{
      .method = HttpMethod::Get, .url = base_url_ + QStringLiteral("/models"), .headers = authHeaders()};

  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): false positive — mirrors
  // OllamaProvider::fetchModelList()'s identical nested-std::function-copy pattern.
  http_client_->send(
      request,
      [this, on_success, on_error](const QByteArray& body) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
            !doc.object().value(QStringLiteral("data")).isArray()) {
          if (on_error) {
            on_error(QStringLiteral("Malformed model list response from OpenAI"));
          }
          return;
        }

        const QJsonArray dataArray = doc.object().value(QStringLiteral("data")).toArray();
        std::vector<ModelId> models;
        models.reserve(static_cast<std::size_t>(dataArray.size()));
        for (const auto& entry : dataArray) {
          const QString modelId = entry.toObject().value(QStringLiteral("id")).toString();
          if (modelId.isEmpty() || isDenylistedModel(modelId)) {
            continue;
          }
          models.push_back(ModelId{.provider_id = instance_id_, .model_name = modelId});
        }
        available_models_ = std::move(models);
        if (on_success) {
          on_success();
        }
      },
      [on_error](const QString& error) {
        if (on_error) {
          on_error(extractOpenAiErrorMessage(error.toUtf8(), error));
        }
      },
      kModelDiscoveryTimeout);
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
}

HttpRequestHandlePtr OpenAIProvider::sendChat(const ModelId& model, const std::vector<Message>& history,
                                              const std::function<void(const StreamEvent&)>& on_event,
                                              std::chrono::milliseconds idle_timeout,
                                              const holonight_domain::ToolCatalogSnapshot& tool_catalog) {
  const QJsonArray input = OpenAIToolCodec::encodeHistory(history);

  QJsonObject body;
  body[QStringLiteral("model")] = model.model_name;
  body[QStringLiteral("input")] = input;
  body[QStringLiteral("stream")] = true;
  body[QStringLiteral("store")] = false;
  body[QStringLiteral("temperature")] = temperature_;
  if (!tool_catalog.client_tools.empty()) {
    body[QStringLiteral("tools")] = OpenAIToolCodec::encodeDefinitions(tool_catalog);
    body[QStringLiteral("include")] = QJsonArray{QStringLiteral("reasoning.encrypted_content")};
  }
  // Deliberately absent: previous_response_id (REQ-F-004/C-005), max_output_tokens
  // (REQ-F-006/C-008), reasoning.effort (REQ-C-004).

  const HttpRequest request{.method = HttpMethod::Post,
                            .url = base_url_ + QStringLiteral("/responses"),
                            .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                            .content_type = QStringLiteral("application/json"),
                            .headers = authHeaders()};

  auto context = std::make_shared<StreamContext>();
  context->model_identifier = model.model_name;
  context->provider_instance_id = instance_id_;

  auto onData = [context, on_event](const QByteArray& chunk) {
    if (context->terminal) {
      return;
    }
    context->buffer += chunk;
    auto [blockEnd, delimiterSize] = findSseBlockDelimiter(context->buffer);
    while (blockEnd != -1) {
      const QByteArray block = context->buffer.left(blockEnd);
      context->buffer.remove(0, blockEnd + delimiterSize);
      processSseBlock(block, context, on_event);
      if (context->terminal) {
        return;
      }
      std::tie(blockEnd, delimiterSize) = findSseBlockDelimiter(context->buffer);
    }
  };

  auto onFinished = [context, on_event]() {
    if (context->terminal) {
      return;
    }

    const QByteArray remainingBlock = context->buffer.trimmed();
    if (!remainingBlock.isEmpty()) {
      processSseBlock(remainingBlock, context, on_event);
    }
    if (!context->completed && !context->terminal) {
      failStream(context, on_event, QStringLiteral("Stream ended without completion"));
    }
  };

  auto onError = [context, on_event](const QString& transportMessage) {
    if (context->terminal) {
      return;
    }
    failStream(context, on_event, extractOpenAiErrorMessage(context->buffer, transportMessage));
  };

  context->handle = http_client_->sendStreaming(request, idle_timeout, onData, onFinished, onError);
  return std::make_shared<detail::CancellationAwareHandle>(context->handle, [context, on_event] {
    if (context->terminal) {
      return;
    }
    context->terminal = true;
    on_event(StreamEvent{
        holonight_domain::Cancelled{.usage = context->usage, .model_identifier = context->model_identifier}});
  });
}

}  // namespace holonight_providers
