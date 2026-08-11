#include "holonight_rendering/code_highlighter.h"

#include <QFileInfo>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>
#include <holonight/appearance_reader.h>
#include <holonight/theme_catalog.h>

namespace holonight_rendering {

CodeHighlighter::CodeHighlighter(QObject* parent) : KSyntaxHighlighting::SyntaxHighlighter(parent) {
#ifdef HOLONIGHT_SYNTAX_THEME_DIR
  if (QFileInfo::exists(QStringLiteral(HOLONIGHT_SYNTAX_THEME_DIR))) {
    repository_.addCustomSearchPath(QStringLiteral(HOLONIGHT_SYNTAX_THEME_DIR));
  }
#endif
  applyThemeForActiveScheme();
}

void CodeHighlighter::attachTo(QQuickTextDocument* document) {
  if (document == nullptr) {
    return;
  }
  setDocument(document->textDocument());
  rehighlight();
}

void CodeHighlighter::setLanguage(const QString& normalizedLanguage) {
  if (language_ == normalizedLanguage) {
    return;
  }
  language_ = normalizedLanguage;

  // REQ-F-004/REQ-NF-003: an unresolved or empty language yields an invalid Definition, which
  // clears highlighting rather than crashing or logging.
  const KSyntaxHighlighting::Definition definition = repository_.definitionForName(normalizedLanguage);
  setDefinition(definition);

  const bool active = definition.isValid();
  if (active != highlighting_active_) {
    highlighting_active_ = active;
    emit highlightingActiveChanged();
  }

  emit languageChanged();
  if (document() != nullptr) {
    rehighlight();
  }
}

void CodeHighlighter::refreshTheme() {
  applyThemeForActiveScheme();
  if (document() != nullptr) {
    rehighlight();
  }
}

void CodeHighlighter::applyThemeForActiveScheme() {
  const Holonight::ThemeSchemeKind scheme = Holonight::AppearanceReader().appearance().theme_scheme;
  const QString schemeId = Holonight::schemeIdForKind(scheme);

  const KSyntaxHighlighting::Theme theme = repository_.theme(schemeId);
  if (theme.isValid()) {
    setTheme(theme);
  }
  // REQ-C-009: if the theme directory/file wasn't found, the Repository has no matching custom
  // theme and theme() returns an invalid Theme — setTheme() is skipped, leaving
  // KSyntaxHighlighting's own bundled default theme in place. No crash, no error log required.
}

}  // namespace holonight_rendering
