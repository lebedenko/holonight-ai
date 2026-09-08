#include "qml/fakes/provider_controller.h"

#include <QAbstractListModel>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QtQml>

#include <gtest/gtest.h>
#include <holonight_rendering/code_highlighter.h>
#include <holonight_rendering/content_block.h>
#include <memory>

namespace {

QString workspaceFile(const QString& file_name) {
  return QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/workspace/") + file_name);
}

std::unique_ptr<QObject> createComponent(QQmlEngine& engine, const QString& file_name,
                                         const QVariantMap& initial_properties = {}) {
  QQmlComponent component{&engine, QUrl::fromLocalFile(workspaceFile(file_name))};
  EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
  if (!component.isReady()) {
    return {};
  }
  std::unique_ptr<QObject> object{component.createWithInitialProperties(initial_properties)};
  EXPECT_NE(object, nullptr) << qPrintable(component.errorString());
  return object;
}

void activate(QObject* object, const char* signal_name) {
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(object, signal_name, Qt::DirectConnection));
  QCoreApplication::processEvents();
}

class ProviderManagementQml : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterTypesAndRevisions<holonight_rendering::CodeHighlighter>("HolonightChat", 1);
    qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlock>("HolonightChat", 1);
    qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlockTypeNs>("HolonightChat", 1);
    qmlRegisterType(QUrl::fromLocalFile(workspaceFile(QStringLiteral("../shared/ProviderIcon.qml"))), "HolonightChat",
                    1, 0, "ProviderIcon");
  }

  void SetUp() override {
    engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  }

  QQmlEngine engine;
};

TEST_F(ProviderManagementQml, EmptyAndPopulatedListsDoNotAutomaticallySelectAProvider) {
  FakeProviderController controller;
  controller.model.prepend({.instance_id = QStringLiteral("existing"),
                            .provider_type = QStringLiteral("ollama"),
                            .display_name = QStringLiteral("Local"),
                            .enabled = true});

  auto panel = createComponent(engine, QStringLiteral("ProviderListPanel.qml"),
                               {{QStringLiteral("providerController"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);
  auto* list = panel->findChild<QObject*>(QStringLiteral("providerInstanceList"));
  ASSERT_NE(list, nullptr);
  QTRY_COMPARE(list->property("count").toInt(), 1);
  EXPECT_EQ(list->property("currentIndex").toInt(), -1);
  EXPECT_TRUE(controller.selectedInstanceId().isEmpty());

  auto page = createComponent(engine, QStringLiteral("ProvidersPage.qml"));
  ASSERT_NE(page, nullptr);
  auto* empty_state = page->findChild<QObject*>(QStringLiteral("noProviderSelectedState"));
  auto* empty_state_text = page->findChild<QObject*>(QStringLiteral("noProviderSelectedText"));
  QTRY_VERIFY(empty_state != nullptr);
  ASSERT_NE(empty_state_text, nullptr);
  EXPECT_TRUE(empty_state->property("surfaceRole").isValid());
  EXPECT_NE(empty_state_text->property("color").value<QColor>(), QColor{Qt::black});
}

TEST_F(ProviderManagementQml, AddMenuListsEveryProviderAndPrependsAndSelectsTheDraft) {
  FakeProviderController controller;
  controller.model.prepend({.instance_id = QStringLiteral("existing"),
                            .provider_type = QStringLiteral("ollama"),
                            .display_name = QStringLiteral("Local"),
                            .enabled = true});
  auto panel = createComponent(engine, QStringLiteral("ProviderListPanel.qml"),
                               {{QStringLiteral("providerController"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);

  const std::array actions = {
      std::pair{QStringLiteral("addOllamaProviderAction"), QStringLiteral("Ollama")},
      std::pair{QStringLiteral("addOpenAIProviderAction"), QStringLiteral("OpenAI")},
      std::pair{QStringLiteral("addAnthropicProviderAction"), QStringLiteral("Anthropic")},
      std::pair{QStringLiteral("addGoogleProviderAction"), QStringLiteral("Google")},
  };
  for (const auto& [object_name, label] : actions) {
    auto* action = panel->findChild<QObject*>(object_name);
    ASSERT_NE(action, nullptr) << qPrintable(object_name);
    EXPECT_EQ(action->property("text").toString(), label);
  }

  activate(panel->findChild<QObject*>(QStringLiteral("addGoogleProviderAction")), "triggered");
  ASSERT_EQ(controller.model.rowCount(), 2);
  EXPECT_EQ(controller.model.row(0).provider_type, QStringLiteral("google"));
  EXPECT_EQ(controller.selectedInstanceId(), QStringLiteral("new-google"));
  EXPECT_EQ(controller.selectedProviderType(), QStringLiteral("google"));
}

TEST_F(ProviderManagementQml, ValidationAndDeletionStateArePresentedInline) {
  FakeProviderController controller;
  controller.setNameValidationError(QStringLiteral("Name already exists"));
  controller.setCanDelete(false);
  controller.setDeletionExplanation(QStringLiteral("Deletion is blocked while a response is streaming."));
  auto scaffold = createComponent(engine, QStringLiteral("ProviderSettingsScaffold.qml"),
                                  {{QStringLiteral("providerController"), QVariant::fromValue(&controller)},
                                   {QStringLiteral("providerType"), QStringLiteral("ollama")},
                                   {QStringLiteral("title"), QStringLiteral("Ollama")}});
  ASSERT_NE(scaffold, nullptr);

  auto* name_field = scaffold->findChild<QObject*>(QStringLiteral("providerNameField"));
  auto* delete_button = scaffold->findChild<QObject*>(QStringLiteral("deleteProviderButton"));
  ASSERT_NE(name_field, nullptr);
  ASSERT_NE(delete_button, nullptr);
  EXPECT_TRUE(name_field->property("hasError").toBool());
  EXPECT_EQ(name_field->property("errorText").toString(), QStringLiteral("Name already exists"));
  EXPECT_FALSE(delete_button->property("enabled").toBool());
  auto* explanation = scaffold->findChild<QObject*>(QStringLiteral("deletionExplanationText"));
  ASSERT_NE(explanation, nullptr);
  EXPECT_TRUE(explanation->property("visible").toBool());
  EXPECT_EQ(explanation->property("text").toString(),
            QStringLiteral("Deletion is blocked while a response is streaming."));

  controller.setCanDelete(true);
  QTRY_VERIFY(delete_button->property("enabled").toBool());
  auto* dialog = scaffold->findChild<QObject*>(QStringLiteral("deleteProviderDialog"));
  ASSERT_NE(dialog, nullptr);
  EXPECT_EQ(dialog->property("standardButtons").toInt(), 0);
  auto* confirm_button = scaffold->findChild<QObject*>(QStringLiteral("confirmDeleteProviderButton"));
  auto* cancel_button = scaffold->findChild<QObject*>(QStringLiteral("cancelDeleteProviderButton"));
  ASSERT_NE(confirm_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  activate(confirm_button, "clicked");
  EXPECT_EQ(controller.delete_count, 1);
}

TEST_F(ProviderManagementQml, DisabledProviderUsesNeutralDisabledStatus) {
  auto delegate = createComponent(engine, QStringLiteral("ProviderListDelegate.qml"),
                                  {{QStringLiteral("providerName"), QStringLiteral("Offline provider")},
                                   {QStringLiteral("providerId"), QStringLiteral("offline")},
                                   {QStringLiteral("providerType"), QStringLiteral("unsupported")},
                                   {QStringLiteral("providerEnabled"), false}});
  ASSERT_NE(delegate, nullptr);
  EXPECT_EQ(delegate->property("subtitle").toString(), QStringLiteral("Disabled"));
  EXPECT_EQ(delegate->property("status").toInt(), 0);
}

TEST_F(ProviderManagementQml, DirtyPromptOffersSaveDiscardAndCancelActions) {
  FakeProviderController controller;
  controller.setDirty(true);
  controller.setNavigationPromptVisible(true);
  auto window = createComponent(engine, QStringLiteral("SettingsWindow.qml"),
                                {{QStringLiteral("providerController"), QVariant::fromValue(&controller)}});
  ASSERT_NE(window, nullptr);

  auto* dialog = window->findChild<QObject*>(QStringLiteral("dirtyNavigationDialog"));
  ASSERT_NE(dialog, nullptr);
  QTRY_VERIFY(dialog->property("visible").toBool());
  activate(window->findChild<QObject*>(QStringLiteral("cancelDirtyNavigationButton")), "clicked");
  activate(window->findChild<QObject*>(QStringLiteral("discardDirtyNavigationButton")), "clicked");
  activate(window->findChild<QObject*>(QStringLiteral("saveDirtyNavigationButton")), "clicked");
  EXPECT_EQ(controller.cancel_count, 1);
  EXPECT_EQ(controller.discard_count, 1);
  EXPECT_EQ(controller.save_count, 1);
  controller.setNavigationPromptVisible(false);
}

TEST_F(ProviderManagementQml, DirtyPromptDisablesSaveForInvalidDraft) {
  FakeProviderController controller;
  controller.setDirty(true);
  controller.setCanSave(false);
  controller.setNavigationPromptVisible(true);
  auto window = createComponent(engine, QStringLiteral("SettingsWindow.qml"),
                                {{QStringLiteral("providerController"), QVariant::fromValue(&controller)}});
  ASSERT_NE(window, nullptr);

  auto* save_button = window->findChild<QObject*>(QStringLiteral("saveDirtyNavigationButton"));
  auto* discard_button = window->findChild<QObject*>(QStringLiteral("discardDirtyNavigationButton"));
  ASSERT_NE(save_button, nullptr);
  ASSERT_NE(discard_button, nullptr);
  EXPECT_FALSE(save_button->property("enabled").toBool());
  EXPECT_TRUE(discard_button->property("enabled").toBool());
}

TEST_F(ProviderManagementQml, HistoricalAttributionShowsResolvedNameModelAndProviderType) {
  auto bubble = createComponent(engine, QStringLiteral("../shared/MessageBubble.qml"),
                                {{QStringLiteral("messageRole"), QStringLiteral("assistant")},
                                 {QStringLiteral("messageText"), QStringLiteral("Historical response")},
                                 {QStringLiteral("messageStatus"), QStringLiteral("complete")},
                                 {QStringLiteral("modelName"), QStringLiteral("gpt-5")},
                                 {QStringLiteral("providerId"), QStringLiteral("deleted-instance")},
                                 {QStringLiteral("providerType"), QStringLiteral("openai")},
                                 {QStringLiteral("providerName"), QStringLiteral("Former work account")},
                                 {QStringLiteral("createdAt"), QDateTime::currentDateTime()},
                                 {QStringLiteral("contentBlocks"), QVariantList{}},
                                 {QStringLiteral("inputTokenCount"), QVariant()},
                                 {QStringLiteral("outputTokenCount"), QVariant()},
                                 {QStringLiteral("reasoningTokenCount"), QVariant()},
                                 {QStringLiteral("cacheCreationTokenCount"), QVariant()},
                                 {QStringLiteral("cacheReadTokenCount"), QVariant()},
                                 {QStringLiteral("totalTokenCount"), QVariant()},
                                 {QStringLiteral("durationMs"), QVariant()}});
  ASSERT_NE(bubble, nullptr);

  auto* attribution = bubble->findChild<QObject*>(QStringLiteral("providerAttribution"));
  auto* icon = bubble->findChild<QObject*>(QStringLiteral("providerIcon"));
  ASSERT_NE(attribution, nullptr);
  ASSERT_NE(icon, nullptr);
  EXPECT_EQ(attribution->property("text").toString(), QStringLiteral("Former work account · gpt-5"));
  EXPECT_EQ(icon->property("providerType").toString(), QStringLiteral("openai"));

  bubble->setProperty("providerName", QStringLiteral("Renamed work account"));
  QTRY_COMPARE(attribution->property("text").toString(), QStringLiteral("Renamed work account · gpt-5"));
}

TEST_F(ProviderManagementQml, EmptyCancelledAssistantPlaceholderIsNotRendered) {
  auto bubble = createComponent(engine, QStringLiteral("../shared/MessageBubble.qml"),
                                {{QStringLiteral("messageRole"), QStringLiteral("assistant")},
                                 {QStringLiteral("messageText"), QStringLiteral("   \n")},
                                 {QStringLiteral("messageStatus"), QStringLiteral("cancelled")},
                                 {QStringLiteral("modelName"), QStringLiteral("claude-haiku")},
                                 {QStringLiteral("providerId"), QStringLiteral("anthropic")},
                                 {QStringLiteral("providerType"), QStringLiteral("anthropic")},
                                 {QStringLiteral("providerName"), QStringLiteral("Anthropic")},
                                 {QStringLiteral("createdAt"), QDateTime::currentDateTime()},
                                 {QStringLiteral("contentBlocks"), QVariantList{}},
                                 {QStringLiteral("inputTokenCount"), QVariant()},
                                 {QStringLiteral("outputTokenCount"), QVariant()},
                                 {QStringLiteral("reasoningTokenCount"), QVariant()},
                                 {QStringLiteral("cacheCreationTokenCount"), QVariant()},
                                 {QStringLiteral("cacheReadTokenCount"), QVariant()},
                                 {QStringLiteral("totalTokenCount"), QVariant()},
                                 {QStringLiteral("durationMs"), QVariant()}});
  ASSERT_NE(bubble, nullptr);

  EXPECT_FALSE(bubble->property("visible").toBool());
  EXPECT_EQ(bubble->property("implicitHeight").toReal(), 0.0);
}

}  // namespace
