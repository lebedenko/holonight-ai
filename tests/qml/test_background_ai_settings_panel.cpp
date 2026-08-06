#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>

#include <gtest/gtest.h>
#include <memory>

namespace {

// Mirrors UtilitySettingsController's QML-visible surface (docs/sdd/background-ai-settings-panel)
// so BackgroundAiSettingsPanel.qml can be exercised without a real ConfigRepository/ChatViewModel —
// the same technique test_provider_management.cpp already uses for ProviderManagementController via
// FakeProviderController.
class FakeUtilitySettingsController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList providerInstances READ providerInstances WRITE setProviderInstances NOTIFY changed)
  Q_PROPERTY(QString defaultProviderId READ defaultProviderId WRITE setDefaultProviderId NOTIFY changed)
  Q_PROPERTY(QString defaultModelName READ defaultModelName WRITE setDefaultModelName NOTIFY changed)
  Q_PROPERTY(QStringList defaultModelNames READ defaultModelNames WRITE setDefaultModelNames NOTIFY changed)
  Q_PROPERTY(bool chatTitleGenerationEnabled READ chatTitleGenerationEnabled WRITE setChatTitleGenerationEnabled NOTIFY
                 changed)
  Q_PROPERTY(
      QString titleOverrideProviderId READ titleOverrideProviderId WRITE setTitleOverrideProviderId NOTIFY changed)
  Q_PROPERTY(QString titleOverrideModelName READ titleOverrideModelName WRITE setTitleOverrideModelName NOTIFY changed)
  Q_PROPERTY(
      QStringList titleOverrideModelNames READ titleOverrideModelNames WRITE setTitleOverrideModelNames NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty WRITE setDirty NOTIFY changed)
  Q_PROPERTY(bool canSave READ canSave WRITE setCanSave NOTIFY changed)
  Q_PROPERTY(QString saveNotice READ saveNotice WRITE setSaveNotice NOTIFY changed)
  Q_PROPERTY(QString saveNoticeStatus READ saveNoticeStatus WRITE setSaveNoticeStatus NOTIFY changed)

 public:
  [[nodiscard]] QVariantList providerInstances() const { return provider_instances_; }
  void setProviderInstances(QVariantList value) {
    provider_instances_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString defaultProviderId() const { return default_provider_id_; }
  void setDefaultProviderId(QString value) {
    default_provider_id_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString defaultModelName() const { return default_model_name_; }
  void setDefaultModelName(QString value) {
    default_model_name_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QStringList defaultModelNames() const { return default_model_names_; }
  void setDefaultModelNames(QStringList value) {
    default_model_names_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] bool chatTitleGenerationEnabled() const { return chat_title_generation_enabled_; }
  void setChatTitleGenerationEnabled(bool value) {
    chat_title_generation_enabled_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] QString titleOverrideProviderId() const { return title_override_provider_id_; }
  void setTitleOverrideProviderId(QString value) {
    title_override_provider_id_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString titleOverrideModelName() const { return title_override_model_name_; }
  void setTitleOverrideModelName(QString value) {
    title_override_model_name_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QStringList titleOverrideModelNames() const { return title_override_model_names_; }
  void setTitleOverrideModelNames(QStringList value) {
    title_override_model_names_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] bool dirty() const { return dirty_; }
  void setDirty(bool value) {
    dirty_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] bool canSave() const { return can_save_; }
  void setCanSave(bool value) {
    can_save_ = value;
    Q_EMIT changed();
  }
  [[nodiscard]] QString saveNotice() const { return save_notice_; }
  void setSaveNotice(QString value) {
    save_notice_ = std::move(value);
    Q_EMIT changed();
  }
  [[nodiscard]] QString saveNoticeStatus() const { return save_notice_status_; }
  void setSaveNoticeStatus(QString value) {
    save_notice_status_ = std::move(value);
    Q_EMIT changed();
  }

  Q_INVOKABLE bool save() {
    ++save_count_;
    return true;
  }
  Q_INVOKABLE void discardDraft() { ++discard_count_; }
  Q_INVOKABLE void refreshModelsForInstance(const QString& providerInstanceId) {
    last_refresh_requested_ = providerInstanceId;
  }

  [[nodiscard]] int saveCount() const { return save_count_; }
  [[nodiscard]] int discardCount() const { return discard_count_; }
  [[nodiscard]] QString lastRefreshRequested() const { return last_refresh_requested_; }

 Q_SIGNALS:
  void changed();

 private:
  QVariantList provider_instances_;
  QString default_provider_id_;
  QString default_model_name_;
  QStringList default_model_names_;
  bool chat_title_generation_enabled_ = true;
  QString title_override_provider_id_;
  QString title_override_model_name_;
  QStringList title_override_model_names_;
  bool dirty_ = false;
  bool can_save_ = false;
  QString save_notice_;
  QString save_notice_status_ = QStringLiteral("idle");
  int save_count_ = 0;
  int discard_count_ = 0;
  QString last_refresh_requested_;
};

QVariantMap providerRow(const QString& providerId, const QString& displayName) {
  return QVariantMap{{QStringLiteral("provider_id"), providerId}, {QStringLiteral("display_name"), displayName}};
}

QString workspaceFile(const QString& fileName) {
  return QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/workspace/") + fileName);
}

std::unique_ptr<QObject> createComponent(QQmlEngine& engine, const QString& fileName,
                                         const QVariantMap& initialProperties = {}) {
  QQmlComponent component{&engine, QUrl::fromLocalFile(workspaceFile(fileName))};
  EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
  if (!component.isReady()) {
    return {};
  }
  std::unique_ptr<QObject> object{component.createWithInitialProperties(initialProperties)};
  EXPECT_NE(object, nullptr) << qPrintable(component.errorString());
  return object;
}

void activateIndex(QObject* combo, int index) {
  ASSERT_NE(combo, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, index)));
  QCoreApplication::processEvents();
}

void click(QObject* object) {
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(object, "clicked", Qt::DirectConnection));
  QCoreApplication::processEvents();
}

void toggle(QObject* object) {
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(object, "toggled", Qt::DirectConnection));
  QCoreApplication::processEvents();
}

class BackgroundAiSettingsPanelQml : public testing::Test {
 protected:
  // BackgroundAiSettingsPanel.qml (like ProviderSettingsScaffold.qml) does `import HolonightChat`
  // for its default `property var controller: UtilitySettingsController` binding — QML needs that
  // identifier to resolve at parse time even though every test below overrides `controller` via
  // createWithInitialProperties(), never reading the default. Registering a placeholder instance
  // under the same module/name (mirroring test_provider_management.cpp's
  // qmlRegisterTypesAndRevisions<...>("HolonightChat", 1) calls) satisfies that resolution without
  // constructing the real, config-file-touching UtilitySettingsController.
  static void SetUpTestSuite() {
    static FakeUtilitySettingsController placeholder;
    qmlRegisterSingletonInstance<FakeUtilitySettingsController>("HolonightChat", 1, 0, "UtilitySettingsController",
                                                                &placeholder);
  }

  void SetUp() override {
    qml_engine_.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    qml_engine_.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  }

  // NOLINTBEGIN(readability-identifier-naming,cppcoreguidelines-non-private-member-variables-in-classes)
  // GTest test fixtures conventionally use protected members for test access
  QQmlEngine qml_engine_;
  // NOLINTEND(readability-identifier-naming,cppcoreguidelines-non-private-member-variables-in-classes)
};

// T-033 (REQ-F-003/REQ-F-005): the Model field is hidden while no provider instance is selected —
// there is nothing meaningful to populate it with yet.
TEST_F(BackgroundAiSettingsPanelQml, ModelFieldHiddenWhenNoProviderSelected) {
  auto picker = createComponent(qml_engine_, QStringLiteral("ProviderModelPicker.qml"),
                                {{QStringLiteral("selectedProviderId"), QString()}});
  ASSERT_NE(picker, nullptr);

  auto* modelField = picker->findChild<QObject*>(QStringLiteral("modelField"));
  ASSERT_NE(modelField, nullptr);
  EXPECT_FALSE(modelField->property("visible").toBool());
}

// T-033 (REQ-F-003/REQ-F-005): once a provider instance is selected, the model field becomes
// visible and its combo box reflects the supplied model list independently of any other state.
TEST_F(BackgroundAiSettingsPanelQml, ModelFieldVisibleAndPopulatedWhenProviderSelected) {
  auto picker =
      createComponent(qml_engine_, QStringLiteral("ProviderModelPicker.qml"),
                      {{QStringLiteral("selectedProviderId"), QStringLiteral("instance-a")},
                       {QStringLiteral("modelNames"),
                        QVariant::fromValue(QStringList{QStringLiteral("model-a"), QStringLiteral("model-b")})}});
  ASSERT_NE(picker, nullptr);

  auto* modelField = picker->findChild<QObject*>(QStringLiteral("modelField"));
  auto* modelCombo = picker->findChild<QObject*>(QStringLiteral("modelCombo"));
  ASSERT_NE(modelField, nullptr);
  ASSERT_NE(modelCombo, nullptr);
  EXPECT_TRUE(modelField->property("visible").toBool());
  EXPECT_EQ(modelCombo->property("model").toStringList(),
            QStringList({QStringLiteral("model-a"), QStringLiteral("model-b")}));
}

// T-033: selecting a provider instance in the picker emits providerSelected with that instance's
// provider_id — the identifier the controller actually needs, not a display label or list index.
TEST_F(BackgroundAiSettingsPanelQml, SelectingProviderEmitsProviderSelectedWithInstanceId) {
  QVariantList instances{providerRow(QStringLiteral(""), QStringLiteral("Use chat model")),
                         providerRow(QStringLiteral("instance-a"), QStringLiteral("Instance A")),
                         providerRow(QStringLiteral("instance-b"), QStringLiteral("Instance B"))};
  auto picker = createComponent(qml_engine_, QStringLiteral("ProviderModelPicker.qml"),
                                {{QStringLiteral("providerInstances"), QVariant::fromValue(instances)}});
  ASSERT_NE(picker, nullptr);

  QSignalSpy spy(picker.get(), SIGNAL(providerSelected(QString)));
  activateIndex(picker->findChild<QObject*>(QStringLiteral("providerInstanceCombo")), 2);

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().constFirst().toString(), QStringLiteral("instance-b"));
}

// T-033: selecting a model in the (already-visible) model combo emits modelSelected with the
// model's display text.
TEST_F(BackgroundAiSettingsPanelQml, SelectingModelEmitsModelSelectedWithModelName) {
  auto picker =
      createComponent(qml_engine_, QStringLiteral("ProviderModelPicker.qml"),
                      {{QStringLiteral("selectedProviderId"), QStringLiteral("instance-a")},
                       {QStringLiteral("modelNames"),
                        QVariant::fromValue(QStringList{QStringLiteral("model-a"), QStringLiteral("model-b")})}});
  ASSERT_NE(picker, nullptr);

  QSignalSpy spy(picker.get(), SIGNAL(modelSelected(QString)));
  activateIndex(picker->findChild<QObject*>(QStringLiteral("modelCombo")), 1);

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().constFirst().toString(), QStringLiteral("model-b"));
}

// T-034 (REQ-F-001): the panel must load without error against a fake controller.
TEST_F(BackgroundAiSettingsPanelQml, PanelLoadsWithoutError) {
  FakeUtilitySettingsController controller;
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);
}

// REQ-C-002: the utility panel reuses ProviderSettingsScaffold while suppressing controls that
// apply only to an individual provider instance.
TEST_F(BackgroundAiSettingsPanelQml, ReusesScaffoldWithoutProviderSpecificControls) {
  FakeUtilitySettingsController controller;
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);

  auto* providerNameField = panel->findChild<QObject*>(QStringLiteral("providerNameField"));
  auto* providerEnabledSwitch = panel->findChild<QObject*>(QStringLiteral("providerEnabledCheckBox"));
  auto* deleteProviderButton = panel->findChild<QObject*>(QStringLiteral("deleteProviderButton"));
  ASSERT_NE(providerNameField, nullptr);
  ASSERT_NE(providerEnabledSwitch, nullptr);
  ASSERT_NE(deleteProviderButton, nullptr);
  EXPECT_FALSE(providerNameField->property("visible").toBool());
  EXPECT_FALSE(providerEnabledSwitch->property("visible").toBool());
  EXPECT_FALSE(deleteProviderButton->property("visible").toBool());
}

// T-034 (REQ-F-018): Save/Discard enabled state tracks the controller's dirty/canSave properties,
// not some panel-local copy of them.
TEST_F(BackgroundAiSettingsPanelQml, SaveDiscardEnabledStateTracksController) {
  FakeUtilitySettingsController controller;
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);
  auto* discardButton = panel->findChild<QObject*>(QStringLiteral("discardBackgroundAiButton"));
  auto* saveButton = panel->findChild<QObject*>(QStringLiteral("saveBackgroundAiButton"));
  ASSERT_NE(discardButton, nullptr);
  ASSERT_NE(saveButton, nullptr);
  EXPECT_FALSE(discardButton->property("enabled").toBool());
  EXPECT_FALSE(saveButton->property("enabled").toBool());

  controller.setDirty(true);
  controller.setCanSave(true);
  QTRY_VERIFY(discardButton->property("enabled").toBool());
  QTRY_VERIFY(saveButton->property("enabled").toBool());
}

// T-034 (REQ-F-018/019/020): toggling/picking a field marks dirty without persisting (there is no
// persistence call from the panel itself — only Save/Discard invoke the controller), and clicking
// Discard/Save invokes exactly the corresponding controller method.
TEST_F(BackgroundAiSettingsPanelQml, DiscardAndSaveButtonsInvokeController) {
  FakeUtilitySettingsController controller;
  controller.setDirty(true);
  controller.setCanSave(true);
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);

  click(panel->findChild<QObject*>(QStringLiteral("discardBackgroundAiButton")));
  click(panel->findChild<QObject*>(QStringLiteral("saveBackgroundAiButton")));

  EXPECT_EQ(controller.discardCount(), 1);
  EXPECT_EQ(controller.saveCount(), 1);
}

// T-034 (REQ-F-007/009): toggling the chat-title generation switch writes straight through to the
// controller's chatTitleGenerationEnabled property, and disables the override picker underneath it.
TEST_F(BackgroundAiSettingsPanelQml, ChatTitleToggleUpdatesControllerAndGatesOverridePicker) {
  FakeUtilitySettingsController controller;
  controller.setChatTitleGenerationEnabled(true);
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);
  auto* toggleSwitch = panel->findChild<QObject*>(QStringLiteral("chatTitleGenerationEnabledSwitch"));
  auto* overridePicker = panel->findChild<QObject*>(QStringLiteral("titleOverrideModelPicker"));
  ASSERT_NE(toggleSwitch, nullptr);
  ASSERT_NE(overridePicker, nullptr);
  EXPECT_TRUE(overridePicker->property("enabled").toBool());

  toggleSwitch->setProperty("checked", false);
  toggle(toggleSwitch);

  EXPECT_FALSE(controller.chatTitleGenerationEnabled());
  QTRY_VERIFY(!overridePicker->property("enabled").toBool());
}

// T-034 (REQ-F-006/010): the default-model and title-override pickers write into the correct,
// distinct controller properties — proves the two ProviderModelPicker instances are wired to their
// intended targets, not accidentally swapped or aliased.
TEST_F(BackgroundAiSettingsPanelQml, PickersWriteIntoDistinctControllerProperties) {
  FakeUtilitySettingsController controller;
  QVariantList instances{providerRow(QStringLiteral("instance-a"), QStringLiteral("Instance A"))};
  controller.setProviderInstances(instances);
  auto panel = createComponent(qml_engine_, QStringLiteral("BackgroundAiSettingsPanel.qml"),
                               {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  ASSERT_NE(panel, nullptr);

  auto* defaultPicker = panel->findChild<QObject*>(QStringLiteral("defaultUtilityModelPicker"));
  auto* overridePicker = panel->findChild<QObject*>(QStringLiteral("titleOverrideModelPicker"));
  ASSERT_NE(defaultPicker, nullptr);
  ASSERT_NE(overridePicker, nullptr);

  QMetaObject::invokeMethod(defaultPicker, "providerSelected", Qt::DirectConnection,
                            Q_ARG(QString, QStringLiteral("instance-a")));
  EXPECT_EQ(controller.defaultProviderId(), QStringLiteral("instance-a"));
  EXPECT_TRUE(controller.titleOverrideProviderId().isEmpty());
}

}  // namespace

#include "test_background_ai_settings_panel.moc"
