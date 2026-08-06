#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

namespace holonight_application {

class ProviderDraftCommitter;
class ProviderDraftSession;
class ProviderInstanceRegistry;

class ProviderDraftGuard : public QObject {
  Q_OBJECT

 public:
  using NavigationAction = std::function<void()>;

  explicit ProviderDraftGuard(ProviderInstanceRegistry* registry, ProviderDraftCommitter* committer,
                              QObject* parent = nullptr);

  void setDraftSession(ProviderDraftSession* draft);
  [[nodiscard]] ProviderDraftSession* draftSession() const;
  [[nodiscard]] bool navigationPending() const;

  void requestNavigation(NavigationAction action);
  [[nodiscard]] bool saveAndContinue();
  [[nodiscard]] bool discardAndContinue();
  void cancelNavigation();

 Q_SIGNALS:
  void confirmationRequested();
  void navigationPendingChanged();

 private:
  void completeNavigation();
  void clearPendingNavigation();

  ProviderInstanceRegistry* registry_;
  ProviderDraftCommitter* committer_;
  QPointer<ProviderDraftSession> draft_;
  NavigationAction pending_navigation_;
};

}  // namespace holonight_application
