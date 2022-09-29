/*
    This file is part of the KDE libraries

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "cursorwords_test.h"

#include <kateconfig.h>
#include <katedocument.h>
#include <kateglobal.h>
#include <kateview.h>

#include <QTest>

QTEST_MAIN(CursorWordsTest)

using namespace KTextEditor;

// clang-format off
#define CREATE_DOC_AND_VIEW(text, line, col)                   \
    auto doc = DocumentPrivate();                              \
    doc.config()->setCamelCursor(false);                       \
    auto view = new ViewPrivate(&doc, nullptr);                \
    doc.setText(text);                                         \
    view->setCursorPosition({line, col})
// clang-format on

CursorWordsTest::CursorWordsTest(QObject *parent)
    : QObject(parent)
{
    EditorPrivate::enableUnitTestMode();
}

CursorWordsTest::~CursorWordsTest()
{
}

void CursorWordsTest::testMoveToNextWordSingleLine()
{
    { // single space between words

        CREATE_DOC_AND_VIEW("foo bar quzzi", 0, 0);

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 4));

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 8));

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 13));
    }

    { // cursor inside multiple spaces between words

        CREATE_DOC_AND_VIEW("  -  1234  xyz", 0, 1); // cursro at second space

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 2)); // just before "-"

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 5));

        view->wordRight();

        QCOMPARE(view->cursorPosition(), Cursor(0, 11));
    }
}

void CursorWordsTest::testMoveToPrevWordSingleLine()
{
    { // single space between words

        CREATE_DOC_AND_VIEW("foo bar quzzi", 0, 8); // cursor at the start of "quzzi"

        view->wordLeft();

        QCOMPARE(view->cursorPosition(), Cursor(0, 4));

        view->wordLeft();

        QCOMPARE(view->cursorPosition(), Cursor(0, 0));
    }

    { // cursor inside multiple spaces between words

        CREATE_DOC_AND_VIEW("  12  -  ", 0, 8); // cursor at the last space

        view->wordLeft();

        QCOMPARE(view->cursorPosition(), Cursor(0, 6));

        view->wordLeft();

        QCOMPARE(view->cursorPosition(), Cursor(0, 2));
    }
}

// clang-format off
#define COMPARE_CHAR_AND_CURSOR(view, expectedCursor, expectedCharacter)                  \
    QCOMPARE(view->document()->characterAt(view->cursorPosition()), expectedCharacter);   \
    QCOMPARE(view->cursorPosition(), expectedCursor)
// clang-format on

void CursorWordsTest::testMoveToWordsMultipleLines()
{
    CREATE_DOC_AND_VIEW("hello  there...\n\tno  one answers.", 0, 0);

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(0, 7), 't');

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(0, 12), '.');

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(0, 15), '\0'); // end of line

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 1), 'n');

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 5), 'o');

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 9), 'a');

    view->wordRight();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 16), '.');

    view->wordLeft();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 9), 'a');

    view->wordLeft();
    view->wordLeft();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(1, 1), 'n');

    view->wordLeft();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(0, 15), '\0');

    view->wordLeft();
    COMPARE_CHAR_AND_CURSOR(view, Cursor(0, 12), '.');
}
