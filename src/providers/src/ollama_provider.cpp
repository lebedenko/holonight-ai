#include "holonight_providers/ollama_provider.h"

#include "cancellation_aware_handle.h"
#include "holonight_providers/ollama_tool_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <chrono>

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

void processLine(const QByteArray& line, const std::shared_ptr<StreamContext>& context,
                 const std::function<void(const StreamEvent&)>& on_event) {
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    failStream(context, on_event, QStringLiteral("Malformed response from Ollama: %1").arg(parseError.errorString()));
    return;
  }

  const QJsonObject object = doc.object();
  if (object.contains(QStringLiteral("error"))) {
    failStream(context, on_event, object.value(QStringLiteral("error")).toString());
    return;
  }

  const QJsonObject message = object.value(QStringLiteral("message")).toObject();
  const QString content = message.value(QStringLiteral("content")).toString();
  on_event(StreamEvent{ContentDelta{content}});

  if (object.contains(QStringLiteral("model"))) {
    context->model_identifier = object.value(QStringLiteral("model")).toString();
  }

  if (message.contains(QStringLiteral("tool_calls"))) {  // REQ-F-003
    const QJsonValue toolCallsValue = message.value(QStringLiteral("tool_calls"));
    if (!toolCallsValue.isArray()) {
      failStream(context, on_event, QStringLiteral("Ollama tool_calls field is not a JSON array"));
      return;
    }
    const QJsonArray toolCalls = toolCallsValue.toArray();
    auto decoded = OllamaToolCodec::decodeRequests(context->provider_instance_id, toolCalls);
    if (!decoded.has_value()) {
      failStream(context, on_event, decoded.error());
      return;
    }
    for (const auto& event : *decoded) {  // REQ-F-004/NF-004: array order preserved
      on_event(StreamEvent{event});
    }
  }

  if (object.value(QStringLiteral("done")).toBool(false)) {
    context->terminal = true;
    context->completed = true;

    if (object.contains(QStringLiteral("prompt_eval_count"))) {
      context->usage.input_tokens = object.value(QStringLiteral("prompt_eval_count")).toInt();
    }
    if (object.contains(QStringLiteral("eval_count"))) {
      context->usage.output_tokens = object.value(QStringLiteral("eval_count")).toInt();
    }
    if (context->usage.input_tokens && context->usage.output_tokens) {
      context->usage.total_tokens = *context->usage.input_tokens + *context->usage.output_tokens;
    }
    if (object.contains(QStringLiteral("total_duration"))) {
      context->usage.ollama_total_duration_ns = object.value(QStringLiteral("total_duration")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("load_duration"))) {
      context->usage.ollama_load_duration_ns = object.value(QStringLiteral("load_duration")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("prompt_eval_duration"))) {
      context->usage.ollama_prompt_eval_duration_ns =
          object.value(QStringLiteral("prompt_eval_duration")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("eval_duration"))) {
      context->usage.ollama_eval_duration_ns = object.value(QStringLiteral("eval_duration")).toVariant().toLongLong();
    }

    on_event(StreamEvent{
        holonight_domain::Completed{.usage = context->usage, .model_identifier = context->model_identifier}});
  }
}

}  // namespace

OllamaProvider::OllamaProvider(std::shared_ptr<HttpClient> http_client, QString base_url, QString instance_id)
    : http_client_(std::move(http_client)), instance_id_(std::move(instance_id)), base_url_(std::move(base_url)) {}

const std::vector<ModelId>& OllamaProvider::availableModels() const { return available_models_; }

void OllamaProvider::restoreAvailableModels(std::vector<ModelId> models) { available_models_ = std::move(models); }

void OllamaProvider::refresh(const std::function<void()>& on_complete) {
  fetchModelList(on_complete, [on_complete](const QString& /*reason*/) {
    if (on_complete) {
      on_complete();
    }
  });
}

void OllamaProvider::refresh(const std::function<void()>& on_success,
                             const std::function<void(const QString&)>& on_error) {
  fetchModelList(on_success, on_error);
}

void OllamaProvider::setBaseUrl(QString base_url) { base_url_ = std::move(base_url); }

const QString& OllamaProvider::baseUrl() const { return base_url_; }

void OllamaProvider::setAuthToken(QString auth_token) { auth_token_ = std::move(auth_token); }

void OllamaProvider::setContextWindow(int context_window) { context_window_ = context_window; }

void OllamaProvider::setTemperature(double temperature) { temperature_ = temperature; }

QHash<QString, QString> OllamaProvider::authHeaders() const {
  if (auth_token_.isEmpty()) {
    return {};
  }
  return {{QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(auth_token_)}};
}

void OllamaProvider::fetchModelList(const std::function<void()>& on_success,
                                    const std::function<void(const QString&)>& on_error) {
  const HttpRequest request{
      .method = HttpMethod::Get, .url = base_url_ + QStringLiteral("/api/tags"), .headers = authHeaders()};

  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): false positive — the analyzer loses
  // track of ownership through the nested std::function copies here (on_error, itself wrapping a
  // captured on_complete from refresh()'s single-callback overload, gets copied again into this
  // lambda). No raw new/delete exists anywhere in this file; std::function's own RAII handles it.
  http_client_->send(
      request,
      [this, on_success, on_error](const QByteArray& body) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
            !doc.object().value(QStringLiteral("models")).isArray()) {
          if (on_error) {
            on_error(QStringLiteral("Malformed model list response from Ollama"));
          }
          return;
        }

        const QJsonArray modelsArray = doc.object().value(QStringLiteral("models")).toArray();
        std::vector<ModelId> models;
        models.reserve(static_cast<std::size_t>(modelsArray.size()));
        for (const auto& entry : modelsArray) {
          const QString modelName = entry.toObject().value(QStringLiteral("name")).toString();
          if (modelName.isEmpty()) {
            continue;
          }
          models.push_back(ModelId{.provider_id = instance_id_, .model_name = modelName});
        }
        available_models_ = std::move(models);
        if (on_success) {
          on_success();
        }
      },
      [on_error](const QString& error) {
        if (on_error) {
          on_error(error);
        }
      },
      kModelDiscoveryTimeout);
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
}

HttpRequestHandlePtr OllamaProvider::sendChat(const ModelId& model, const std::vector<Message>& history,
                                              const std::function<void(const StreamEvent&)>& on_event,
                                              std::chrono::milliseconds idle_timeout,
                                              const holonight_domain::ToolCatalogSnapshot& tool_catalog) {
  const QJsonArray messages = OllamaToolCodec::encodeHistory(history);

  QJsonObject options;
  options[QStringLiteral("temperature")] = temperature_;
  options[QStringLiteral("num_ctx")] = context_window_;

  QJsonObject body;
  body[QStringLiteral("model")] = model.model_name;
  body[QStringLiteral("stream")] = true;
  body[QStringLiteral("messages")] = messages;
  body[QStringLiteral("options")] = options;
  const QJsonArray tools = OllamaToolCodec::encodeDefinitions(tool_catalog);
  if (!tools.isEmpty()) {
    body[QStringLiteral("tools")] = tools;  // REQ-F-002
  }
  // Deliberately absent: "think" (REQ-C-004 -- this cycle never sets it).

  const HttpRequest request{.method = HttpMethod::Post,
                            .url = base_url_ + QStringLiteral("/api/chat"),
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
    qsizetype newlineIndex = context->buffer.indexOf('\n');
    while (newlineIndex != -1) {
      const QByteArray line = context->buffer.left(newlineIndex).trimmed();
      context->buffer.remove(0, newlineIndex + 1);
      if (!line.isEmpty()) {
        processLine(line, context, on_event);
        if (context->terminal) {
          return;
        }
      }
      newlineIndex = context->buffer.indexOf('\n');
    }
  };

  auto onFinished = [context, on_event]() {
    if (context->terminal) {
      return;
    }

    const QByteArray remainingLine = context->buffer.trimmed();
    if (!remainingLine.isEmpty()) {
      processLine(remainingLine, context, on_event);
    }
    if (!context->completed) {
      failStream(context, on_event, QStringLiteral("Ollama response ended before completion"));
    }
  };

  auto onError = [context, on_event](const QString& message) { failStream(context, on_event, message); };

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
