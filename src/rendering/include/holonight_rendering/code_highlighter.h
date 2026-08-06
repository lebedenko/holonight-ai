#pragma once

#include <QObject>
#include <QQuickTextDocument>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>

namespace holonight_rendering {

struct SyntaxHighlighterForeign {
  Q_GADGET
  QML_FOREIGN(KSyntaxHighlighting::SyntaxHighlighter)
  QML_ANONYMOUS
};

// QML-instantiable: each ChatCodeBlock.qml creates exactly one of these and attaches it to its code
// TextEdit's document.
class CodeHighlighter : public KSyntaxHighlighting::SyntaxHighlighter {
  Q_OBJECT
  QML_ELEMENT

  // The language after REQ-F-003 normalization (or "" / an unknown string). Setting it resolves a
  // KSyntaxHighlighting::Definition and calls the base class's setDefinition(); an unresolved
  // language clears the definition (REQ-F-004/REQ-NF-003: no highlighting, no crash, no log spam).
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
  // True once a valid Definition is applied — ChatCodeBlock's header uses this (rather than
  // re-deriving the same lookup a second time in QML) to decide between the normalized language
  // label and the neutral "plain text" label.
  Q_PROPERTY(bool highlightingActive READ highlightingActive NOTIFY highlightingActiveChanged)

 public:
  explicit CodeHighlighter(QObject* parent = nullptr);
  ~CodeHighlighter() override = default;
  CodeHighlighter(const CodeHighlighter&) = delete;
  CodeHighlighter& operator=(const CodeHighlighter&) = delete;
  CodeHighlighter(CodeHighlighter&&) = delete;
  CodeHighlighter& operator=(CodeHighlighter&&) = delete;

  // Attaches this highlighter to the QTextDocument backing a QML TextEdit/TextArea. Must be called
  // once, before or after setLanguage() (order doesn't matter — both paths call rehighlight() only
  // once state is complete, avoiding a visible uncolored flash).
  Q_INVOKABLE void attachTo(QQuickTextDocument* document);

  [[nodiscard]] QString language() const { return language_; }
  void setLanguage(const QString& normalizedLanguage);
  [[nodiscard]] bool highlightingActive() const { return highlighting_active_; }

  // Re-reads the active HoloNight scheme and re-applies the matching KSyntaxHighlighting theme.
  // Called from QML on HoloniightPalette::paletteChanged (see DESIGN.md §6.4) — never re-parses or
  // re-derives the language, only swaps the Theme.
  Q_INVOKABLE void refreshTheme();

 signals:
  void languageChanged();
  void highlightingActiveChanged();

 private:
  void applyThemeForActiveScheme();  // shared by the constructor and refreshTheme()

  KSyntaxHighlighting::Repository repository_;
  QString language_;
  bool highlighting_active_ = false;
};

}  // namespace holonight_rendering
