/*
    SPDX-FileCopyrightText: 2024 Jonathan Poelen <jonathan.poelen@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "kateconfig.h"
#include "katescripteditor.h"
#include "katedocument.h"
#include "kateview.h"
#include "katescriptview.h"
#include "katescriptdocument.h"
#include "scripttesterhelper.h"

#include <QApplication>
#include <QStandardPaths>
#include <QJSEngine>

// TODO
// QtMessageHandler ScriptTestBase::m_msgHandler = nullptr;
// static void noDebugMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
// {
//     switch (type) {
//     case QtDebugMsg:
//         break;
//     default:
//         ScriptTestBase::m_msgHandler(type, context, msg);
//     }
// }


constexpr QStringView operator""_sv(const char16_t *str, size_t size) noexcept
{
    return QStringView(str, size);
}

int main(int ac, char** av)
{
    if (ac != 2) {
        return 1;
    }
    QApplication app(ac, av);

    QStandardPaths::setTestModeEnabled(true);
    // m_msgHandler = qInstallMessageHandler(noDebugMessageOutput);
    KTextEditor::DocumentPrivate doc(true, false);
    KTextEditor::ViewPrivate view(&doc, nullptr);
    view.config()->setValue(KateViewConfig::AutoBrackets, false); // TODO

    QJSEngine engine;

    KateScriptEditor editor;

    KateScriptView viewObj(&engine);
    viewObj.setView(&view);

    KateScriptDocument docObj(&engine);
    docObj.setDocument(&doc);

    using TestFramework = KTextEditorTester::TestFramework;
    TestFramework testFramework(
        stderr,
        TestFramework::Format{
            TestFramework::DebugMessageStrategy::AlwaysDisplay,
            TestFramework::DebugTrace::Yes,
            TestFramework::CommandTextStrategy::ReplaceNewLineAndTabWithLiteral,
            // TestFramework::Format::TextReplacement{
            //     .newLine = u'␤',
            //     .tab1 = u'—',
            //     .tab2 = u'⇥',
            // },
        },
        TestFramework::Colors::defaultColors(),
        TestFramework::JSPaths{
            .libraries = {},
            .scripts = {},
        },
        &engine, &doc, &view
    );

    auto globalObject = engine.globalObject();

    // TODO to replace so as not to depend on environment files and to have suitable error reporting
    // export read & require function and add the require guard object
    QJSValue functions = engine.newQObject(&testFramework);
    globalObject.setProperty(QStringLiteral("read"), functions.property(QStringLiteral("read")));
    globalObject.setProperty(QStringLiteral("require"), functions.property(QStringLiteral("require")));

    // export debug function
    globalObject.setProperty(QStringLiteral("debug"), functions.property(QStringLiteral("debug")));

    globalObject.setProperty(QStringLiteral("editor"), engine.newQObject(&editor));
    globalObject.setProperty(QStringLiteral("view"), engine.newQObject(&viewObj));
    globalObject.setProperty(QStringLiteral("document"), engine.newQObject(&docObj));

    using TextStyle = KSyntaxHighlighting::Theme::TextStyle;
    globalObject.setProperty(QStringLiteral("dsNormal"), TextStyle::Normal);
    globalObject.setProperty(QStringLiteral("dsKeyword"), TextStyle::Keyword);
    globalObject.setProperty(QStringLiteral("dsFunction"), TextStyle::Function);
    globalObject.setProperty(QStringLiteral("dsVariable"), TextStyle::Variable);
    globalObject.setProperty(QStringLiteral("dsControlFlow"), TextStyle::ControlFlow);
    globalObject.setProperty(QStringLiteral("dsOperator"), TextStyle::Operator);
    globalObject.setProperty(QStringLiteral("dsBuiltIn"), TextStyle::BuiltIn);
    globalObject.setProperty(QStringLiteral("dsExtension"), TextStyle::Extension);
    globalObject.setProperty(QStringLiteral("dsPreprocessor"), TextStyle::Preprocessor);
    globalObject.setProperty(QStringLiteral("dsAttribute"), TextStyle::Attribute);
    globalObject.setProperty(QStringLiteral("dsChar"), TextStyle::Char);
    globalObject.setProperty(QStringLiteral("dsSpecialChar"), TextStyle::SpecialChar);
    globalObject.setProperty(QStringLiteral("dsString"), TextStyle::String);
    globalObject.setProperty(QStringLiteral("dsVerbatimString"), TextStyle::VerbatimString);
    globalObject.setProperty(QStringLiteral("dsSpecialString"), TextStyle::SpecialString);
    globalObject.setProperty(QStringLiteral("dsImport"), TextStyle::Import);
    globalObject.setProperty(QStringLiteral("dsDataType"), TextStyle::DataType);
    globalObject.setProperty(QStringLiteral("dsDecVal"), TextStyle::DecVal);
    globalObject.setProperty(QStringLiteral("dsBaseN"), TextStyle::BaseN);
    globalObject.setProperty(QStringLiteral("dsFloat"), TextStyle::Float);
    globalObject.setProperty(QStringLiteral("dsConstant"), TextStyle::Constant);
    globalObject.setProperty(QStringLiteral("dsComment"), TextStyle::Comment);
    globalObject.setProperty(QStringLiteral("dsDocumentation"), TextStyle::Documentation);
    globalObject.setProperty(QStringLiteral("dsAnnotation"), TextStyle::Annotation);
    globalObject.setProperty(QStringLiteral("dsCommentVar"), TextStyle::CommentVar);
    globalObject.setProperty(QStringLiteral("dsRegionMarker"), TextStyle::RegionMarker);
    globalObject.setProperty(QStringLiteral("dsInformation"), TextStyle::Information);
    globalObject.setProperty(QStringLiteral("dsWarning"), TextStyle::Warning);
    globalObject.setProperty(QStringLiteral("dsAlert"), TextStyle::Alert);
    globalObject.setProperty(QStringLiteral("dsOthers"), TextStyle::Others);
    globalObject.setProperty(QStringLiteral("dsError"), TextStyle::Error);

    // View and Document expose JS Range objects in the API, which will fail to work
    // if Range is not included. range.js includes cursor.js
    testFramework.require(QStringLiteral("range.js"));

    // translation functions (return untranslated text)
    engine.evaluate(QStringLiteral(
        "function i18n(text, ...arg) { return text; }\n"
        "function i18nc(context, text, ...arg) { return text; }\n"
        "function i18np(singular, plural, number, ...arg) { return number > 1 ? plural : singular; }\n"
        "function i18ncp(context, singular, plural, number, ...arg) { return number > 1 ? plural : singular; }\n"
    ));

    // TODO view.executeCommand() could return an empty string for ok, with message for error (no API breaking)

    // TODO path
    // no new line so that the lines indicated by evaluate correspond to the user code
    constexpr auto jsInjection = u"(function(env){"
        "const TestFramework = this.loadModule('/home/jonathan/kde/src/ktexteditor/src/scripttester/testframework.js');"
        "const DUAL_MODE = TestFramework.DUAL_MODE;"
        "let calleeWrapper = TestFramework.calleeWrapper;"
        "let testFramework = new TestFramework.TestFramework(this, env);"
        "let testCase = testFramework.testCase.bind(testFramework);"

        "let document = calleeWrapper('document', env.document);"
        "let editor = calleeWrapper('editor', env.editor);"
        "let view = calleeWrapper('view', env.view);"
        "\n"
    ;
    QString code = QString::fromUtf8(av[1], strlen(av[1]));
    code = jsInjection % code % QStringLiteral("})");

    // TODO KateScript::backtrace

    QStringList exceptionStackTrace;
    auto writeExceptions = [&]{
        if (exceptionStackTrace.isEmpty()) {
            return;
        }
        testFramework.stream() << "excpt:";
        for (auto &s : exceptionStackTrace) {
            testFramework.stream() << "\n  \x1b[31m" << s << "\x1b[m";
        }
        testFramework.stream() << '\n';
    };

    auto result = engine.evaluate(code, QStringLiteral("myfile.js"), 0, &exceptionStackTrace);
    writeExceptions();
    testFramework.stream() << "result: " << result.toString() << '\n';

    result = result.callWithInstance(functions, {globalObject});
    if (result.isError()) {
        testFramework.stream() << "excpt:\n";
        testFramework.writeException(result, u"| "_sv);
    }
    testFramework.stream() << "result: " << result.toString() << '\n';
    // testFramework.stream() << "result:" << viewObj.executeCommand(QStringLiteral("duplicateLinesDown")).toString();

    // this
    // .setConfig({...})
    // .testCase(name, () => {
    //
    // this
    // .setConfig({...})
    // .withInput(/*label,*/ input, (ctx) => {
    //   ctx
    //   .cmd([func, params...] | jscode, expected, additionalDesc)
    //   ...
    //   .xcmd([func, params...] | jscode, result, espected, additionalDesc)
    //   ...
    //   .eq([func, params...] | jscode, expected, additionalDesc)
    //   .ne | le | lt | ge | gt
    //   ...
    //   .report(msg)
    //   .error(msg)
    // })
    //
    // .sequence(/*label,*/ input, (ctx) => {
    //   // same as withInput, but next input is previous expected value
    // })
    //
    // .label(label)
    // .cmd(...) | xcmd | eq | ne | ....
}
