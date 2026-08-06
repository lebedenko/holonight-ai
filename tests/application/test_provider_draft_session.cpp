#include "holonight_application/provider_draft_session.h"

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

using namespace holonight_config;

ProviderInstanceConfig openAiConfig() {
  return {.id = QStringLiteral("instance-1"),
          .type = ProviderType::OpenAi,
          .display_name = QStringLiteral("Work"),
          .enabled = true,
          .settings = OpenAIProviderConfig{}};
}

TEST(ProviderDraftSession, ExistingEditsAreDirtyAndDiscardRestoresSnapshot) {
  const auto saved = openAiConfig();
  ProviderDraftSession session(saved, saved);

  session.setDisplayName(QStringLiteral("Personal"));
  session.setEnabled(false);
  session.setCredential(QStringLiteral("secret"));
  EXPECT_TRUE(session.dirty());

  session.discard();
  EXPECT_FALSE(session.dirty());
  EXPECT_EQ(session.editable(), saved);
  EXPECT_EQ(session.credentialEdit(), ProviderDraftSession::CredentialEdit::Keep);
  EXPECT_TRUE(session.credentialValue().isEmpty());
}

TEST(ProviderDraftSession, RenameEnableAndTypedFieldEditsShareOneSession) {
  const auto saved = openAiConfig();
  ProviderDraftSession session(saved, saved);

  session.setDisplayName(QStringLiteral("Personal"));
  session.setEnabled(false);
  ASSERT_TRUE(session.setSettings(OpenAIProviderConfig{.base_url = QStringLiteral("https://proxy.test/v1"),
                                                       .default_model = QStringLiteral("gpt-test"),
                                                       .temperature = 0.4}));

  EXPECT_TRUE(session.dirty());
  EXPECT_EQ(session.editable().display_name, QStringLiteral("Personal"));
  EXPECT_FALSE(session.editable().enabled);
  EXPECT_EQ(std::get<OpenAIProviderConfig>(session.editable().settings),
            (OpenAIProviderConfig{.base_url = QStringLiteral("https://proxy.test/v1"),
                                  .default_model = QStringLiteral("gpt-test"),
                                  .temperature = 0.4}));
}

TEST(ProviderDraftSession, NewInstanceStartsCleanRelativeToItsInitialDraft) {
  ProviderDraftSession session(std::nullopt, openAiConfig());

  EXPECT_TRUE(session.isAddition());
  EXPECT_FALSE(session.dirty());
  session.setDisplayName(QStringLiteral("Changed"));
  EXPECT_TRUE(session.dirty());
  session.discard();
  EXPECT_FALSE(session.dirty());
  EXPECT_EQ(session.editable().display_name, QStringLiteral("Work"));
}

TEST(ProviderDraftSession, ResetUsesTypedDefaultsWithoutChangingIdentityOrSaving) {
  auto config = openAiConfig();
  config.settings = OpenAIProviderConfig{
      .base_url = QStringLiteral("https://proxy.test"), .default_model = QStringLiteral("model"), .temperature = 0.3};
  ProviderDraftSession session(config, config);

  session.resetToDefaults();

  EXPECT_EQ(session.editable().id, config.id);
  EXPECT_EQ(session.editable().display_name, config.display_name);
  EXPECT_EQ(session.editable().settings, ProviderSettings(OpenAIProviderConfig{}));
  EXPECT_EQ(session.credentialEdit(), ProviderDraftSession::CredentialEdit::Remove);
  EXPECT_TRUE(session.dirty());
}

TEST(ProviderDraftSession, RejectsWrongTypedSettingsVariant) {
  ProviderDraftSession session(std::nullopt, openAiConfig());

  EXPECT_FALSE(session.setSettings(OllamaProviderConfig{}));
  EXPECT_EQ(session.editable().settings, ProviderSettings(OpenAIProviderConfig{}));
  EXPECT_FALSE(session.dirty());
}

TEST(ProviderDraftSession, ValidationTrimsNameAndChecksUniquenessTypedFieldsAndCredential) {
  auto config = openAiConfig();
  ProviderDraftSession session(config, config, [](const QString& name, const QString& own_id) {
    return name != QStringLiteral("Duplicate") && own_id == "instance-1";
  });
  session.setDisplayName(QStringLiteral("  Duplicate  "));
  EXPECT_TRUE(session.setSettings(OpenAIProviderConfig{.base_url = QString(), .temperature = 3.0}));
  session.setCredential(QString());

  EXPECT_FALSE(session.validate());
  EXPECT_EQ(session.editable().display_name, QStringLiteral("Duplicate"));
  EXPECT_EQ(session.validationErrors().size(), 4);

  session.setDisplayName(QStringLiteral("  Unique  "));
  EXPECT_TRUE(session.setSettings(OpenAIProviderConfig{}));
  session.setCredential(QStringLiteral("new-secret"));
  EXPECT_TRUE(session.validate());
  EXPECT_EQ(session.editable().display_name, QStringLiteral("Unique"));
}

TEST(ProviderDraftSession, AnthropicUsesItsNarrowerTemperatureRange) {
  ProviderInstanceConfig config{.id = QStringLiteral("anthropic-1"),
                                .type = ProviderType::Anthropic,
                                .display_name = QStringLiteral("Anthropic"),
                                .settings = AnthropicProviderConfig{.temperature = 1.5}};
  ProviderDraftSession session(std::nullopt, config);

  EXPECT_FALSE(session.validate());
  EXPECT_EQ(session.validationErrors().size(), 1);
}

}  // namespace
}  // namespace holonight_application
