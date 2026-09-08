#include "qml/fakes/chat_view_model.h"
#include "qml/fakes/provider_controller.h"
#include "qml/fakes/utility_settings_controller.h"

#include <QDateTime>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QtQml>

#include <gtest/gtest.h>
#include <holonight_rendering/code_highlighter.h>
#include <holonight_rendering/content_block.h>
#include <memory>

namespace {

std::unique_ptr<QObject> create(QQmlEngine& engine, const QString& path, const QVariantMap& properties = {}) {
  QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/HolonightChat/") + path}};
  EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
  if (!component.isReady()) {
    return {};
  }
  std::unique_ptr<QObject> object{component.createWithInitialProperties(properties)};
  EXPECT_NE(object, nullptr) << qPrintable(component.errorString());
  return object;
}

void activate(QObject* object, const char* signal) {
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(object, signal));
  QCoreApplication::processEvents();
}

TEST(RuntimeControls, ApplicationSurfacesAndBehavior) {
  FakeChatViewModel chat;
  FakeProviderController providers;
  FakeUtilitySettingsController utility;
  const std::unique_ptr<QQmlPropertyMap> provider_form{QQmlPropertyMap::create()};
  for (const auto* key : {"baseUrl", "defaultModel", "authToken", "modelRefreshError", "saveNotice", "saveNoticeStatus",
                          "testConnectionMessage", "testConnectionStatus", "ollamaConnectionStatus",
                          "openAiConnectionStatus", "anthropicConnectionStatus", "googleConnectionStatus"}) {
    provider_form->insert(QString::fromLatin1(key), QString{});
  }
  for (const auto* key : {"toolCallingEnabled", "credentialOperationInProgress", "hasStoredToken",
                          "modelRefreshInProgress", "testConnectionInProgress"}) {
    provider_form->insert(QString::fromLatin1(key), false);
  }
  provider_form->insert(QStringLiteral("credentialStoreAvailable"), true);
  provider_form->insert(QStringLiteral("availableModelNames"), QStringList{QStringLiteral("model-one")});
  provider_form->insert(QStringLiteral("temperature"), 0.5);
  provider_form->insert(QStringLiteral("maxOutputTokens"), 1024);
  provider_form->insert(QStringLiteral("contextWindow"), 4096);
  qmlRegisterSingletonInstance("HolonightChat", 1, 0, "ChatViewModel", &chat);
  qmlRegisterSingletonInstance("HolonightChat", 1, 0, "ProviderManagementController", &providers);
  qmlRegisterSingletonInstance("HolonightChat", 1, 0, "UtilitySettingsController", &utility);
  for (const auto* name : {"ProviderSettingsController", "OpenAIProviderSettingsController",
                           "AnthropicProviderSettingsController", "GoogleProviderSettingsController"}) {
    qmlRegisterSingletonInstance("HolonightChat", 1, 0, name, provider_form.get());
  }
  qmlRegisterTypesAndRevisions<holonight_rendering::CodeHighlighter>("HolonightChat", 1);
  qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlock>("HolonightChat", 1);
  qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlockTypeNs>("HolonightChat", 1);
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  QStringList diagnostics;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&diagnostics](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      diagnostics.append(error.toString());
    }
  });
  auto workspace = create(engine, QStringLiteral("workspace/WorkspaceWindow.qml"));
  ASSERT_NE(workspace, nullptr);
  EXPECT_TRUE(workspace->property("contentPadding").isValid());
  auto settings = create(engine, QStringLiteral("workspace/SettingsWindow.qml"));
  ASSERT_NE(settings, nullptr);
  QQuickWindow host;
  host.resize(1100, 720);
  host.show();
  auto panel = create(engine, QStringLiteral("quickpanel/QuickPanel.qml"),
                      {{QStringLiteral("outputName"), QStringLiteral("offscreen")}});
  ASSERT_NE(panel, nullptr);
  auto* panel_item = qobject_cast<QQuickItem*>(panel.get());
  ASSERT_NE(panel_item, nullptr);
  panel_item->setParentItem(host.contentItem());
  panel_item->setSize(QSizeF{500, 600});
  auto* quick_popup = panel->findChild<QObject*>(QStringLiteral("quickPanelConversationPopup"));
  ASSERT_NE(quick_popup, nullptr);
  activate(quick_popup, "open");
  QTRY_VERIFY(quick_popup->property("visible").toBool());
  activate(quick_popup, "close");
  auto page = create(engine, QStringLiteral("workspace/ProvidersPage.qml"));
  ASSERT_NE(page, nullptr);
  EXPECT_NE(page->findChild<QObject*>(QStringLiteral("noProviderSelectedState")), nullptr);
  for (const auto* type : {"ollama", "openai", "anthropic", "google", "unsupported"}) {
    page->setProperty("selectedProviderType", QString::fromLatin1(type));
    page->setProperty("selectedProviderId", QStringLiteral("fixture-provider"));
    QCoreApplication::processEvents();
    EXPECT_NE(page->property("item").value<QObject*>(), nullptr) << type;
    if (QString::fromLatin1(type) != QStringLiteral("unsupported")) {
      auto* current_form = page->property("item").value<QObject*>();
      ASSERT_NE(current_form, nullptr);
      auto* field = current_form->findChild<QObject*>(QStringLiteral("credentialTextField"));
      ASSERT_NE(field, nullptr) << type;
      provider_form->insert(QStringLiteral("authToken"), QStringLiteral("fixture-draft"));
      QTRY_COMPARE(field->property("text").toString(), QStringLiteral("fixture-draft"));
      EXPECT_NE(field->property("displayText").toString(), QStringLiteral("fixture-draft"));
      activate(field->findChild<QObject*>(QStringLiteral("clearCredentialButton")), "clicked");
      EXPECT_TRUE(provider_form->value(QStringLiteral("authToken")).toString().isEmpty());
      EXPECT_TRUE(field->property("text").toString().isEmpty());
    }
  }
  settings->setProperty("currentSection", QStringLiteral("background-ai"));
  providers.setNavigationPromptVisible(true);
  auto* dialog = settings->findChild<QObject*>(QStringLiteral("dirtyNavigationDialog"));
  ASSERT_NE(dialog, nullptr);
  QTRY_VERIFY(dialog->property("visible").toBool());
  activate(settings->findChild<QObject*>(QStringLiteral("cancelDirtyNavigationButton")), "clicked");
  activate(settings->findChild<QObject*>(QStringLiteral("discardDirtyNavigationButton")), "clicked");
  activate(settings->findChild<QObject*>(QStringLiteral("saveDirtyNavigationButton")), "clicked");
  EXPECT_EQ(providers.cancel_count, 1);
  EXPECT_EQ(providers.discard_count, 1);
  EXPECT_EQ(providers.save_count, 1);
  providers.setNavigationPromptVisible(false);

  auto credential = create(engine, QStringLiteral("workspace/CredentialTextField.qml"));
  ASSERT_NE(credential, nullptr);
  credential->setProperty("text", QStringLiteral("fixture-value"));
  EXPECT_EQ(credential->property("echoMode").toInt(), 2);
  EXPECT_NE(credential->property("displayText").toString(), QStringLiteral("fixture-value"));
  auto* reveal = credential->findChild<QObject*>(QStringLiteral("revealCredentialButton"));
  ASSERT_NE(reveal, nullptr);
  reveal->setProperty("checked", true);
  EXPECT_EQ(credential->property("displayText").toString(), QStringLiteral("fixture-value"));
  reveal->setProperty("checked", false);
  QSignalSpy clear_spy{credential.get(), SIGNAL(clearRequested())};
  activate(credential->findChild<QObject*>(QStringLiteral("clearCredentialButton")), "clicked");
  EXPECT_EQ(clear_spy.count(), 1);
  credential->setProperty("text", QString{});
  EXPECT_TRUE(credential->property("displayText").toString().isEmpty());

  auto composer = create(engine, QStringLiteral("shared/ChatComposer.qml"));
  ASSERT_NE(composer, nullptr);
  auto* editor = composer->findChild<QObject*>(QStringLiteral("composerEditor"));
  ASSERT_NE(editor, nullptr);
  chat.setInputText(QStringLiteral("Draft from model"));
  EXPECT_EQ(editor->property("text").toString(), chat.inputText());
  editor->setProperty("text", QStringLiteral("Edited draft"));
  EXPECT_EQ(chat.inputText(), QStringLiteral("Edited draft"));
  activate(composer.get(), "submit");
  EXPECT_EQ(chat.lastSentText(), QStringLiteral("Edited draft"));
  EXPECT_TRUE(editor->property("text").toString().isEmpty());

  auto picker = create(engine, QStringLiteral("workspace/ProviderModelPicker.qml"));
  ASSERT_NE(picker, nullptr);
  auto* picker_item = qobject_cast<QQuickItem*>(picker.get());
  ASSERT_NE(picker_item, nullptr);
  picker_item->setParentItem(host.contentItem());
  picker_item->setWidth(300);
  QStringList models;
  for (int index = 0; index < 40; ++index) {
    models.append(QStringLiteral("model-%1").arg(index));
  }
  picker->setProperty("selectedProviderId", QStringLiteral("fixture"));
  picker->setProperty("modelNames", models);
  picker->setProperty("selectedModelName", models.at(2));
  auto* model_combo = picker->findChild<QObject*>(QStringLiteral("modelCombo"));
  ASSERT_NE(model_combo, nullptr);
  EXPECT_EQ(model_combo->property("currentIndex").toInt(), 2);
  QSignalSpy selection{picker.get(), SIGNAL(modelSelected(QString))};
  ASSERT_TRUE(QMetaObject::invokeMethod(model_combo, "activated", Q_ARG(int, 7)));
  ASSERT_EQ(selection.count(), 1);
  EXPECT_EQ(selection.at(0).at(0).toString(), models.at(7));
  model_combo->setProperty("maximumVisibleItems", 3);
  auto* model_popup = model_combo->property("popup").value<QObject*>();
  ASSERT_NE(model_popup, nullptr);
  activate(model_popup, "open");
  QTRY_VERIFY(model_popup->property("visible").toBool());
  auto* popup_list = model_popup->property("contentItem").value<QObject*>();
  ASSERT_NE(popup_list, nullptr);
  QTRY_VERIFY(popup_list->property("contentHeight").toReal() > popup_list->property("height").toReal());
  EXPECT_LE(popup_list->property("height").toReal(), 3 * model_combo->property("delegateHeight").toReal());
  activate(model_popup, "close");

  auto provider_list = create(engine, QStringLiteral("workspace/ProviderListPanel.qml"));
  ASSERT_NE(provider_list, nullptr);
  auto* list_item = qobject_cast<QQuickItem*>(provider_list.get());
  ASSERT_NE(list_item, nullptr);
  list_item->setParentItem(host.contentItem());
  list_item->setSize(QSizeF{300, 400});
  auto* menu_action = provider_list->findChild<QObject*>(QStringLiteral("addGoogleProviderAction"));
  ASSERT_NE(menu_action, nullptr);
  auto* menu = menu_action->property("menu").value<QObject*>();
  ASSERT_NE(menu, nullptr);
  activate(menu, "open");
  QTRY_VERIFY(menu->property("visible").toBool());
  activate(menu_action, "triggered");
  EXPECT_EQ(providers.selectedProviderType(), QStringLiteral("google"));
  activate(menu, "close");

  QQmlComponent scrolling{&engine};
  scrolling.setData(R"(
    import QtQuick
    import HolonightChat
    BottomAnchoredListView {
      width: 200; height: 100; model: 40
      delegate: Rectangle { required property int index; width: 200; height: 30 }
    }
  )",
                    QUrl{QStringLiteral("inline:scrolling.qml")});
  QTRY_VERIFY_WITH_TIMEOUT(scrolling.status() != QQmlComponent::Loading, 5000);
  ASSERT_TRUE(scrolling.isReady()) << qPrintable(scrolling.errorString());
  std::unique_ptr<QObject> transcript{scrolling.create()};
  ASSERT_NE(transcript, nullptr);
  auto* transcript_item = qobject_cast<QQuickItem*>(transcript.get());
  ASSERT_NE(transcript_item, nullptr);
  transcript_item->setParentItem(host.contentItem());
  QTRY_VERIFY(transcript->property("contentHeight").toReal() > 100);
  const auto initial_y = transcript->property("contentY").toReal();
  ASSERT_TRUE(QMetaObject::invokeMethod(transcript.get(), "handleDiscreteWheel", Q_ARG(double, 120.0), Q_ARG(int, 0)));
  QTRY_VERIFY(transcript->property("contentY").toReal() != initial_y);
  activate(transcript.get(), "showLatest");
  EXPECT_TRUE(transcript->property("followingLatest").toBool());

  QVariantMap statistics{{QStringLiteral("anchorItem"), QVariant::fromValue(host.contentItem())}};
  for (const auto* key : {"inputTokenCount", "outputTokenCount", "reasoningTokenCount", "cacheCreationTokenCount",
                          "cacheReadTokenCount", "totalTokenCount", "durationMs"}) {
    statistics.insert(QString::fromLatin1(key), 100);
  }
  auto stats = create(engine, QStringLiteral("shared/ResponseStatsPopup.qml"), statistics);
  ASSERT_NE(stats, nullptr);
  stats->setProperty("parent", QVariant::fromValue(host.contentItem()));
  activate(stats.get(), "open");
  QTRY_VERIFY(stats->property("visible").toBool());
  activate(stats.get(), "close");
  auto tool =
      create(engine, QStringLiteral("shared/ToolActivityCard.qml"),
             {{QStringLiteral("createdAt"), QDateTime::fromSecsSinceEpoch(0)},
              {QStringLiteral("toolCall"), QVariantMap{{QStringLiteral("toolUseId"), QStringLiteral("fixture-tool")},
                                                       {QStringLiteral("status"), QStringLiteral("complete")},
                                                       {QStringLiteral("functionName"), QStringLiteral("fixture_tool")},
                                                       {QStringLiteral("kind"), QStringLiteral("result")},
                                                       {QStringLiteral("rawResultJson"), QStringLiteral("{}")}}}});
  ASSERT_NE(tool, nullptr);
  activate(tool->findChild<QObject*>(QStringLiteral("toolActivityDisclosure")), "clicked");
  EXPECT_TRUE(tool->property("expanded").toBool());
  activate(tool->findChild<QObject*>(QStringLiteral("toolActivityRawDisclosure")), "clicked");
  EXPECT_TRUE(tool->property("rawExpanded").toBool());

  // qmlContext URLs prove the selected implementation behind local wrappers.
  auto button = create(engine, QStringLiteral("workspace/ProviderActionButton.qml"),
                       {{QStringLiteral("actionIconSource"), QUrl{}}});
  ASSERT_NE(button, nullptr);
  auto* background = button->property("background").value<QObject*>();
  ASSERT_NE(background, nullptr);
  const QString origin = qmlContext(background)->baseUrl().toString();
  const bool fusion = qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") == QStringLiteral("Fusion");
  EXPECT_TRUE(origin.contains(fusion ? QStringLiteral("Fusion") : QStringLiteral("Holonight"))) << qPrintable(origin);
  qInfo().noquote() << "CONTROL_IMPLEMENTATION" << origin;
  QFile maps{QStringLiteral("/proc/self/maps")};
  ASSERT_TRUE(maps.open(QIODevice::ReadOnly));
  const auto loaded = maps.readAll();
  EXPECT_TRUE(loaded.contains("libholonight_core_qml"));
  EXPECT_TRUE(loaded.contains("libholonight_controls_qml"));
  EXPECT_TRUE(loaded.contains(QByteArray{HOLONIGHT_QML_IMPORT_PATH} + "/Holonight/Core/"));
  EXPECT_TRUE(loaded.contains(QByteArray{HOLONIGHT_QML_IMPORT_PATH} + "/Holonight/Controls/"));
  if (!fusion) {
    EXPECT_TRUE(loaded.contains("libholonight_qml"));
  } else {
    EXPECT_TRUE(loaded.contains("libqtquickcontrols2fusionstyleplugin"));
  }
  qInfo().noquote() << "PLUGIN_ROOT" << QStringLiteral(HOLONIGHT_QML_IMPORT_PATH);
  QCoreApplication::processEvents();
  EXPECT_TRUE(diagnostics.isEmpty()) << qPrintable(diagnostics.join('\n'));
}

}  // namespace
