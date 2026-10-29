// Fixture state is visible to derived test cases and deterministic assertions.
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace {

class MarkdownBlockQml : public testing::Test {
 protected:
  std::unique_ptr<QObject> create(const QString& source, int width = 640, bool streaming = false) {
    engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    engine.addImportPath(QStringLiteral(QMARKDOWN_QML_IMPORT_PATH));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                                         QStringLiteral("/qml/shared/MarkdownBlock.qml")));
    EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
    if (!component.isReady()) {
      return {};
    }
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("markdown"), source},
                                                                           {QStringLiteral("width"), width},
                                                                           {QStringLiteral("streaming"), streaming}}));
    if (auto* item = qobject_cast<QQuickItem*>(object.get())) {
      window.resize(640, 480);
      item->setParentItem(window.contentItem());
      window.show();
    }
    return object;
  }

  static QString selected(QObject* object) {
    EXPECT_TRUE(QMetaObject::invokeMethod(object, "selectAll"));
    return object->property("selectedText").toString();
  }

  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  QQmlEngine engine;
  QQuickWindow window;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(MarkdownBlockQml, MixedGfmDocumentSupportsCrossBlockSelectionAndCopy) {
  const QString source = QString::fromUtf8(
      "# Heading\n\n> Quote\n> - nested\n>   - child\n\n- [x] done\n- [ ] todo\n\n"
      "| Name | Value |\n| --- | --- |\n| Unicode | café 🌙 |\n\n~~removed~~ https://example.org\n\n"
      "<details>\n<summary>literal HTML</summary>\n\n```cpp\nint value = 1;\n```\n\n</details>\n\nend");
  auto object = create(source);
  ASSERT_NE(object, nullptr);
  QTRY_VERIFY(object->property("contentHeight").toReal() > 0);
  EXPECT_TRUE(object->property("gfmEnabled").toBool());
  EXPECT_TRUE(object->property("syntaxHighlightingAvailable").toBool());
  const QString text = selected(object.get());
  for (const auto& fragment : {QStringLiteral("Heading"), QStringLiteral("child"), QStringLiteral("café 🌙"),
                               QStringLiteral("<details>"), QStringLiteral("int value = 1;"), QStringLiteral("end")}) {
    EXPECT_TRUE(text.contains(fragment)) << qPrintable(text);
  }
  ASSERT_TRUE(QMetaObject::invokeMethod(object.get(), "requestCopy"));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), text);
}

TEST_F(MarkdownBlockQml, CodeActionsCopyParserPayloadWithWhitespaceAndTrailingNewline) {
  auto object = create(QStringLiteral("before\n\n```unknown-language\n  a\t b  \n\n```\n\nafter"));
  ASSERT_NE(object, nullptr);
  QTRY_VERIFY(object->findChild<QObject*>(QStringLiteral("codeCopyButton")) != nullptr);
  auto* button = object->findChild<QObject*>(QStringLiteral("codeCopyButton"));
  auto* label = object->findChild<QObject*>(QStringLiteral("codeLanguageLabel"));
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->property("text").toString(), QStringLiteral("plain text"));
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("  a\t b  \n\n"));
  EXPECT_EQ(button->property("text").toString(), QStringLiteral("Copied"));
  QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), QStringLiteral("Copy"), 2000);
  EXPECT_FALSE(object->property("codeBlockWrapEnabled").toBool());
  EXPECT_TRUE(object->property("codeBlockHeightLimitEnabled").toBool());
  EXPECT_FALSE(object->property("codeBlockLineNumbersVisible").toBool());
}

TEST_F(MarkdownBlockQml, LongCodeBlockHasBoundedScrollableViewport) {
  const QString code = QStringLiteral("int value = 1;\n").repeated(100);
  auto object = create(QStringLiteral("```cpp\n") + code + QStringLiteral("```"));
  ASSERT_NE(object, nullptr);
  QTRY_VERIFY(object->findChild<QQuickItem*>(QStringLiteral("codeViewport")) != nullptr);
  auto* viewport = object->findChild<QQuickItem*>(QStringLiteral("codeViewport"));
  QTRY_VERIFY(viewport->height() > 0);
  EXPECT_LT(viewport->height(), viewport->property("contentHeight").toReal());
  EXPECT_TRUE(viewport->property("clip").toBool());
  viewport->setProperty("contentY", 100);
  EXPECT_EQ(viewport->property("contentY").toReal(), 100);
  auto* button = object->findChild<QObject*>(QStringLiteral("codeCopyButton"));
  ASSERT_NE(button, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), code);
}

TEST_F(MarkdownBlockQml, TerminalFlushAndSourceReplacementDiscardPendingSnapshots) {
  for (const bool changed : {false, true}) {
    auto object = create(QStringLiteral("```cpp\nint partial = 1;"), 420, true);
    ASSERT_NE(object, nullptr);
    const QString final = changed ? QStringLiteral("```cpp\nint final = 2;\n```\n\nfinished")
                                  : QStringLiteral("```cpp\nint partial = 1;");
    object->setProperty("markdown", final);
    object->setProperty("streaming", false);
    QCoreApplication::processEvents();
    EXPECT_TRUE(selected(object.get()).contains(changed ? QStringLiteral("finished") : QStringLiteral("partial")));
    object->setProperty("streaming", true);
    object->setProperty("markdown", QStringLiteral("obsolete streaming conversation"));
    object->setProperty("markdown", QStringLiteral("restored conversation"));
    object->setProperty("streaming", false);
    EXPECT_EQ(selected(object.get()), QStringLiteral("restored conversation"));
    QTest::qWait(100);
    EXPECT_EQ(selected(object.get()), QStringLiteral("restored conversation"));
  }
}

TEST_F(MarkdownBlockQml, PaletteReloadUpdatesExistingDocumentAndSyntaxTheme) {
  QTemporaryDir isolation;
  ASSERT_TRUE(isolation.isValid());
  const QByteArray original = qgetenv("HOLONIGHT_APPEARANCE_FILE");
  const auto restore = qScopeGuard([&] {
    if (original.isNull()) {
      qunsetenv("HOLONIGHT_APPEARANCE_FILE");
    } else {
      qputenv("HOLONIGHT_APPEARANCE_FILE", original);
    }
  });
  const QString path = isolation.filePath(QStringLiteral("appearance.toml"));
  qputenv("HOLONIGHT_APPEARANCE_FILE", path.toUtf8());
  auto writeScheme = [&](const QByteArray& scheme) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return false;
    }
    return file.write("version = 1\n[theme]\nscheme = \"" + scheme + "\"\n") > 0;
  };
  ASSERT_TRUE(writeScheme("holonight-dark"));
  auto object = create(QStringLiteral("paragraph\n\n```cpp\nint value = 1;\n```"));
  ASSERT_NE(object, nullptr);
  auto* palette = engine.singletonInstance<QObject*>("Holonight.Core", "HoloniightPalette");
  auto* style = object->property("style").value<QObject*>();
  auto* syntax = object->property("syntaxTheme").value<QObject*>();
  ASSERT_NE(palette, nullptr);
  ASSERT_NE(style, nullptr);
  ASSERT_NE(syntax, nullptr);
  const auto dark = style->property("bodyColor").value<QColor>();
  const auto dark_preset = style->property("preset");
  const QString text = selected(object.get());
  ASSERT_TRUE(writeScheme("holonight-light"));
  ASSERT_TRUE(QMetaObject::invokeMethod(palette, "reload"));
  QTRY_VERIFY(style->property("bodyColor").value<QColor>() != dark);
  EXPECT_NE(style->property("preset"), dark_preset);
  EXPECT_EQ(style->property("bodyColor"), palette->property("textPrimary"));
  EXPECT_EQ(syntax->property("keywordColor"), palette->property("accentViolet"));
  EXPECT_EQ(selected(object.get()), text);
}

TEST_F(MarkdownBlockQml, ZeroWidthAndNarrowResizeKeepDocumentUsable) {
  auto object =
      create(QStringLiteral("paragraph\n\n```cpp\n") + QString(600, QLatin1Char('x')) + QStringLiteral("\n```"), 0);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->property("contentHeight").toReal(), 0);
  for (const int width : {280, 800, 320}) {
    object->setProperty("width", width);
    QTRY_VERIFY(object->property("contentHeight").toReal() > 0);
    EXPECT_TRUE(selected(object.get()).contains(QString(600, QLatin1Char('x'))));
  }
}

}  // namespace
