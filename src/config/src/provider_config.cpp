#include "holonight_config/provider_config.h"

namespace holonight_config {

QString providerTypeToString(ProviderType type) {
  switch (type) {
    case ProviderType::Ollama:
      return QStringLiteral("ollama");
    case ProviderType::OpenAi:
      return QStringLiteral("openai");
    case ProviderType::Anthropic:
      return QStringLiteral("anthropic");
    case ProviderType::Google:
      return QStringLiteral("google");
  }
  return {};
}

std::optional<ProviderType> providerTypeFromString(const QString& value) {
  if (value == QStringLiteral("ollama")) {
    return ProviderType::Ollama;
  }
  if (value == QStringLiteral("openai")) {
    return ProviderType::OpenAi;
  }
  if (value == QStringLiteral("anthropic")) {
    return ProviderType::Anthropic;
  }
  if (value == QStringLiteral("google")) {
    return ProviderType::Google;
  }
  return std::nullopt;
}

QString defaultProviderDisplayName(ProviderType type) {
  switch (type) {
    case ProviderType::Ollama:
      return QStringLiteral("Ollama");
    case ProviderType::OpenAi:
      return QStringLiteral("OpenAI");
    case ProviderType::Anthropic:
      return QStringLiteral("Anthropic");
    case ProviderType::Google:
      return QStringLiteral("Google");
  }
  return {};
}

}  // namespace holonight_config
