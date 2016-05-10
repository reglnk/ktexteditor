/*  This file is part of the KDE libraries and the Kate part.
 *
 *  Copyright (C) 2013 Simon St James <kdedevel@etotheipiplusone.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#ifndef KATEVI_EMULATED_COMMAND_BAR_H
#define KATEVI_EMULATED_COMMAND_BAR_H

#include "kateviewhelpers.h"
#include <vimode/cmds.h>

#include <ktexteditor/range.h>
#include <ktexteditor/attribute.h>
#include <ktexteditor/movingrange.h>
#include "searcher.h"

namespace KTextEditor {
    class ViewPrivate;
    class Command;
}

class QLabel;
class QCompleter;
class QStringListModel;

namespace KateVi
{

/**
 * A KateViewBarWidget that attempts to emulate some of the features of Vim's own command bar,
 * including insertion of register contents via ctr-r<registername>; dismissal via
 * ctrl-c and ctrl-[; bi-directional incremental searching, with SmartCase; interactive sed-replace;
 * plus a few extensions such as completion from document and navigable sed search and sed replace history.
 */
class KTEXTEDITOR_EXPORT EmulatedCommandBar : public KateViewBarWidget
{
    Q_OBJECT

public:
    enum Mode { NoMode, SearchForward, SearchBackward, Command };
    explicit EmulatedCommandBar(InputModeManager *viInputModeManager, QWidget *parent = 0);
    virtual ~EmulatedCommandBar();
    void init(Mode mode, const QString &initialText = QString());
    bool isActive();
    void setCommandResponseMessageTimeout(long commandResponseMessageTimeOutMS);
    bool handleKeyPress(const QKeyEvent *keyEvent);
    bool isSendingSyntheticSearchCompletedKeypress();

    void startInteractiveSearchAndReplace(QSharedPointer<SedReplace::InteractiveSedReplacer> interactiveSedReplace);
    QString executeCommand(const QString &commandToExecute);

    void setViInputModeManager(InputModeManager *viInputModeManager);

private:

    InputModeManager *m_viInputModeManager;
    bool m_isActive = false;
    Mode m_mode = NoMode;
    KTextEditor::ViewPrivate *m_view;
    QLineEdit *m_edit;
    QLabel *m_barTypeIndicator;
    void showBarTypeIndicator(Mode mode);
    KTextEditor::Cursor m_startingCursorPos;
    bool m_wasAborted = true;
    bool m_suspendEditEventFiltering = false;
    bool m_waitingForRegister = false ;
    QLabel *m_waitingForRegisterIndicator;
    bool m_insertedTextShouldBeEscapedForSearchingAsLiteral = false;

    class ActiveMode
    {
    public:
        ActiveMode(EmulatedCommandBar* emulatedCommandBar)
            : m_emulatedCommandBar(emulatedCommandBar)
        {
        }
        virtual ~ActiveMode() = 0;
        virtual bool handleKeyPress(const QKeyEvent *keyEvent) = 0;
    protected:
        EmulatedCommandBar *m_emulatedCommandBar;
    };

    class InteractiveSedReplaceMode : public ActiveMode
    {
    public:
        InteractiveSedReplaceMode(EmulatedCommandBar* emulatedCommandBar)
            : ActiveMode(emulatedCommandBar)
        {
        }
        virtual ~InteractiveSedReplaceMode()
        {
        };
         void activate(QSharedPointer<SedReplace::InteractiveSedReplacer> interactiveSedReplace);
        virtual bool handleKeyPress(const QKeyEvent* keyEvent);
    private:
        void updateInteractiveSedReplaceLabelText();
        void finishInteractiveSedReplace();
        QSharedPointer<SedReplace::InteractiveSedReplacer> m_interactiveSedReplacer;
    };
    friend InteractiveSedReplaceMode; //  TODO - see if we can ultimately remove this.
    QScopedPointer<InteractiveSedReplaceMode> m_interactiveSedReplaceMode;


    QLabel *m_interactiveSedReplaceLabel; // TODO - try to move this into InteractiveSedReplaceMode.
    bool m_interactiveSedReplaceActive; // TODO - try to move this into InteractiveSedReplaceMode.

    void moveCursorTo(const KTextEditor::Cursor &cursorPos);

    QCompleter *m_completer;
    QStringListModel *m_completionModel;
    bool m_isNextTextChangeDueToCompletionChange = false;
    enum CompletionType { None, SearchHistory, WordFromDocument, Commands, CommandHistory, SedFindHistory, SedReplaceHistory };
    CompletionType m_currentCompletionType = None;
    void updateCompletionPrefix();
    void currentCompletionChanged();
    bool m_completionActive;
    QString m_textToRevertToIfCompletionAborted;
    int m_cursorPosToRevertToIfCompletionAborted;

    KTextEditor::Attribute::Ptr m_highlightMatchAttribute;
    KTextEditor::MovingRange *m_highlightedMatch;
    void updateMatchHighlight(const KTextEditor::Range &matchRange);
    enum BarBackgroundStatus { Normal, MatchFound, NoMatchFound };
    void setBarBackground(BarBackgroundStatus status);

    KateVi::Searcher::SearchParams m_currentSearchParams;

    bool m_isSendingSyntheticSearchCompletedKeypress = false;

    bool eventFilter(QObject *object, QEvent *event) Q_DECL_OVERRIDE;
    void deleteSpacesToLeftOfCursor();
    void deleteWordCharsToLeftOfCursor();
    bool deleteNonWordCharsToLeftOfCursor();
    QString wordBeforeCursor();
    QString commandBeforeCursor();
    void replaceWordBeforeCursorWith(const QString &newWord);
    void replaceCommandBeforeCursorWith(const QString &newCommand);

    void activateSearchHistoryCompletion();
    void activateWordFromDocumentCompletion();
    void activateCommandCompletion();
    void activateCommandHistoryCompletion();
    void activateSedFindHistoryCompletion();
    void activateSedReplaceHistoryCompletion();
    void deactivateCompletion();
    void abortCompletionAndResetToPreCompletion();
    void setCompletionIndex(int index);

    /**
     * Stuff to do with expressions of the form:
     *
     *   s/find/replace/<sedflags>
     */
    struct ParsedSedExpression {
        bool parsedSuccessfully;
        int findBeginPos;
        int findEndPos;
        int replaceBeginPos;
        int replaceEndPos;
        QChar delimiter;
    };
    ParsedSedExpression parseAsSedExpression();
    QString withSedFindTermReplacedWith(const QString &newFindTerm);
    QString withSedReplaceTermReplacedWith(const QString &newReplaceTerm);
    QString sedFindTerm();
    QString sedReplaceTerm();
    QString withSedDelimiterEscaped(const QString &text);

    bool isCursorInFindTermOfSed();
    bool isCursorInReplaceTermOfSed();

    /**
     * The "range expression" is the (optional) expression before the command that describes
     * the range over which the command should be run e.g. '<,'>.  @see CommandRangeExpressionParser
     */
    QString withoutRangeExpression();
    QString rangeExpression();

    void closed() Q_DECL_OVERRIDE;
    void closeWithStatusMessage(const QString& exitStatusMessage);
    QTimer *m_exitStatusMessageDisplayHideTimer;
    QLabel *m_exitStatusMessageDisplay;
    long m_exitStatusMessageHideTimeOutMS = 4000;
private:
    KCompletion m_cmdCompletion;
    QHash<QString, KTextEditor::Command *> m_cmdDict;
    KTextEditor::Command *queryCommand(const QString &cmd) const;

private Q_SLOTS:
    void editTextChanged(const QString &newText);
    void updateMatchHighlightAttrib();
    void startHideExitStatusMessageTimer();
};

}

#endif /* KATEVI_EMULATED_COMMAND_BAR_H */
