#include "holonight_application/utility_settings_controller.h"

#include <array>
#include <gtest/gtest.h>

namespace holonight_application {
namespace {

// T-032 (REQ-C-006): UtilitySettingsController must expose exactly the Q_PROPERTY/Q_INVOKABLE
// surface BackgroundAiSettingsPanel.qml and ProviderModelPicker.qml bind to. This is a static
// meta-object check — no QQmlEngine, no touching the real config.json (the constructor calls
// holonight_config::resolveConfigFilePath() and requires a live "HolonightChat"/"ChatViewModel"
// singleton, neither of which a headless unit test should exercise) — that catches an accidentally
// renamed or removed property before it becomes a QML "unknown property" runtime failure. The
// actual registration pipeline (qt6_extract_metatypes + combine-metatypes.cmake feeding
// _qt_internal_qml_type_registration) is verified by `task qmltypes-check`, and holonight-chat's
// own build succeeding with these QML files referencing the singleton is the end-to-end proof that
// "no unknown type errors" holds.
TEST(UtilitySettingsController, ExposesQmlPropertyAndInvokableSurface) {
  const QMetaObject& meta = UtilitySettingsController::staticMetaObject;

  const std::array requiredProperties{"providerInstances",
                                      "defaultProviderId",
                                      "defaultModelName",
                                      "defaultModelNames",
                                      "chatTitleGenerationEnabled",
                                      "titleOverrideProviderId",
                                      "titleOverrideModelName",
                                      "titleOverrideModelNames",
                                      "dirty",
                                      "canSave",
                                      "saveNotice",
                                      "saveNoticeStatus"};
  for (const char* name : requiredProperties) {
    EXPECT_NE(meta.indexOfProperty(name), -1) << name;
  }

  EXPECT_NE(meta.indexOfMethod("save()"), -1);
  EXPECT_NE(meta.indexOfMethod("discardDraft()"), -1);
  EXPECT_NE(meta.indexOfMethod("refreshModelsForInstance(QString)"), -1);
}

}  // namespace
}  // namespace holonight_application
