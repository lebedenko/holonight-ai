#include "holonight_providers/google_provider.h"

#include "cancellation_aware_handle.h"
#include "holonight_providers/google_tool_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
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
  QString provider_instance_id;
  HttpRequestHandlePtr handle;
  bool terminal = false;
  bool completed = false;
  holonight_domain::Usage usage;
  std::optional<QString> model_identifier;
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

// Gemini reports usageMetadata on every chunk, cumulative-to-date rather than delta — so each
// chunk's values simply overwrite the previous ones (REQ-F-006); the last chunk received before
// the terminal finishReason is authoritative, unlike Anthropic where two distinct fields combine.
void applyUsageMetadata(const QJsonObject& object, holonight_domain::Usage& usage) {
  if (!object.contains(QStringLiteral("usageMetadata"))) {
    return;
  }
  const QJsonObject usageObject = object.value(QStringLiteral("usageMetadata")).toObject();
  if (usageObject.contains(QStringLiteral("promptTokenCount"))) {
    usage.input_tokens = usageObject.value(QStringLiteral("promptTokenCount")).toInt();
  }
  if (usageObject.contains(QStringLiteral("candidatesTokenCount"))) {
    usage.output_tokens = usageObject.value(QStringLiteral("candidatesTokenCount")).toInt();
  }
  if (usageObject.contains(QStringLiteral("thoughtsTokenCount"))) {
    usage.reasoning_tokens = usageObject.value(QStringLiteral("thoughtsTokenCount")).toInt();
  }
  if (usageObject.contains(QStringLiteral("cachedContentTokenCount"))) {
    usage.cache_read_tokens = usageObject.value(QStringLiteral("cachedContentTokenCount")).toInt();
  }
  if (usageObject.contains(QStringLiteral("totalTokenCount"))) {
    usage.total_tokens = usageObject.value(QStringLiteral("totalTokenCount")).toInt();
  }
}

// Same four substrings every prior provider cycle's denylist has used (REQ-F-009's own text names
// them explicitly) — not extended speculatively with Gemini-specific guesses. Fail-open: any model
// id NOT matching one of these substrings is surfaced (REQ-F-010/011).
constexpr std::array<QLatin1StringView, 4> kDenylistedSubstrings{
    QLatin1StringView("embedding"),
    QLatin1StringView("moderation"),
    QLatin1StringView("vision"),
    QLatin1StringView("ocr"),
};

bool isDenylistedModel(const QString& raw_name) {
  return std::ranges::any_of(kDenylistedSubstrings,
                             [&raw_name](const auto& substring) { return raw_name.contains(substring); });
}

// New second filter dimension (DESIGN.md §5.7/§6): a model must also declare "generateContent"
// support. Missing or empty supportedGenerationMethods is treated as NOT supporting it (fail-closed
// on this one check, deliberately asymmetric with the denylist's fail-open philosophy).
bool supportsGenerateContent(const QJsonObject& entry) {
  const QJsonArray methods = entry.value(QStringLiteral("supportedGenerationMethods")).toArray();
  return std::ranges::any_of(
      methods, [](const QJsonValue& method) { return method.toString() == QStringLiteral("generateContent"); });
}

// Gemini's GET /v1beta/models returns resource names like "models/gemini-2.0-flash"; ModelId's
// cross-provider contract stores only the bare, URL-path-ready suffix (DESIGN.md §5.8) — sendChat()
// re-adds the literal "models/" segment when building the request URL.
QString stripModelsPrefix(QString name) {
  constexpr auto kPrefix = QLatin1StringView("models/");
  if (name.startsWith(kPrefix)) {
    name.remove(0, kPrefix.size());
  }
  return name;
}

// Extracts the SSE "data:" payload from one blank-line-delimited block. Tolerates CRLF line
// endings and the optional single space after the colon (SSE spec); ignores event:/id:/comment
// lines (Gemini's alt=sse stream does not emit named event: lines the way Anthropic's Messages API
// does — every block is implicitly a content update — so this is pure defensive tolerance).
// Multiple data: lines within one block are joined with '\n', per the SSE multi-line-data
// convention.
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

// Google error responses are JSON-shaped ({"error":{"code":...,"message":...,"status":...}}).
// Extracts just the human-readable message so callers never surface the raw JSON blob directly in
// the UI (REQ-F-018).
QString extractGoogleErrorMessage(const QJsonObject& object) {
  const QString message = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("Google returned an error") : message;
}

// Byte-array sibling of the overload above, used to recover a JSON error body delivered via
// HttpClient's transport-level on_error callback rather than an in-stream "error" SSE event.
QString extractGoogleErrorMessage(const QByteArray& raw_body, const QString& fallback) {
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

QString promptBlockMessage(const QString& block_reason) {
  if (block_reason == QStringLiteral("SAFETY")) {
    return QStringLiteral("Prompt blocked by Google's safety filters.");
  }
  if (block_reason == QStringLiteral("BLOCKLIST")) {
    return QStringLiteral("Prompt blocked because it contains a forbidden term.");
  }
  if (block_reason == QStringLiteral("PROHIBITED_CONTENT")) {
    return QStringLiteral("Prompt blocked because it contains prohibited content.");
  }
  return QStringLiteral("Prompt blocked by Google: %1.").arg(block_reason);
}

QString finishReasonMessage(const QString& finish_reason) {
  if (finish_reason == QStringLiteral("MAX_TOKENS")) {
    return QStringLiteral("Response stopped after reaching the maximum output-token limit.");
  }
  if (finish_reason == QStringLiteral("SAFETY")) {
    return QStringLiteral("Response blocked by Google's safety filters.");
  }
  if (finish_reason == QStringLiteral("RECITATION")) {
    return QStringLiteral("Response blocked due to recitation concerns.");
  }
  if (finish_reason == QStringLiteral("BLOCKLIST")) {
    return QStringLiteral("Response blocked because it contains a forbidden term.");
  }
  if (finish_reason == QStringLiteral("PROHIBITED_CONTENT")) {
    return QStringLiteral("Response blocked because it contains prohibited content.");
  }
  if (finish_reason == QStringLiteral("SPII")) {
    return QStringLiteral("Response blocked because it may contain sensitive personal information.");
  }
  return QStringLiteral("Google stopped the response: %1.").arg(finish_reason);
}

void routeSseEvent(const QJsonObject& object, const std::shared_ptr<StreamContext>& context,
                   const std::function<void(const StreamEvent&)>& on_event) {
  // Transport-level error object, distinct from a non-2xx HTTP status (REQ-F-018).
  if (object.contains(QStringLiteral("error"))) {
    failStream(context, on_event, extractGoogleErrorMessage(object));
    return;
  }

  applyUsageMetadata(object, context->usage);  // cumulative-per-chunk, last chunk wins (REQ-F-006)
  if (object.contains(QStringLiteral("modelVersion"))) {
    context->model_identifier = object.value(QStringLiteral("modelVersion")).toString();
  }

  const QJsonArray candidates = object.value(QStringLiteral("candidates")).toArray();
  if (candidates.isEmpty()) {
    const QString blockReason =
        object.value(QStringLiteral("promptFeedback")).toObject().value(QStringLiteral("blockReason")).toString();
    if (!blockReason.isEmpty() && blockReason != QStringLiteral("BLOCK_REASON_UNSPECIFIED")) {
      failStream(context, on_event, promptBlockMessage(blockReason));
    }
    // A usage-only or other partial block with no prompt block reason is an intermediate no-op.
    return;
  }
  const QJsonObject candidate = candidates.at(0).toObject();  // REQ-F-013: candidates[0] only

  // Extract and forward any text content BEFORE checking finishReason — Gemini's final block
  // commonly carries both the last text delta and the terminal finishReason together.
  const QJsonArray parts =
      candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray();
  QString deltaText;
  QJsonArray functionCallParts;  // Part-level objects, not just the inner functionCall sub-object.
  for (const auto& partValue : parts) {
    const QJsonObject part = partValue.toObject();
    if (part.contains(QStringLiteral("text"))) {
      deltaText += part.value(QStringLiteral("text")).toString();
    } else if (part.contains(QStringLiteral("functionCall"))) {
      functionCallParts.append(part);
    }
    // inlineData and any other part kind remain a deliberate no-op.
  }
  if (!deltaText.isEmpty()) {  // REQ-F-014
    on_event(StreamEvent{ContentDelta{deltaText}});
  }

  for (const auto& partValue : functionCallParts) {  // array order preserved
    auto request = GoogleToolCodec::decodeRequest(context->provider_instance_id, partValue.toObject());
    if (!request.has_value()) {
      failStream(context, on_event, QStringLiteral("Malformed function call from Google"));
      return;
    }
    on_event(StreamEvent{*request});
  }

  // Gemini includes "finishReason" on intermediate chunks too, set to the proto3 zero-value
  // "FINISH_REASON_UNSPECIFIED" rather than omitting the key — so key *presence* alone cannot
  // signal termination (that previously caused streams to be cut off after the first chunk).
  const QString finishReason = candidate.value(QStringLiteral("finishReason")).toString();
  if (finishReason.isEmpty() || finishReason == QStringLiteral("FINISH_REASON_UNSPECIFIED")) {
    return;  // REQ-F-019: mid-stream block, nothing terminal yet.
  }

  if (finishReason != QStringLiteral("STOP")) {
    failStream(context, on_event, finishReasonMessage(finishReason));
    return;
  }

  context->terminal = true;
  context->completed = true;
  on_event(
      StreamEvent{holonight_domain::Completed{.usage = context->usage, .model_identifier = context->model_identifier}});
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
    failStream(context, on_event, QStringLiteral("Malformed response from Google: %1").arg(parseError.errorString()));
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

GoogleProvider::GoogleProvider(std::shared_ptr<HttpClient> http_client, QString base_url, QString instance_id)
    : http_client_(std::move(http_client)), instance_id_(std::move(instance_id)), base_url_(std::move(base_url)) {}

const std::vector<ModelId>& GoogleProvider::availableModels() const { return available_models_; }

void GoogleProvider::restoreAvailableModels(std::vector<ModelId> models) { available_models_ = std::move(models); }

void GoogleProvider::refresh(const std::function<void()>& on_complete) {
  fetchModelList(on_complete, [on_complete](const QString& /*reason*/) {
    if (on_complete) {
      on_complete();
    }
  });
}

void GoogleProvider::refresh(const std::function<void()>& on_success,
                             const std::function<void(const QString&)>& on_error) {
  fetchModelList(on_success, on_error);
}

void GoogleProvider::setBaseUrl(QString base_url) { base_url_ = std::move(base_url); }

const QString& GoogleProvider::baseUrl() const { return base_url_; }

void GoogleProvider::setAuthKey(QString auth_key) { auth_key_ = std::move(auth_key); }

void GoogleProvider::setTemperature(double temperature) { temperature_ = temperature; }

void GoogleProvider::setMaxOutputTokens(int max_output_tokens) { max_output_tokens_ = max_output_tokens; }

QHash<QString, QString> GoogleProvider::authHeaders() const {
  QHash<QString, QString> headers;
  if (!auth_key_.isEmpty()) {
    headers.insert(QStringLiteral("x-goog-api-key"), auth_key_);  // REQ-F-001/038/039
  }
  return headers;  // empty map ⇒ no auth header at all, mirroring OllamaProvider's contract
}

void GoogleProvider::fetchModelList(const std::function<void()>& on_success,
                                    const std::function<void(const QString&)>& on_error) {
  fetchModelPage({}, std::make_shared<std::vector<ModelId>>(), on_success, on_error);
}

void GoogleProvider::fetchModelPage(const QString& page_token,
                                    const std::shared_ptr<std::vector<ModelId>>& accumulated_models,
                                    const std::function<void()>& on_success,
                                    const std::function<void(const QString&)>& on_error) {
  QUrl url(base_url_ + QStringLiteral("/v1beta/models"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1000"));
  if (!page_token.isEmpty()) {
    query.addQueryItem(QStringLiteral("pageToken"), page_token);
  }
  url.setQuery(query);

  const HttpRequest request{.method = HttpMethod::Get, .url = url.toString(), .headers = authHeaders()};

  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): false positive — mirrors
  // AnthropicProvider::fetchModelList()'s identical nested-std::function-copy pattern.
  http_client_->send(
      request,
      [this, accumulated_models, on_success, on_error](const QByteArray& body) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
            !doc.object().value(QStringLiteral("models")).isArray()) {
          if (on_error) {
            on_error(QStringLiteral("Malformed model list response from Google"));
          }
          return;
        }

        const QJsonArray modelsArray = doc.object().value(QStringLiteral("models")).toArray();
        accumulated_models->reserve(accumulated_models->size() + static_cast<std::size_t>(modelsArray.size()));
        for (const auto& entry : modelsArray) {
          const QJsonObject modelObject = entry.toObject();
          const QString rawName = modelObject.value(QStringLiteral("name")).toString();
          if (rawName.isEmpty() || isDenylistedModel(rawName) || !supportsGenerateContent(modelObject)) {
            continue;
          }
          accumulated_models->push_back(ModelId{.provider_id = instance_id_, .model_name = stripModelsPrefix(rawName)});
        }

        const QString nextPageToken = doc.object().value(QStringLiteral("nextPageToken")).toString();
        if (!nextPageToken.isEmpty()) {
          fetchModelPage(nextPageToken, accumulated_models, on_success, on_error);
          return;
        }

        available_models_ = std::move(*accumulated_models);
        if (on_success) {
          on_success();
        }
      },
      [on_error](const QString& error) {
        if (on_error) {
          on_error(extractGoogleErrorMessage(error.toUtf8(), error));
        }
      },
      kModelDiscoveryTimeout);
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
}

HttpRequestHandlePtr GoogleProvider::sendChat(const ModelId& model, const std::vector<Message>& history,
                                              const std::function<void(const StreamEvent&)>& on_event,
                                              std::chrono::milliseconds idle_timeout,
                                              const holonight_domain::ToolCatalogSnapshot& tool_catalog) {
  QStringList systemParts;
  const QJsonArray contents = GoogleToolCodec::encodeHistory(history, systemParts);

  QJsonObject generationConfig;
  generationConfig[QStringLiteral("temperature")] = temperature_;            // REQ-F-005
  generationConfig[QStringLiteral("maxOutputTokens")] = max_output_tokens_;  // REQ-F-006

  QJsonObject body;
  body[QStringLiteral("contents")] = contents;
  if (!systemParts.isEmpty()) {
    QJsonObject systemInstruction;
    systemInstruction[QStringLiteral("parts")] =
        QJsonArray{QJsonObject{{QStringLiteral("text"), systemParts.join(QStringLiteral("\n\n"))}}};  // REQ-F-003
    body[QStringLiteral("systemInstruction")] = systemInstruction;
  }
  body[QStringLiteral("generationConfig")] = generationConfig;
  const QJsonArray tools = GoogleToolCodec::encodeDefinitions(tool_catalog);
  if (!tools.isEmpty()) {
    body[QStringLiteral("tools")] = tools;
  }
  // Deliberately absent: safetySettings, toolConfig, candidateCount, topK, topP,
  // stopSequences, and any top-level "model"/"stream" field (REQ-C-001/C-004/C-008/C-009,
  // DESIGN.md §5.9 — model selection lives entirely in the URL path).

  const HttpRequest request{.method = HttpMethod::Post,
                            .url = base_url_ + QStringLiteral("/v1beta/models/") + model.model_name +
                                   QStringLiteral(":streamGenerateContent?alt=sse"),
                            .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                            .content_type = QStringLiteral("application/json"),
                            .headers = authHeaders()};

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
    failStream(context, on_event, extractGoogleErrorMessage(context->buffer, transportMessage));  // REQ-F-023
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
