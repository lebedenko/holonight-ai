#include "holonight_providers/anthropic_provider.h"

#include "cancellation_aware_handle.h"
#include "holonight_providers/anthropic_tool_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>
#include <QStringList>

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

namespace holonight_providers {

using holonight_domain::ContentDelta;
using holonight_domain::Message;
using holonight_domain::MessageRole;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

namespace {

constexpr auto kModelDiscoveryTimeout = std::chrono::seconds{10};

struct StreamContext {
  // Accumulates a tool_use content block's streamed input_json_delta fragments. The concatenated
  // partial_json string is only guaranteed to be valid JSON once content_block_stop fires for this
  // block's index -- individual fragments are not independently parseable.
  struct ToolUseAccumulator {
    QString id;
    QString name;
    QString partial_json;
    bool provider_hosted = false;
  };

  QByteArray buffer;
  QString provider_instance_id;
  HttpRequestHandlePtr handle;
  QString stop_reason;
  bool has_visible_content = false;
  bool terminal = false;
  bool completed = false;
  holonight_domain::Usage usage;
  std::optional<QString> model_identifier;
  QHash<int, ToolUseAccumulator> tool_use_blocks;  // keyed by SSE "index"
  bool has_tool_call = false;
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

// Fail-open denylist (REQ-F-010/011): any model id NOT matching one of these substrings is
// surfaced, so newly released Anthropic chat models appear automatically without a code release.
constexpr std::array<QLatin1StringView, 4> kDenylistedSubstrings{
    QLatin1StringView("embedding"),
    QLatin1StringView("moderation"),
    QLatin1StringView("vision"),
    QLatin1StringView("ocr"),
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

// Anthropic error responses are always JSON-shaped ({"type":"error","error":{"type":...,
// "message":...}}). Extracts just the human-readable message so callers never surface the raw
// JSON blob directly in the UI (REQ-F-018).
QString extractAnthropicErrorMessage(const QJsonObject& object) {
  const QString message = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("Anthropic returned an error") : message;
}

// Byte-array sibling of the overload above, used to recover a JSON error body delivered via
// HttpClient's transport-level on_error callback rather than an in-stream "error" SSE event.
QString extractAnthropicErrorMessage(const QByteArray& raw_body, const QString& fallback) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw_body, &parseError);
  if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
    const QString message =
        doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
      return message;
    }
  }
  return fallback;
}

// message_start reports the initial input_tokens and a partial output_tokens, later overwritten
// by message_delta's authoritative final value (REQ-F-005).
void applyMessageStartUsage(const QJsonObject& object, holonight_domain::Usage& usage) {
  const QJsonObject messageObject = object.value(QStringLiteral("message")).toObject();
  const QJsonObject usageObject = messageObject.value(QStringLiteral("usage")).toObject();
  if (usageObject.contains(QStringLiteral("input_tokens"))) {
    usage.input_tokens = usageObject.value(QStringLiteral("input_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("output_tokens"))) {
    usage.output_tokens = usageObject.value(QStringLiteral("output_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("cache_creation_input_tokens"))) {
    usage.cache_creation_tokens = usageObject.value(QStringLiteral("cache_creation_input_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("cache_read_input_tokens"))) {
    usage.cache_read_tokens = usageObject.value(QStringLiteral("cache_read_input_tokens")).toInt();
  }
}

// message_delta carries the final cumulative usage — it replaces (not sums with) whatever
// message_start reported (REQ-F-005).
void applyMessageDeltaUsage(const QJsonObject& object, holonight_domain::Usage& usage) {
  const QJsonObject usageObject = object.value(QStringLiteral("usage")).toObject();
  if (usageObject.contains(QStringLiteral("output_tokens"))) {
    usage.output_tokens = usageObject.value(QStringLiteral("output_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("cache_creation_input_tokens"))) {
    usage.cache_creation_tokens = usageObject.value(QStringLiteral("cache_creation_input_tokens")).toInt();
  }
  if (usageObject.contains(QStringLiteral("cache_read_input_tokens"))) {
    usage.cache_read_tokens = usageObject.value(QStringLiteral("cache_read_input_tokens")).toInt();
  }
}

// content_block_start: records a tool_use block's id/name so later input_json_delta fragments
// (keyed by the same "index") have somewhere to accumulate. Non-tool_use blocks (e.g. "text") need
// no bookkeeping here.
void handleContentBlockStart(const QJsonObject& object, const std::shared_ptr<StreamContext>& context) {
  const int index = object.value(QStringLiteral("index")).toInt();
  const QJsonObject block = object.value(QStringLiteral("content_block")).toObject();
  const QString blockType = block.value(QStringLiteral("type")).toString();
  if (blockType == QStringLiteral("tool_use") || blockType == QStringLiteral("server_tool_use")) {
    context->tool_use_blocks[index] =
        StreamContext::ToolUseAccumulator{.id = block.value(QStringLiteral("id")).toString(),
                                          .name = block.value(QStringLiteral("name")).toString(),
                                          .partial_json = QString(),
                                          .provider_hosted = blockType == QStringLiteral("server_tool_use")};
  }
}

void handleContentBlockDelta(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                             const std::function<void(const StreamEvent&)>& on_event) {  // REQ-F-015
  const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
  const QString deltaType = delta.value(QStringLiteral("type")).toString();
  if (deltaType == QStringLiteral("text_delta")) {
    const QString text = delta.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
      context->has_visible_content = true;
    }
    on_event(StreamEvent{ContentDelta{text}});
    return;
  }
  if (deltaType == QStringLiteral("input_json_delta")) {
    const int index = object.value(QStringLiteral("index")).toInt();
    auto accumulatorIt = context->tool_use_blocks.find(index);
    if (accumulatorIt != context->tool_use_blocks.end()) {
      accumulatorIt->partial_json += delta.value(QStringLiteral("partial_json")).toString();
    }
    return;
  }
  // Any other delta.type is a deliberate no-op.
}

// content_block_stop: the accumulated partial_json for a tool_use block is only guaranteed to be
// valid JSON once this fires -- parses it, emits StreamEvent::ToolCall, and forgets the
// accumulator. Blocks the app never saw a content_block_start for (defensive) are a no-op.
void handleContentBlockStop(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                            const std::function<void(const StreamEvent&)>& on_event) {
  const int index = object.value(QStringLiteral("index")).toInt();
  auto accumulatorIt = context->tool_use_blocks.find(index);
  if (accumulatorIt == context->tool_use_blocks.end()) {
    return;
  }

  const QByteArray raw =
      accumulatorIt->partial_json.isEmpty() ? QByteArrayLiteral("{}") : accumulatorIt->partial_json.toUtf8();
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    failStream(context, on_event,
               QStringLiteral("Malformed tool input from Anthropic: %1").arg(parseError.errorString()));
    return;
  }
  context->has_tool_call = true;
  on_event(StreamEvent{AnthropicToolCodec::decodeRequest(context->provider_instance_id, accumulatorIt->id,
                                                         accumulatorIt->name, doc.object(),
                                                         accumulatorIt->provider_hosted)});
  context->tool_use_blocks.erase(accumulatorIt);
}

void routeSseEvent(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  const QString type = object.value(QStringLiteral("type")).toString();

  if (type == QStringLiteral("content_block_start")) {
    handleContentBlockStart(object, context);
    return;
  }

  if (type == QStringLiteral("content_block_delta")) {
    handleContentBlockDelta(object, context, on_event);
    return;
  }

  if (type == QStringLiteral("content_block_stop")) {
    handleContentBlockStop(object, context, on_event);
    return;
  }

  if (type == QStringLiteral("message_stop")) {  // REQ-F-016
    if (context->stop_reason == QStringLiteral("max_tokens")) {
      failStream(context, on_event,
                 QStringLiteral("Anthropic response was truncated after reaching the maximum output token limit"));
      return;
    }
    if (!context->has_visible_content && !context->has_tool_call) {
      failStream(context, on_event, QStringLiteral("Anthropic completed without returning text content"));
      return;
    }
    context->terminal = true;
    context->completed = true;
    if (context->usage.input_tokens && context->usage.output_tokens) {
      context->usage.total_tokens = *context->usage.input_tokens + *context->usage.output_tokens;
    }
    on_event(StreamEvent{
        holonight_domain::Completed{.usage = context->usage, .model_identifier = context->model_identifier}});
    return;
  }

  if (type == QStringLiteral("message_delta")) {
    const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
    const QString stopReason = delta.value(QStringLiteral("stop_reason")).toString();
    if (!stopReason.isEmpty()) {
      context->stop_reason = stopReason;
    }

    applyMessageDeltaUsage(object, context->usage);
    return;
  }

  if (type == QStringLiteral("message_start")) {
    applyMessageStartUsage(object, context->usage);
    const QJsonObject messageObject = object.value(QStringLiteral("message")).toObject();
    if (messageObject.contains(QStringLiteral("model"))) {
      context->model_identifier = messageObject.value(QStringLiteral("model")).toString();
    }
    return;
  }

  if (type == QStringLiteral("error")) {  // REQ-F-018
    failStream(context, on_event, extractAnthropicErrorMessage(object));
    return;
  }

  // Everything else — ping, and any future event type — is a deliberate no-op (REQ-F-019).
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
    failStream(context, on_event,
               QStringLiteral("Malformed response from Anthropic: %1").arg(parseError.errorString()));
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

AnthropicProvider::AnthropicProvider(std::shared_ptr<HttpClient> http_client, QString base_url, QString instance_id)
    : http_client_(std::move(http_client)), instance_id_(std::move(instance_id)), base_url_(std::move(base_url)) {}

const std::vector<ModelId>& AnthropicProvider::availableModels() const { return available_models_; }

void AnthropicProvider::restoreAvailableModels(std::vector<ModelId> models) { available_models_ = std::move(models); }

void AnthropicProvider::refresh(const std::function<void()>& on_complete) {
  fetchModelList(on_complete, [on_complete](const QString& /*reason*/) {
    if (on_complete) {
      on_complete();
    }
  });
}

void AnthropicProvider::refresh(const std::function<void()>& on_success,
                                const std::function<void(const QString&)>& on_error) {
  fetchModelList(on_success, on_error);
}

void AnthropicProvider::setBaseUrl(QString base_url) { base_url_ = std::move(base_url); }

const QString& AnthropicProvider::baseUrl() const { return base_url_; }

void AnthropicProvider::setAuthKey(QString auth_key) { auth_key_ = std::move(auth_key); }

void AnthropicProvider::setTemperature(double temperature) { temperature_ = temperature; }

void AnthropicProvider::setMaxOutputTokens(int max_output_tokens) { max_output_tokens_ = max_output_tokens; }

QHash<QString, QString> AnthropicProvider::requestHeaders() const {
  QHash<QString, QString> headers{{QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")}};
  if (!auth_key_.isEmpty()) {
    headers.insert(QStringLiteral("x-api-key"), auth_key_);  // REQ-F-001/038
  }
  return headers;
}

void AnthropicProvider::fetchModelList(const std::function<void()>& on_success,
                                       const std::function<void(const QString&)>& on_error) {
  const HttpRequest request{
      .method = HttpMethod::Get, .url = base_url_ + QStringLiteral("/v1/models"), .headers = requestHeaders()};

  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): false positive — mirrors
  // OpenAIProvider::fetchModelList()'s identical nested-std::function-copy pattern.
  http_client_->send(
      request,
      [this, on_success, on_error](const QByteArray& body) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
            !doc.object().value(QStringLiteral("data")).isArray()) {
          if (on_error) {
            on_error(QStringLiteral("Malformed model list response from Anthropic"));
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
          on_error(extractAnthropicErrorMessage(error.toUtf8(), error));
        }
      },
      kModelDiscoveryTimeout);
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
}

HttpRequestHandlePtr AnthropicProvider::sendChat(const ModelId& model, const std::vector<Message>& history,
                                                 const std::function<void(const StreamEvent&)>& on_event,
                                                 std::chrono::milliseconds idle_timeout,
                                                 const holonight_domain::ToolCatalogSnapshot& tool_catalog) {
  QStringList systemParts;
  const QJsonArray messages = AnthropicToolCodec::encodeHistory(history, systemParts);

  QJsonObject body;
  body[QStringLiteral("model")] = model.model_name;
  body[QStringLiteral("messages")] = messages;
  body[QStringLiteral("max_tokens")] = max_output_tokens_;  // REQ-F-007: always present.
  body[QStringLiteral("temperature")] = temperature_;       // REQ-F-006
  body[QStringLiteral("stream")] = true;
  if (!systemParts.isEmpty()) {
    body[QStringLiteral("system")] = systemParts.join(QStringLiteral("\n\n"));  // REQ-F-004/005
  }
  const QJsonArray tools = AnthropicToolCodec::encodeDefinitions(tool_catalog);
  if (!tools.isEmpty()) {
    body[QStringLiteral("tools")] = tools;
  }
  // Deliberately absent: tool_choice, thinking, budget_tokens, top_k, top_p, metadata,
  // stop_sequences, betas (REQ-C-004).

  const HttpRequest request{.method = HttpMethod::Post,
                            .url = base_url_ + QStringLiteral("/v1/messages"),
                            .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                            .content_type = QStringLiteral("application/json"),
                            .headers = requestHeaders()};

  auto context = std::make_shared<StreamContext>();
  context->provider_instance_id = instance_id_;
  context->model_identifier = model.model_name;

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
      failStream(context, on_event, QStringLiteral("Stream ended without completion"));  // REQ-F-018
    }
  };

  auto onError = [context, on_event](const QString& transportMessage) {
    if (context->terminal) {
      return;
    }
    failStream(context, on_event, extractAnthropicErrorMessage(context->buffer, transportMessage));  // REQ-F-023
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
