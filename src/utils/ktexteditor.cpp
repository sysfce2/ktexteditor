/* This file is part of the KDE project
   Copyright (C) 2001 Christoph Cullmann (cullmann@kde.org)

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "kateview.h"

#include "cursor.h"

#include "configpage.h"

#include "editor.h"

#include "document.h"

#include "view.h"

#include "plugin.h"

#include "recoveryinterface.h"
#include "command.h"
#include "markinterface.h"
#include "modificationinterface.h"
#include "searchinterface.h"
#include "sessionconfiginterface.h"
#include "draft/templateinterface.h"
#include "texthintinterface.h"

#include "annotationinterface.h"

#include "kateglobal.h"
#include "kateconfig.h"
#include "katecmd.h"

using namespace KTextEditor;

Editor::Editor(EditorPrivate *impl)
    : QObject ()
    , d (impl)
{
}

Editor::~Editor()
{
}

Editor *KTextEditor::Editor::instance()
{
    /**
     * Just use internal KTextEditor::EditorPrivate::self()
     */
    return KTextEditor::EditorPrivate::self();
}

QString Editor::defaultEncoding () const
{
    /**
     * return default encoding in global config object
     */
    return d->documentConfig()->encoding ();
}

bool View::insertText(const QString &text)
{
    KTextEditor::Document *doc = document();
    if (!doc) {
        return false;
    }
    return doc->insertText(cursorPosition(), text, blockSelection());
}

bool View::isStatusBarEnabled() const
{
    /**
     * is the status bar around?
     */
    return !!d->statusBar();
}

void View::setStatusBarEnabled(bool enable)
{
    /**
     * no state change, do nothing
     */
    if (enable == !!d->statusBar())
        return;

    /**
     * else toggle it
     */
    d->toggleStatusBar ();
}

ConfigPage::ConfigPage(QWidget *parent)
    : QWidget(parent)
    , d(0)
{}

ConfigPage::~ConfigPage()
{}

QString ConfigPage::fullName() const
{
    return name();
}

QIcon ConfigPage::icon() const
{
    return QIcon::fromTheme(QLatin1String("document-properties"));
}

View::View (ViewPrivate *impl, QWidget *parent)
    : QWidget (parent), KXMLGUIClient()
    , d(impl)
{}

View::~View()
{}

Plugin::Plugin(QObject *parent)
    : QObject(parent)
    , d(0)
{}

Plugin::~Plugin()
{}    

int Plugin::configPages() const
{
    return 0;
}

ConfigPage *Plugin::configPage(int, QWidget *)
{
    return nullptr;
}

MarkInterface::MarkInterface()
    : d(0)
{}

MarkInterface::~MarkInterface()
{}

ModificationInterface::ModificationInterface()
    : d(0)
{}

ModificationInterface::~ModificationInterface()
{}

SearchInterface::SearchInterface()
    : d(0)
{}

SearchInterface::~SearchInterface()
{}

SessionConfigInterface::SessionConfigInterface()
    : d(0)
{}

SessionConfigInterface::~SessionConfigInterface()
{}

TemplateInterface::TemplateInterface()
    : d(0)
{}

TemplateInterface::~TemplateInterface()
{}

TextHintInterface::TextHintInterface()
    : d(0)
{}

TextHintInterface::~TextHintInterface()
{}

TextHintProvider::TextHintProvider()
    : d(0)
{}

TextHintProvider::~TextHintProvider()
{}

RecoveryInterface::RecoveryInterface()
    : d(0)
{
}

RecoveryInterface::~RecoveryInterface()
{
}

Command::Command(const QStringList &cmds, QObject *parent)
    : QObject(parent)
    , m_cmds (cmds)
    , d(nullptr)
{
    // register this command
    static_cast<KTextEditor::EditorPrivate *> (KTextEditor::Editor::instance())->cmdManager()->registerCommand (this);
}

Command::~Command()
{
    // unregister this command, if instance is still there!
    if (KTextEditor::Editor::instance())
        static_cast<KTextEditor::EditorPrivate *> (KTextEditor::Editor::instance())->cmdManager()->unregisterCommand (this);
}

KCompletion *Command::completionObject(KTextEditor::View *, const QString &)
{
    return nullptr;
}

bool Command::supportsRange(const QString &)
{
    return false;
}

bool Command::wantsToProcessText(const QString &)
{
    return false;
}

void Command::processText(KTextEditor::View *, const QString &)
{
}
