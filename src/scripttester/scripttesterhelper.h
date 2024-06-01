/*
    SPDX-FileCopyrightText: 2024-2024 Jonathan Poelen <jonathan.poelen@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KTEXTEDITOR_SCRIPT_TESTER_HELPERS_H
#define KTEXTEDITOR_SCRIPT_TESTER_HELPERS_H

#include <ktexteditor/cursor.h>
#include <ktexteditor/range.h>

#include <QFlags>
#include <QJSValue>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QStringView>
#include <QTextStream>

#include <vector>

#include <cstdio>


class QJSEngine;

namespace KTextEditor
{
    class DocumentPrivate;
    class ViewPrivate;
}

namespace KTextEditorTester
{

class TestFramework : public QObject
{
    Q_OBJECT

public:
    enum class DebugMessageStrategy : bool
    {
        AlwaysDisplay,
        IgnoreWhenNoError,
    };

    enum class DebugTrace : bool
    {
        No,
        Yes,
    };

    enum class CommandTextStrategy : unsigned char
    {
        Raw,
        EscapeForDoubleQuote,
        ReplaceNewLineAndTabWithLiteral,
        ReplaceNewLineAndTabWithPlaceholder,
        ReplaceTabWithPlaceholder,
    };

    enum class Color : bool
    {
        No,
        Yes,
    };

    struct Format {
        struct TextReplacement {
            QChar newLine {};
            QChar tab1 {};
            QChar tab2 {};
        };

        DebugMessageStrategy debugStrategy;
        DebugTrace debugTrace;
        CommandTextStrategy commandTextStrategy;
        TextReplacement textReplacement {};
    };

    struct JSPaths {
        QStringList libraries;
        QStringList scripts;
    };

    struct Colors {
        QString reset;
        QString debug;
        QString error;
        QString command;
        QString fileName;
        QString lineNumber;
        QString labelInfo;
        QString cursor;
        QString selection;
        QString secondaryCursor;
        QString secondarySelection;
        QString inSelection;
        QString virtualText;
        QString result;
        QString resultReplacement;
        QString differ;

        static Colors defaultColors();
    };

    explicit TestFramework(FILE* output, Format format, const Colors &colors, const JSPaths &paths, QJSEngine *engine, KTextEditor::DocumentPrivate *doc, KTextEditor::ViewPrivate *view, QObject *parent = nullptr);

    TestFramework(const TestFramework &) = delete;
    TestFramework& operator=(const TestFramework &) = delete;

    QTextStream &stream()
    {
        return m_stream;
    }

    // KTextEditor API
    //@{
    Q_INVOKABLE QString read(const QString &file);
    Q_INVOKABLE void require(const QString &file);
    Q_INVOKABLE void debug(const QString &msg);
    //@}

    Q_INVOKABLE QJSValue loadModule(const QString &fileName);
    Q_INVOKABLE void setConfig(const QJSValue &config);
    Q_INVOKABLE QJSValue evaluate(const QString &code);

    Q_INVOKABLE void setInput(const QString &input);
    Q_INVOKABLE bool checkOutput(const QString &expected);

    Q_INVOKABLE void cmdDiffer(int nthStack, const QString &code, const QJSValue &exception, const QString &result, const QString &expectedResult, int options);

    struct Placeholders {
        QChar cursor;
        QChar selectionStart;
        QChar selectionEnd;
        QChar secondaryCursor;
        QChar secondarySelectionStart;
        QChar secondarySelectionEnd;
        QChar virtualText;
        enum Type : unsigned char {
            None,
            Cursor,
            Selection,
            SecondaryCursor,
            SecondarySelection,
            VirtualText,
        };
        Q_DECLARE_FLAGS(Types, Type)
        Types enabled;

        bool hasCursor() const;
        bool hasSelection() const;
        bool hasSecondaryCursor() const;
        bool hasSecondarySelection() const;
        bool hasVirtualText() const;
    };

    void writeException(const QJSValue &exception, QStringView prefix);

private:
    void writeLocation(int nthStack);
    void writeLocation(QStringView fileName, QStringView lineNumber);
    void writeCommand(QStringView cmd);
    void writeDebug();
    void writeResult(bool sameInputOutput);

    struct TextItem;

    struct DocumentText {
        std::vector<TextItem> items;
        QString text;
        KTextEditor::Cursor cursor;
        KTextEditor::Range selection;
        QList<KTextEditor::Cursor> secondaryCursors;
        QList<KTextEditor::Range> secondarySelections;

        DocumentText();
        ~DocumentText();
        DocumentText(DocumentText &&) = default;
        DocumentText& operator=(DocumentText &&) = default;

        /// \return Error or empty string
        QString setText(QStringView input, const Placeholders &placeholders);

        std::size_t addKinds(QStringView str, int kind, QChar c);
        std::size_t addSelectionKinds(QStringView str, int kind, QChar start, QChar end);
        void addVirtualText(QStringView str, QChar c);
        void sortItems();
    };

    QJSEngine *m_engine;
    KTextEditor::DocumentPrivate *m_doc;
    KTextEditor::ViewPrivate *m_view;
    QTextStream m_stream;
    QString m_debugMsg;
    Colors m_colors;
    DocumentText m_input;
    DocumentText m_output;
    DocumentText m_expected;
    Placeholders m_placeholders;
    Format m_format;
    JSPaths m_paths;
    QMap<QString, QString> m_libraryFiles;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TestFramework::Placeholders::Types)

} // namespace Kate

#endif
