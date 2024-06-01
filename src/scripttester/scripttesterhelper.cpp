/*
    SPDX-FileCopyrightText: 2024-2024 Jonathan Poelen <jonathan.poelen@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Undefine this because we don't want our i18n*() method names to be turned
// into i18nd*(). This means any translated string in this file should use
// i18nd("ktexteditor6", "foo") instead of i18n("foo")
#undef TRANSLATION_DOMAIN

#include "scripttesterhelper.h"
#include "katepartdebug.h"
#include "katedocument.h"
#include "kateview.h"
#include "kateconfig.h"

#include <algorithm>
#include <cstdio>

#include <QFile>
#include <QJSEngine>
#include <QLatin1StringView>

namespace KTextEditorTester
{

using namespace Qt::Literals::StringLiterals;

namespace
{

constexpr QStringView operator""_sv(const char16_t *str, size_t size) noexcept
{
    return QStringView(str, size);
}

static QString getPath(QJSEngine *engine, const QString& name, const QStringList& dirs, QString &error)
{
    for (const QString &dir : dirs) {
        QString path = dir % u'/' % name;
        if (QFile::exists(path)) {
            return path;
        }
    }

    error = u"file '%1' not found in %2"_sv.arg(name).arg(dirs.join(u", "_sv));
    engine->throwError(QJSValue::URIError, error);
    return QString();
}

static bool readFile(QJSEngine *engine, const QString &sourceUrl, QString &content, QString &error)
{
    QFile file(sourceUrl);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        content = stream.readAll();
        return true;
    }

    error = u"reading error for '%1': %2"_sv.arg(sourceUrl).arg(file.errorString());
    engine->throwError(QJSValue::URIError, error);
    return false;
}

template<class T>
struct SetValue
{
    T& value;

    void operator()(const T &value)
    {
        this->value = value;
    }
};

template<class T>
SetValue(T&) -> SetValue<T>;

template<class SetFn>
static bool readString(const QJSValue &obj, const QString& name, SetFn&& setFn)
{
    auto value = obj.property(name);
    if (!value.isUndefined()) {
        setFn(value.toString());
        return true;
    }
    return false;
}

template<class SetFn>
static bool readInt(const QJSValue &obj, const QString& name, SetFn&& setFn)
{
    auto value = obj.property(name);
    if (!value.isUndefined()) {
        setFn(value.toInt());
        return true;
    }
    return false;
}

template<class SetFn>
static bool readBool(const QJSValue &obj, const QString& name, SetFn&& setFn)
{
    auto value = obj.property(name);
    if (!value.isUndefined()) {
        setFn(value.toBool());
        return true;
    }
    return false;
}

template<class SetFn>
static bool readChar(const QJSValue &obj, const QString& name, SetFn&& setFn)
{
    auto value = obj.property(name);
    if (!value.isUndefined()) {
        QString s = value.toString();
        setFn(s.isEmpty() ? '\0'_L1 : s[0]);
        return true;
    }
    return false;
}

qsizetype computeOffsetDifference(QStringView a, QStringView b)
{
    qsizetype n = qMin(a.size(), b.size());
    qsizetype i = 0;
    for (; i < n; ++i) {
        if (a[i] != b[i]) {
            if (a[i].isLowSurrogate() && b[i].isLowSurrogate()) {
                return qMax(0, i - 1);
            }
            return i;
        }
    }
    return i;
}

} // anonymous namespace

TestFramework::Colors TestFramework::Colors::defaultColors()
{
    // TODO \x1b[K for background tabulation, but urxvt / old alacritty...
    // TODO read from environment variables
    static Colors colors = {
        .reset = u"\033[0m"_s,
        .debug = u"\033[31m"_s,
        .error = u"\033[31m"_s,
        .command = u"\033[32m"_s,
        .fileName = u"\x1b[34m"_s,
        .lineNumber = u"\x1b[35m"_s,
        .labelInfo = u"\x1b[37m"_s,
        .cursor = u"\x1b[40;1;33m"_s,
        .selection = u"\x1b[40;1;33m"_s,
        .secondaryCursor = u"\x1b[40;33m"_s,
        .secondarySelection = u"\x1b[40;33m"_s,
        .inSelection = u"\x1b[4m"_s,
        .virtualText = u"\x1b[4;35m"_s,
        .result = u"\x1b[40m"_s,
        .resultReplacement = u"\x1b[7;36m"_s,
        .differ = u"\x1b[7m"_s,
    };
    return colors;
}

struct TestFramework::TextItem {
    // ordered by priority
    enum Kind {
        SelectionEnd,
        SecondarySelectionEnd,
        Cursor,
        SecondaryCursor,
        SelectionStart,
        SecondarySelectionStart,

        // virtual space for block selection
        VirtualText,

        NewLine,
        Tab,
        Backslash,
        DoubleQuote,

        MaxElement,
        StartCharacterElement = NewLine,
    };

    qsizetype pos;
    Kind kind;
    int virtualTextLen = 0;

    bool operator==(const TextItem &other) const
    {
        return pos == other.pos && kind == other.kind && virtualTextLen == other.virtualTextLen;
    }

    bool isSelectionStart() const
    {
        return kind == SelectionStart || kind == SecondarySelectionStart;
    }

    bool isCharacter() const
    {
        return kind >= TextItem::StartCharacterElement;
    }

    bool isSelectionEnd() const
    {
        return kind == SelectionEnd || kind == SecondarySelectionEnd;
    }
};

TestFramework::DocumentText::DocumentText() = default;
TestFramework::DocumentText::~DocumentText() = default;

std::size_t TestFramework::DocumentText::addKinds(QStringView str, int kind, QChar c)
{
    const auto n = items.size();

    qsizetype pos = 0;
    for (;;) {
        pos = str.indexOf(c, pos);
        if (pos == -1) {
            break;
        }
        items.push_back({pos, TextItem::Kind(kind)});
        ++pos;
    }

    return items.size() - n;
}

std::size_t TestFramework::DocumentText::addSelectionKinds(QStringView str, int kind, QChar start, QChar end)
{
    const auto n = items.size();

    qsizetype pos = 0;
    for (;;) {
        pos = str.indexOf(start, pos);
        if (pos == -1) {
            break;
        }
        qsizetype pos2 = str.indexOf(end, pos + 1);
        if (pos2 == -1) {
            break;
        }

        items.push_back({pos, TextItem::Kind(kind)});
        constexpr int offset = -4;
        static_assert(TextItem::SelectionStart + offset == TextItem::SelectionEnd);
        static_assert(TextItem::SecondarySelectionStart + offset == TextItem::SecondarySelectionEnd);
        items.push_back({pos2, TextItem::Kind(kind + offset)});

        pos = pos2 + 1;
    }

    return (items.size() - n) / 2;
}

void TestFramework::DocumentText::addVirtualText(QStringView str, QChar c)
{
    qsizetype pos = 0;
    for (;;) {
        pos = str.indexOf(c, pos);
        if (pos == -1) {
            break;
        }
        auto pos2 = pos + 1;
        while (pos2 < str.size() && str[pos2] == c) {
            ++pos2;
        }
        items.push_back({pos, TextItem::VirtualText, static_cast<int>(pos2 - pos)});
        pos = pos2;
    }
}

void TestFramework::DocumentText::sortItems()
{
    auto cmp = [](const TextItem &a, const TextItem &b){
        if (a.pos < b.pos) {
            return true;
        }
        if (a.pos > b.pos) {
            return false;
        }
        return a.kind < b.kind;
    };
    std::sort(items.begin(), items.end(), cmp);
}

bool TestFramework::Placeholders::hasCursor() const
{
    return enabled.testFlag(Placeholders::Cursor) && cursor.unicode();
}

bool TestFramework::Placeholders::hasSelection() const
{
    return enabled.testFlag(Placeholders::Selection) && selectionStart.unicode() && selectionEnd.unicode();
}

bool TestFramework::Placeholders::hasSecondaryCursor() const
{
    return enabled.testFlag(Placeholders::SecondaryCursor) && secondaryCursor.unicode();
}

bool TestFramework::Placeholders::hasSecondarySelection() const
{
    return enabled.testFlag(Placeholders::SecondarySelection) && secondarySelectionStart.unicode() && secondarySelectionEnd.unicode();
}

bool TestFramework::Placeholders::hasVirtualText() const
{
    return enabled.testFlag(Placeholders::VirtualText) && virtualText.unicode();
}

QString TestFramework::DocumentText::setText(QStringView input, const Placeholders &placeholders)
{
    items.clear();

    /* TODO
     * Multicursors not allowed when
     * - blockSelection
     * - overwriteMode
     * - viMode: (currentInputMode()->viewInputMode() == KTextEditor::View::InputMode
::ViInputMode)
     */

    addKinds(input, TextItem::NewLine, u'\n');
    QStringView conflictWithVirtualText;
    auto updateConflictWithVirtualText = [&](QChar c, QStringView name){
        if (conflictWithVirtualText.isEmpty() && placeholders.virtualText == c) {
            conflictWithVirtualText = name;
        }
    };

    if (placeholders.hasCursor()) {
        if (addKinds(input, TextItem::Cursor, placeholders.cursor) > 1) {
            return QStringLiteral("primary cursor declared multiple times");
        }
        updateConflictWithVirtualText(placeholders.cursor, u"cursor"_sv);
    }

    if (placeholders.hasSecondaryCursor()) {
        addKinds(input, TextItem::SecondaryCursor, placeholders.secondaryCursor);
        updateConflictWithVirtualText(placeholders.secondaryCursor, u"secondary cursor"_sv);
    }

    if (placeholders.hasSelection()) {
        if (addSelectionKinds(input, TextItem::SelectionStart, placeholders.selectionStart, placeholders.selectionEnd) > 1) {
            return QStringLiteral("primary selection declared multiple times");
        }
        updateConflictWithVirtualText(placeholders.selectionStart, u"selection start"_sv);
        updateConflictWithVirtualText(placeholders.selectionEnd, u"selection end"_sv);
    }

    if (placeholders.hasSecondarySelection()) {
        addSelectionKinds(input, TextItem::SecondarySelectionStart, placeholders.secondarySelectionStart, placeholders.secondarySelectionEnd);
        updateConflictWithVirtualText(placeholders.secondarySelectionStart, u"secondary selection start"_sv);
        updateConflictWithVirtualText(placeholders.secondarySelectionEnd, u"secondary selection end"_sv);
    }

    // TODO when block selection &&
    if (placeholders.hasVirtualText()) {
        if (!conflictWithVirtualText.isEmpty()) {
            return u"virtual text conflicts with %1"_sv.arg(conflictWithVirtualText);
        }
        addVirtualText(input, placeholders.virtualText);
    }

    sortItems();

    int line = 0;
    int charConsumedInPreviousLines = 0;
    cursor = KTextEditor::Cursor::invalid();
    auto selectionStart = KTextEditor::Cursor::invalid();
    auto selectionEnd = KTextEditor::Cursor::invalid();
    auto secondarySelectionStart = KTextEditor::Cursor::invalid();
    const TextItem *selectionEndItem = nullptr;
    qsizetype ignoredChars = 0;
    // 1 => in virtual text
    // -1 => error
    int virtualTextState = 0;

    text.clear();
    secondaryCursors.clear();
    secondarySelections.clear();

    for (auto &item : items) {
        item.pos -= ignoredChars;

        auto cursorFromCurrentItem = [&]{
            return KTextEditor::Cursor(line, item.pos - charConsumedInPreviousLines);
        };

        switch (item.kind) {
            case TextItem::NewLine:
                charConsumedInPreviousLines = item.pos + 1;
                ++line;
                if (virtualTextState > 0) {
                    virtualTextState = -1;
                }
                continue;
            case TextItem::VirtualText:
                ignoredChars += item.virtualTextLen;
                virtualTextState = 1;
                continue;
            case TextItem::Cursor:
                cursor = cursorFromCurrentItem();
                break;
            case TextItem::SelectionStart:
                selectionStart = cursorFromCurrentItem();
                break;
            case TextItem::SelectionEnd:
                selectionEndItem = &item;
                selectionEnd = cursorFromCurrentItem();
                break;
            case TextItem::SecondaryCursor:
                secondaryCursors.push_back(cursorFromCurrentItem());
                break;
            case TextItem::SecondarySelectionStart:
                secondarySelectionStart = cursorFromCurrentItem();
                break;
            case TextItem::SecondarySelectionEnd:
                secondarySelections.push_back({secondarySelectionStart, cursorFromCurrentItem()});
                break;
            default:;
        }

        if (virtualTextState > 0) {
            virtualTextState = 0;
        }

        const auto pos = text.size() + ignoredChars;
        text.append(input.sliced(pos, item.pos + ignoredChars - pos));
        ++ignoredChars;
    }
    text.append(input.sliced(text.size() + ignoredChars));

    if (virtualTextState) {
        return QStringLiteral("virtual text found, but not followed by a cursor or selection");
    }

    /*
     * The previous loop changes TextItem::i and the elements must be reordered
     * so that the cursor is after an end selection.
     * input: `a[b|]c` -> [{1, SelectionStart}, {3, Cursor}, {4, SelectionStop}]
     * update indexes:    [{1, SelectionStart}, {2, Cursor}, {2, SelectionStop}]
     * expected:          [{1, SelectionStart}, {2, SelectionStop}, {2, Cursor}]
     *                    -> `a[b]|c`
     */
    sortItems();

    /*
     * Check for empty or overlapping selections
     */
    int countSelection = 0;
    for (auto &item : items) {
        if (item.isSelectionStart()) {
            ++countSelection;
            if (countSelection & 1) {
                continue;
            }
        } else if (item.isSelectionEnd()) {
            ++countSelection;
            if (!(countSelection & 1)) {
                continue;
            }
        } else {
            continue;
        }
        return QStringLiteral("selection %1 is empty or overlaps").arg(countSelection / 2 + 1);
    }

    /*
     * Init selection and add to secondarySelections when not empty
     */
    selection = {selectionStart, selectionEnd};
    if (!secondarySelections.isEmpty()) {
        if (selectionStart.line() == -1) {
            return QStringLiteral("secondary selections are added without any primary selection");
        } else {
            secondarySelections.prepend(selection);
        }
    }

    /*
     * Init cursor and add to secondaryCursors when not empty
     */
    if (secondaryCursors.isEmpty()) {
        // no cursor specified
        if (cursor.line() == -1) {
            if (selectionEndItem) {
                // Add cursor to end of selection
                auto afterSelection = items.begin() + (selectionEndItem - items.data()) + 1;
                items.insert(afterSelection, {selectionEndItem->pos, TextItem::Cursor});
                cursor = selectionEnd;
            } else {
                // Add cursor to end of document
                items.push_back({input.size(), TextItem::Cursor});
                cursor = KTextEditor::Cursor(line, input.size() - charConsumedInPreviousLines);
            }
        }
    } else if (cursor.line() == -1) {
        return QStringLiteral("secondary cursors are added without any primary cursor");
    } else {
        secondaryCursors.prepend(cursor);
    }

    return QString();
}

TestFramework::TestFramework(FILE* output, Format format, const Colors &colors, const JSPaths &paths, QJSEngine *engine, KTextEditor::DocumentPrivate *doc, KTextEditor::ViewPrivate *view, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_doc(doc)
    , m_view(view)
    , m_stream(output)
    , m_colors(colors)
    , m_placeholders({
        .cursor = u'|',
        .selectionStart = u'[',
        .selectionEnd = u']',
        .secondaryCursor {},
        .secondarySelectionStart {},
        .secondarySelectionEnd = {},
        .virtualText = {},
        .enabled = Placeholders::Cursor
                 | Placeholders::Selection
                 | Placeholders::SecondaryCursor
                 | Placeholders::SecondarySelection
                 | Placeholders::VirtualText,
    })
    , m_format(format)
    , m_paths(paths)
{
    // add resources
    m_paths.libraries.push_back(QStringLiteral(":/ktexteditor/script/libraries"));
    m_paths.scripts.push_back(QStringLiteral(":/ktexteditor/script/files"));
}

QString TestFramework::read(const QString &name)
{
    // the error will also be written to this variable,
    // but ignored because QJSEngine will then throw an exception
    QString contentOrError;

    QString fullName = getPath(m_engine, name, m_paths.scripts, contentOrError);
    if (!fullName.isEmpty()) {
        readFile(m_engine, fullName, contentOrError, contentOrError);
    }
    return contentOrError;
}

void TestFramework::require(const QString &name)
{
    // check include guard
    auto it = m_libraryFiles.find(name);
    if (it != m_libraryFiles.end()) {
        // re-throw previous exception
        if (!it->isEmpty()) {
            m_engine->throwError(QJSValue::URIError, *it);
        }
        return;
    }

    it = m_libraryFiles.insert(name, QString());

    QString fullName = getPath(m_engine, name, m_paths.libraries, *it);
    if (fullName.isEmpty()) {
        return;
    }

    QString code;
    if (!readFile(m_engine, fullName, code, *it)) {
        return;
    }

    // eval in current script engine
    const QJSValue val = m_engine->evaluate(code, fullName);
    if (!val.isError()) {
        return;
    }

    // propagate exception
    *it = val.toString();
    m_engine->throwError(val);
}

// TODO bufferize
void TestFramework::debug(const QString &message)
{
    switch (m_format.debugTrace) {
        case DebugTrace::No:
            break;
        case DebugTrace::Yes: {
            m_engine->throwError(QJSValue());
            auto err = m_engine->catchError();
            writeLocation(
                err.property(QStringLiteral("fileName")).toString(),
                err.property(QStringLiteral("lineNumber")).toString()
            );
            break;
        }
    }

    // TODO add test name and other info with DebugMessageStrategy::AlwaysDisplay
    m_debugMsg += m_colors.debug % message % m_colors.reset % '\n'_L1;

    switch (m_format.debugStrategy) {
        case DebugMessageStrategy::AlwaysDisplay:
            m_stream << m_debugMsg;
            m_debugMsg.clear();
            m_stream.flush();
            break;
        case DebugMessageStrategy::IgnoreWhenNoError:
            break;
    }
}

QJSValue TestFramework::loadModule(const QString &fileName)
{
    auto result = m_engine->importModule(fileName);
    if (result.isError()) {
        m_engine->throwError(result);
    }
    return result;
}

void TestFramework::setConfig(const QJSValue &config)
{
    readString(config, QStringLiteral("encoding"), [&](QString encoding) {
        if (!encoding.isEmpty()) {
            m_doc->setEncoding(encoding);
        }
    });

#define SET_CONFIG(fn) [&](const auto &value) { fn(value); }
    readString(config, QStringLiteral("syntax"), SET_CONFIG(m_doc->setHighlightingMode));

    auto* docConfig = m_doc->config();

    readString(config, QStringLiteral("indentationMode"), SET_CONFIG(docConfig->setIndentationMode));

    readInt(config, QStringLiteral("indentationWidth"), SET_CONFIG(docConfig->setIndentationWidth));
    readInt(config, QStringLiteral("tabWidth"), SET_CONFIG(docConfig->setTabWidth));

    readBool(config, QStringLiteral("replaceTabs"), SET_CONFIG(docConfig->setReplaceTabsDyn));
    readBool(config, QStringLiteral("overrideMode"), SET_CONFIG(docConfig->setOvr));
    readBool(config, QStringLiteral("indentPastedTest"), SET_CONFIG(docConfig->setIndentPastedText));
    readBool(config, QStringLiteral("blockSelection"), SET_CONFIG(m_view->setBlockSelection));
#undef SET_CONFIG

#define SET_ENABLE(name) readBool(config, QStringLiteral("enable" #name), [&](bool on) { m_placeholders.enabled.setFlag(Placeholders::name, on); })
    SET_ENABLE(Cursor);
    SET_ENABLE(Selection);
    SET_ENABLE(SecondaryCursor);
    SET_ENABLE(SecondarySelection);
    SET_ENABLE(VirtualText);
#undef SET_ENABLE

#define SET_PLACEHOLDER(name) readChar(config, QStringLiteral(#name), [&](QChar c) { m_placeholders.name = c; })
    SET_PLACEHOLDER(cursor);
    SET_PLACEHOLDER(selectionStart);
    SET_PLACEHOLDER(selectionEnd);
    SET_PLACEHOLDER(secondaryCursor);
    SET_PLACEHOLDER(secondarySelectionStart);
    SET_PLACEHOLDER(secondarySelectionEnd);
    SET_PLACEHOLDER(virtualText);
#undef SET_PLACEHOLDER
}

QJSValue TestFramework::evaluate(const QString &code)
{
    QStringList stack;
    auto err = m_engine->evaluate(code, QString(), 1, &stack);
    if (!stack.isEmpty()) {
        m_engine->throwError(err);
    }
    return err;
}

void TestFramework::setInput(const QString& input)
{
    m_view->clearSecondarySelections();
    m_view->clearSecondaryCursors();
    m_view->clearSelection(false);

    const auto err = m_input.setText(input, m_placeholders);
    if (!err.isEmpty()) {
        m_engine->throwError(err);
        return;
    }

    m_doc->setText(m_input.text);

    if (m_input.secondarySelections.isEmpty()) {
        if (m_input.selection.start().line() != -1) {
            m_view->setSelection(m_input.selection);
        }
    } else {
        m_view->setSelections(m_input.secondarySelections);
    }

    if (m_input.secondaryCursors.isEmpty()) {
        m_view->setCursorPosition(m_input.cursor);
    } else {
        m_view->setSecondaryCursors(m_input.secondaryCursors);
    }
}

bool TestFramework::checkOutput(const QString& expected)
{
    /**
     * Init m_expected
     */
    const auto err = m_expected.setText(expected, m_placeholders);
    if (!err.isEmpty()) {
        m_engine->throwError(err);
        return false;
    }

    /**
     * Init m_output
     */
    m_output.text = m_doc->text();
    m_output.secondaryCursors = m_view->cursors();
    m_output.secondarySelections = m_view->selectionRanges();

    if (m_output.secondaryCursors.isEmpty()) {
        m_output.cursor = KTextEditor::Cursor::invalid();
    } else {
        m_output.cursor = m_output.secondaryCursors[0];
        m_output.secondaryCursors.pop_front();
    }

    if (m_output.secondarySelections.isEmpty()) {
        m_output.selection = KTextEditor::Range::invalid();
    } else {
        m_output.selection = m_output.secondarySelections[0];
        m_output.secondarySelections.pop_front();
    }

    /**
     * Check output
     */
    if (m_output.text == m_expected.text
        && (!m_placeholders.hasCursor() || m_output.cursor == m_expected.cursor)
        && (!m_placeholders.hasSelection() || m_output.selection == m_expected.selection)
        && (!m_placeholders.hasSecondaryCursor() || m_output.secondaryCursors == m_expected.secondaryCursors)
        && (!m_placeholders.hasSecondarySelection() || m_output.secondarySelections == m_expected.secondarySelections)
    ) {
        return true;
    }

    /**
     * Init m_output.items
     */
    struct CursorItem {
        KTextEditor::Cursor cursor;
        TextItem::Kind kind;

        bool operator < (const CursorItem &other) const
        {
            return cursor < other.cursor;
        }
    };
    std::vector<CursorItem> cursorItems;
    cursorItems.resize(m_output.secondaryCursors.size() + m_output.secondarySelections.size() * 2 + 3);
    auto it = cursorItems.begin();
    *it++ = {m_output.cursor, TextItem::Cursor};
    *it++ = {m_output.selection.start(), TextItem::SelectionStart};
    *it++ = {m_output.selection.end(), TextItem::SelectionEnd};
    for (auto &range : m_output.secondaryCursors) {
        *it++ = {range.start(), TextItem::SecondaryCursor};
    }
    for (auto &range : m_output.secondarySelections) {
        *it++ = {range.start(), TextItem::SecondarySelectionStart};
        *it++ = {range.end(), TextItem::SecondarySelectionEnd};
    }
    const auto end = it;
    std::sort(cursorItems.begin(), end);
    it = std::find_if(cursorItems.begin(), end, [](CursorItem &cursorItem){
        return cursorItem.cursor.isValid();
    });

    QStringView output = m_output.text;
    m_output.items.clear();

    qsizetype line = 0;
    qsizetype pos = 0;
    for (;;) {
        auto nextPos = output.indexOf(u'\n', pos);
        qsizetype lineLen = nextPos == -1 ? output.size() - pos : nextPos - pos;
        for (; it != end && it->cursor.line() == line; ++it) {
            auto virtualTextLen = (it->cursor.column() > lineLen) ? lineLen - it->cursor.column() : 0;
            m_output.items.push_back({pos + it->cursor.column() - virtualTextLen, it->kind, static_cast<int>(virtualTextLen)});
        }
        if (nextPos == -1) {
            break;
        }
        m_output.items.push_back({nextPos, TextItem::NewLine});
        pos = nextPos + 1;
        ++line;
    }

    m_output.sortItems();

    return false;
}

void TestFramework::cmdDiffer(int nthStack, const QString &code, const QJSValue &exception, const QString &result, const QString &expectedResult, int options)
{
    constexpr int outputIsOk = 1 << 0;
    constexpr int containsResultOrError = 1 << 1;
    constexpr int expectedErrorButNoError = 1 << 2;
    constexpr int expectedNoErrorButError = 1 << 3;
    constexpr int isResultNotError = 1 << 4;
    constexpr int sameResultOrError = 1 << 5;

    writeLocation(nthStack);

    m_stream << m_colors.error;
    if (!(options & outputIsOk) && (options & (containsResultOrError | expectedNoErrorButError))) {
        m_stream << "Output and Result differs"_L1;
    } else if (options & (containsResultOrError | expectedNoErrorButError)) {
        m_stream << "Result differs"_L1;
    } else {
        m_stream << "Output differs"_L1;
    }
    m_stream << m_colors.reset << '\n';

    writeCommand(code);
    writeDebug();
    writeResult(options & outputIsOk);

    /**
     * Result block (exception caught or return value)
     */
    if (options & containsResultOrError) {
        m_stream << "  ---------\n"_L1;
        if (options & expectedErrorButNoError) {
            m_stream << m_colors.error << "  An error is expected, but there is none"_L1 << m_colors.reset << '\n';
        } else {
            auto label = ((options & isResultNotError) ? "  result:   "_L1 : "  error:    "_L1);
            if (options & sameResultOrError) {
                m_stream << m_colors.labelInfo << label << m_colors.reset;
            } else {
                m_stream << label;
            }

            if (options & sameResultOrError) {
                m_stream << m_colors.result << result << m_colors.reset << '\n';
            } else {
                auto idiffer = computeOffsetDifference(result, expectedResult);
                auto writeText = [&](QStringView s){
                    m_stream << m_colors.result << s.first(idiffer) << m_colors.reset << m_colors.differ << s.sliced(idiffer) << m_colors.reset << '\n';
                };
                writeText(result);
                m_stream << "  expected: "_L1;
                writeText(expectedResult);
            }
        }
    }

    /**
     * Uncaught exception block
     */
    if (options & expectedNoErrorButError) {
        m_stream << "  ---------\n"_L1 << m_colors.error << "  Uncaught exception: "_L1 << exception.toString() << '\n';
        writeException(exception, u"  | "_sv);
    }

    m_stream << '\n';
}

void TestFramework::writeException(const QJSValue& exception, QStringView prefix)
{
    auto stack = exception.property(QStringLiteral("stack")).toString();
    auto stackView = QStringView(stack);

    // skips the first line that refers to the internal call
    if (stackView.startsWith('%'_L1)) {
        auto pos = stackView.indexOf('\n'_L1);
        if (pos != -1) {
            stackView = stackView.sliced(pos + 1);
        }
    }

    // color lines
    while (!stackView.isEmpty()) {
        m_stream << m_colors.error << prefix;

        // format: func '@file:' '//'? fileName ':' lineNumber '\n'

        // color func
        int pos = stackView.indexOf('@'_L1);
        if (pos != -1) {
            m_stream << m_colors.command << stackView.first(pos) << m_colors.reset << m_colors.error << '@';
            stackView = stackView.sliced(pos + 1);
        }

        // search prefix
        pos = 0;
        if (stackView.startsWith("file://"_L1)) {
            pos = 7;
        } else if (stackView.startsWith("file:"_L1)) {
            pos = 5;
        }

        auto endLine = stackView.indexOf('\n'_L1, pos);
        auto line = stackView.mid(pos, endLine == -1 ? -1 : endLine - pos);

        // color fileName and lineNumber
        if (pos == 0) {
            m_stream << line;
        } else {
            m_stream << stackView.first(pos);
            auto i = line.lastIndexOf(':'_L1);
            auto fileName = line.mid(0, i);
            auto lineNumber = line.mid(i + 1);
            m_stream
                << m_colors.fileName << fileName << m_colors.reset
                << m_colors.error << ':' << m_colors.reset
                << m_colors.lineNumber << lineNumber;
        }
        m_stream << m_colors.reset << '\n';

        if (endLine == -1) {
            break;
        }

        // next line
        stackView = stackView.sliced(pos + line.size() + 1);
    }
}

void TestFramework::writeLocation(int nthStack)
{
    m_engine->throwError(QString());
    auto err = m_engine->catchError().property(QStringLiteral("stack")).toString();

    // stack format: funcname '@' fileName ':' lineNumber '\n'

    // skip lines
    qsizetype startIndex = 0;
    while (nthStack-- > 0) {
        qsizetype i = err.indexOf('\n'_L1, startIndex);
        if (i == -1) {
            break;
        }
        startIndex = i + 1;
    }

    // skip funcname
    qsizetype i = err.indexOf('@'_L1, startIndex);
    if (i != -1) {
        startIndex = i + 1;
    }

    i = err.indexOf('\n'_L1, startIndex);
    auto line = QStringView(err).mid(startIndex, i == -1 ? -1 : i - startIndex);

    i = line.lastIndexOf(':'_L1);
    auto fileName = line.mid(0, i);
    auto lineNumber = line.mid(i + 1);

    writeLocation(fileName, lineNumber);
}

// TODO translate message ?

void TestFramework::writeCommand(QStringView cmd)
{
    m_stream
      << m_colors.error << '`' << m_colors.reset
      << m_colors.command << cmd << m_colors.reset
      << m_colors.error << '`' << m_colors.reset
      << ":\n"_L1;
}

void TestFramework::writeDebug()
{
    m_stream << m_debugMsg;
    m_debugMsg.clear();
}

void TestFramework::writeResult(bool outputIsOk)
{
    static constexpr Placeholders defPlaceholders {
        .cursor = u'|',
        .selectionStart = u'[',
        .selectionEnd = u']',
        .secondaryCursor = u'@',
        .secondarySelectionStart = u'<',
        .secondarySelectionEnd = u'>',
        .virtualText = u'~',
        .enabled {},
    };
    auto getPlaceholder = [&](QChar Placeholders::* m){
        return QStringView((m_placeholders.*m).unicode() ? &(m_placeholders.*m) : &(defPlaceholders.*m), 1);
    };

    QStringView replacements[TextItem::MaxElement][2];
    replacements[TextItem::Cursor][0] = m_colors.cursor;
    replacements[TextItem::Cursor][1] = getPlaceholder(&Placeholders::cursor);
    replacements[TextItem::SelectionStart][0] = m_colors.selection;
    replacements[TextItem::SelectionStart][1] = getPlaceholder(&Placeholders::selectionStart);
    replacements[TextItem::SelectionEnd][0] = m_colors.selection;
    replacements[TextItem::SelectionEnd][1] = getPlaceholder(&Placeholders::selectionEnd);
    replacements[TextItem::SecondaryCursor][0] = m_colors.secondaryCursor;
    replacements[TextItem::SecondaryCursor][1] = getPlaceholder(&Placeholders::secondaryCursor);
    replacements[TextItem::SecondarySelectionStart][0] = m_colors.secondarySelection;
    replacements[TextItem::SecondarySelectionStart][1] = getPlaceholder(&Placeholders::secondarySelectionStart);
    replacements[TextItem::SecondarySelectionEnd][0] = m_colors.secondarySelection;
    replacements[TextItem::SecondarySelectionEnd][1] = getPlaceholder(&Placeholders::secondarySelectionEnd);
    auto virtualTextPlaceholder = getPlaceholder(&Placeholders::virtualText)[0];
    // replacements[TextItem::VirtualText][0] = m_colors.virtualText;
    // replacements[TextItem::VirtualText][1] = getPlaceholder(&Placeholders::virtualText);

    QChar tabBuffer[20];
    auto makeTabString = [&]{
        auto tabWidth = qMin(m_doc->config()->tabWidth(), static_cast<int>(std::size(tabBuffer)));
        if (tabWidth > 0) {
            auto &repl = m_format.textReplacement;
            auto tab1 = repl.tab1.unicode() ? repl.tab1 : u'—';
            auto tab2 = repl.tab2.unicode() ? repl.tab2 : repl.tab1.unicode() ? tab1 : u'⇥';
            for (int i = 0; i < tabWidth - 1; ++i) {
                tabBuffer[i] = tab1;
            }
            tabBuffer[tabWidth - 1] = tab2;
        }
        return QStringView(tabBuffer, tabWidth);
    };

    bool alignNL = true;

    switch (m_format.commandTextStrategy) {
        case CommandTextStrategy::Raw:
            break;
        case CommandTextStrategy::EscapeForDoubleQuote:
            replacements[TextItem::NewLine][0] = m_colors.resultReplacement;
            replacements[TextItem::NewLine][1] = u"\\n"_sv;
            replacements[TextItem::Tab][0] = m_colors.resultReplacement;
            replacements[TextItem::Tab][1] = u"\\t"_sv;
            replacements[TextItem::Backslash][0] = m_colors.resultReplacement;
            replacements[TextItem::Backslash][1] = u"\\\\"_sv;
            replacements[TextItem::DoubleQuote][0] = m_colors.resultReplacement;
            replacements[TextItem::DoubleQuote][1] = u"\\\""_sv;
            alignNL = false;
            break;
        case CommandTextStrategy::ReplaceNewLineAndTabWithLiteral:
            replacements[TextItem::NewLine][0] = m_colors.resultReplacement;
            replacements[TextItem::NewLine][1] = u"\\n"_sv;
            replacements[TextItem::Tab][0] = m_colors.resultReplacement;
            replacements[TextItem::Tab][1] = u"\\t"_sv;
            alignNL = false;
            break;
        case CommandTextStrategy::ReplaceNewLineAndTabWithPlaceholder:
            replacements[TextItem::NewLine][0] = m_colors.resultReplacement;
            replacements[TextItem::NewLine][1] = m_format.textReplacement.newLine.unicode() ? QStringView(&m_format.textReplacement.newLine, 1) : u"↵"_sv;
            replacements[TextItem::Tab][0] = m_colors.resultReplacement;
            replacements[TextItem::Tab][1] = makeTabString();
            break;
        case CommandTextStrategy::ReplaceTabWithPlaceholder:
            replacements[TextItem::Tab][0] = m_colors.resultReplacement;
            replacements[TextItem::Tab][1] = makeTabString();
            break;
    }

    auto writeText = [&](DocumentText &docText, qsizetype idiffer = -1, qsizetype idifferVirtualText = -1){
        QStringView str = docText.text;

        switch (m_format.commandTextStrategy) {
            case CommandTextStrategy::Raw:
                break;
            case CommandTextStrategy::EscapeForDoubleQuote:
                docText.addKinds(str, TextItem::Backslash, u'\\');
                docText.addKinds(str, TextItem::DoubleQuote, u'"');
                [[fallthrough]];
            case CommandTextStrategy::ReplaceNewLineAndTabWithLiteral:
            case CommandTextStrategy::ReplaceNewLineAndTabWithPlaceholder:
            case CommandTextStrategy::ReplaceTabWithPlaceholder:
                docText.addKinds(str, TextItem::Tab, u'\t');
                docText.sortItems();
                break;
        }

        QStringView inSelection;
        QStringView inDiffer;

        // TODO blockSelection
        // TODO differ style (reverse video) + carret

        qsizetype i = 0;
        for (const TextItem &item : docText.items) {
            if (item.kind == TextItem::VirtualText) {
                // TODO insert spaces ?
                // TODO use idifferVirtualText
                continue;
            }

            if (i != item.pos) {
                auto textFragment = str.sliced(i, item.pos - i);
                m_stream << m_colors.result << inDiffer << inSelection;
                if (idiffer != -1 && i <= idiffer && idiffer <= item.pos) {
                    m_stream << textFragment.sliced(0, idiffer - i);
                    m_stream << m_colors.differ;
                    m_stream << textFragment.sliced(idiffer - i);
                } else {
                    m_stream << textFragment;
                }
                m_stream << m_colors.reset;
            }

            if (idiffer != -1 && idiffer <= item.pos) {
                inDiffer = m_colors.differ;
            }

            if (item.virtualTextLen) {
                const auto &replacement = replacements[TextItem::VirtualText];
                m_stream << replacement[0] << inDiffer;
                m_stream.setFieldWidth(item.virtualTextLen);
                m_stream.setPadChar(virtualTextPlaceholder);
                m_stream << inSelection;
                m_stream.setFieldWidth(0);
                m_stream << replacement[1];
            }

            if (item.isSelectionEnd()) {
                inSelection = QStringView();
            }

            const auto &replacement = replacements[item.kind];
            m_stream << replacement[0] << inDiffer << inSelection << replacement[1];
            bool insertNewLine = (alignNL && item.kind == TextItem::NewLine);
            if (insertNewLine || !replacement[0].isEmpty() || !inSelection.isEmpty() || !inDiffer.isEmpty()) {
                m_stream << m_colors.reset;
            }
            if (insertNewLine) {
                m_stream << "\n            "_L1;
            }

            if (item.isSelectionStart()) {
                inSelection = m_colors.inSelection;
            }

            i = item.pos + item.isCharacter();
        }

        if (i != str.size()) {
            m_stream << m_colors.result << inDiffer << str.sliced(i) << m_colors.reset;
        }

        m_stream << '\n';
    };

    auto writeLabel = [&](QLatin1StringView label) {
        if (!outputIsOk) {
            m_stream << label;
        } else {
            m_stream << m_colors.labelInfo << label << m_colors.reset;
        }
    };

    writeLabel("  input:    "_L1);
    writeText(m_input);

    writeLabel("  output:   "_L1);
    if (outputIsOk) {
        writeText(m_expected);
    }
    else {
        auto idiffer = computeOffsetDifference(m_output.text, m_expected.text);
        auto it1 = m_output.items.begin();
        auto it2 = m_expected.items.begin();
        qsizetype idifferVirtualText = -1;
        for (; ;++it1, ++it2) {
            if (it1 == m_output.items.end() || it2 == m_expected.items.end()) {
                if (it1 != m_output.items.end()) {
                    idiffer = qMin(idiffer, it1->pos);
                }
                if (it2 != m_output.items.end()) {
                    idiffer = qMin(idiffer, it2->pos);
                }
                break;
            }

            if (*it1 != *it2) {
                if ((it1->kind == TextItem::VirtualText || it2->kind == TextItem::VirtualText) && it1->virtualTextLen != it2->virtualTextLen) {
                    idifferVirtualText = qMin(it1->virtualTextLen, it2->virtualTextLen);
                    idiffer = qMin(idiffer, qMin(it1->pos, it2->pos) + idifferVirtualText);
                } else {
                    idiffer = qMin(idiffer, qMin(it1->pos, it2->pos));
                }
                break;
            }

            if (it1->pos >= idiffer) {
                break;
            }
        }
        writeText(m_output, idiffer, idifferVirtualText);
        m_stream << "  expected: "_L1;
        writeText(m_expected, idiffer, idifferVirtualText);
    }
}

void TestFramework::writeLocation(QStringView fileName, QStringView lineNumber)
{
    // skip file prefix
    if (fileName.startsWith("file://"_L1)) {
        fileName = fileName.sliced(7);
    } else if (fileName.startsWith("file:"_L1)) {
        fileName = fileName.sliced(5);
    }

    // TODO optional column number (but always 0 -> output as a compiler)
    // TODO option to truncate path prefix
    m_stream << m_colors.fileName << fileName << m_colors.reset << ':' << m_colors.lineNumber << lineNumber << m_colors.reset << ": "_L1;
}

} // namespace kate

#include "moc_katescripthelpers.cpp"
