/* This file is part of the KDE libraries
   Copyright (C) 2007 Mirko Stocker <me@misto.ch>
   Copyright (C) 2007 Matthew Woehlke <mw_triad@users.sourceforge.net>
   Copyright (C) 2003, 2004 Anders Lund <anders@alweb.dk>
   Copyright (C) 2003 Hamish Rodda <rodda@kde.org>
   Copyright (C) 2001,2002 Joseph Wenninger <jowenn@kde.org>
   Copyright (C) 2001 Christoph Cullmann <cullmann@kde.org>
   Copyright (C) 1999 Jochen Wilhelmy <digisnap@cs.tu-berlin.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

//BEGIN INCLUDES
#include "katehighlight.h"

#include "katehighlighthelpers.h"
#include "katetextline.h"
#include "katedocument.h"
#include "katerenderer.h"
#include "kateglobal.h"
#include "kateschema.h"
#include "kateconfig.h"
#include "kateextendedattribute.h"
#include "katepartdebug.h"
#include "katedefaultcolors.h"

#include <KConfig>
#include <KConfigGroup>
#include <KMessageBox>

#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVarLengthArray>
#include <QAction>
#include <QApplication>
//END

//BEGIN STATICS
namespace {
inline const QString stdDeliminator()
{
    return QStringLiteral(" \t.():!+,-<=>%&*/;?[]^{|}~\\");
}

// @return true if x is "true" or "1"
bool isTrue(const QString& x)
{
    return (x.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) || x.toInt() == 1;
}
}
//END

//BEGIN KateHighlighting
KateHighlighting::KateHighlighting(const KSyntaxHighlighting::Definition &def)
    : refCount(0)
    , startctx(0)
    , base_startctx(0)
{
    errorsAndWarnings = QString();
    building = false;
    noHl = false;
    m_foldingIndentationSensitive = false;
    folding = false;

    if (!def.isValid()) {
        noHl = true;
        iName = QStringLiteral("None"); // not translated internal name (for config and more)
        iNameTranslated = i18nc("Syntax highlighting", "None"); // user visible name
        iSection = QString();
    } else {
        iName = def.name();
        iNameTranslated = def.translatedName();
        iSection = def.translatedSection();
        iHidden = def.isHidden();
        identifier = def.filePath();
        iVersion = QString::number(def.version());
        iStyle = def.style();
        iAuthor = def.author();
        iLicense = def.license();
    }

    deliminator = stdDeliminator();

    /**
     * tell the AbstractHighlighter the definition it shall use
     */
    setDefinition(def);

    /**
     * only create mapping and stuff if the definition is valid
     */
    if (def.isValid()) {
        /**
         * create the format => attributes mapping
         * collect embedded highlightings, too
         */
        for (const auto & includedDefinition : definition().includedDefinitions()) {
            // embeddedHighlightingModes should not contain the base highlighting
            if (includedDefinition.name() != iName)
                embeddedHighlightingModes.push_back(includedDefinition.name());

            // collect formats
            for (const auto & format : includedDefinition.formats()) {
                if (m_formatsIdToIndex.insert(std::make_pair(format.id(), m_formats.size())).second) {
                    m_formats.push_back(format);
                }
            }
        }
    }
}

KateHighlighting::~KateHighlighting()
{
    // cleanup ;)
    cleanup();

    qDeleteAll(m_additionalData);
}

void KateHighlighting::cleanup()
{
    qDeleteAll(m_contexts);
    m_contexts.clear();

    qDeleteAll(m_hlItemCleanupList);
    m_hlItemCleanupList.clear();

    m_attributeArrays.clear();

    internalIDList.clear();
}

void KateHighlighting::doHighlight(const Kate::TextLineData *_prevLine,
                                   Kate::TextLineData *textLine,
                                   const Kate::TextLineData *nextLine,
                                   bool &ctxChanged,
                                   int tabWidth,
                                   QVector<ContextChange>* contextChanges)
{
    if (!textLine) {
        return;
    }

    // in all cases, remove old hl, or we will grow to infinite ;)
    textLine->clearAttributes();

    // reset folding start
    textLine->clearMarkedAsFoldingStart();

    // no hl set, nothing to do more than the above cleaning ;)
    if (noHl) {
        return;
    }

    /**
     * highlight the given line via the abstract highlighter
     * a bit ugly: we set the line to highlight as member to be able to update its stats in the applyFormat and applyFolding member functions
     */
    m_textLineToHighlight = textLine;
    const KSyntaxHighlighting::State initialState (!_prevLine ? KSyntaxHighlighting::State() : _prevLine->highlightingState());
    const KSyntaxHighlighting::State endOfLineState = highlightLine(textLine->string(), initialState);
    textLine->setHighlightingState(endOfLineState);
    m_textLineToHighlight = nullptr;
}

void KateHighlighting::applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format)
{
    Q_ASSERT(m_textLineToHighlight);
    m_textLineToHighlight->addAttribute(Kate::TextLineData::Attribute(offset, length, m_formatsIdToIndex[format.id()], 0));
}

void KateHighlighting::applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region)
{
    Q_ASSERT(m_textLineToHighlight);
}

void KateHighlighting::getKateExtendedAttributeList(const QString &schema, QList<KTextEditor::Attribute::Ptr> &list, KConfig *cfg)
{
    KConfigGroup config(cfg ? cfg : KateHlManager::self()->getKConfig(),
                        QLatin1String("Highlighting ") + iName + QLatin1String(" - Schema ") + schema);

    list.clear();
    createKateExtendedAttribute(list);

    foreach (KTextEditor::Attribute::Ptr p, list) {
        Q_ASSERT(p);

        QStringList s = config.readEntry(p->name(), QStringList());

//    qCDebug(LOG_KTE)<<p->name<<s.count();
        if (s.count() > 0) {

            while (s.count() < 10) {
                s << QString();
            }
            QString name = p->name();
            bool spellCheck = !p->skipSpellChecking();
            p->clear();
            p->setName(name);
            p->setSkipSpellChecking(!spellCheck);

            QString tmp = s[0]; if (!tmp.isEmpty()) {
                p->setDefaultStyle(static_cast<KTextEditor::DefaultStyle> (tmp.toInt()));
            }

            QRgb col;

            tmp = s[1]; if (!tmp.isEmpty()) {
                col = tmp.toUInt(nullptr, 16); p->setForeground(QColor(col));
            }

            tmp = s[2]; if (!tmp.isEmpty()) {
                col = tmp.toUInt(nullptr, 16); p->setSelectedForeground(QColor(col));
            }

            tmp = s[3]; if (!tmp.isEmpty()) {
                p->setFontBold(tmp != QLatin1String("0"));
            }

            tmp = s[4]; if (!tmp.isEmpty()) {
                p->setFontItalic(tmp != QLatin1String("0"));
            }

            tmp = s[5]; if (!tmp.isEmpty()) {
                p->setFontStrikeOut(tmp != QLatin1String("0"));
            }

            tmp = s[6]; if (!tmp.isEmpty()) {
                p->setFontUnderline(tmp != QLatin1String("0"));
            }

            tmp = s[7]; if (!tmp.isEmpty()) {
                col = tmp.toUInt(nullptr, 16); p->setBackground(QColor(col));
            }

            tmp = s[8]; if (!tmp.isEmpty()) {
                col = tmp.toUInt(nullptr, 16); p->setSelectedBackground(QColor(col));
            }

            tmp = s[9]; if (!tmp.isEmpty() && tmp != QLatin1String("---")) {
                p->setFontFamily(tmp);
            }

        }
    }
}

void KateHighlighting::getKateExtendedAttributeListCopy(const QString &schema, QList< KTextEditor::Attribute::Ptr > &list, KConfig *cfg)
{
    QList<KTextEditor::Attribute::Ptr> attributes;
    getKateExtendedAttributeList(schema, attributes, cfg);

    list.clear();

    foreach (const KTextEditor::Attribute::Ptr &attribute, attributes) {
        list.append(KTextEditor::Attribute::Ptr(new KTextEditor::Attribute(*attribute.data())));
    }
}

void KateHighlighting::setKateExtendedAttributeList(const QString &schema, QList<KTextEditor::Attribute::Ptr> &list, KConfig *cfg, bool writeDefaultsToo)
{
    KConfigGroup config(cfg ? cfg : KateHlManager::self()->getKConfig(),
                        QLatin1String("Highlighting ") + iName + QLatin1String(" - Schema ") + schema);

    QStringList settings;

    KateAttributeList defList;
    KateHlManager::self()->getDefaults(schema, defList);

    foreach (const KTextEditor::Attribute::Ptr &p, list) {
        Q_ASSERT(p);

        settings.clear();
        KTextEditor::DefaultStyle defStyle = p->defaultStyle();
        KTextEditor::Attribute::Ptr a(defList[defStyle]);
        settings << QString::number(p->defaultStyle(), 10);
        settings << (p->hasProperty(QTextFormat::ForegroundBrush) ? QString::number(p->foreground().color().rgb(), 16) : (writeDefaultsToo ? QString::number(a->foreground().color().rgb(), 16) : QString()));
        settings << (p->hasProperty(SelectedForeground) ? QString::number(p->selectedForeground().color().rgb(), 16) : (writeDefaultsToo ? QString::number(a->selectedForeground().color().rgb(), 16) : QString()));
        settings << (p->hasProperty(QTextFormat::FontWeight) ? (p->fontBold() ? QStringLiteral("1") : QStringLiteral("0")) : (writeDefaultsToo ? (a->fontBold() ? QStringLiteral("1") : QStringLiteral("0")) : QString()));
        settings << (p->hasProperty(QTextFormat::FontItalic) ? (p->fontItalic() ? QStringLiteral("1") : QStringLiteral("0")) : (writeDefaultsToo ? (a->fontItalic() ? QStringLiteral("1") : QStringLiteral("0")) : QString()));
        settings << (p->hasProperty(QTextFormat::FontStrikeOut) ? (p->fontStrikeOut() ? QStringLiteral("1") : QStringLiteral("0")) : (writeDefaultsToo ? (a->fontStrikeOut() ? QStringLiteral("1") : QStringLiteral("0")) : QString()));
        settings << (p->hasProperty(QTextFormat::FontUnderline) ? (p->fontUnderline() ? QStringLiteral("1") : QStringLiteral("0")) : (writeDefaultsToo ? (a->fontUnderline() ? QStringLiteral("1") : QStringLiteral("0")) : QString()));
        settings << (p->hasProperty(QTextFormat::BackgroundBrush) ? QString::number(p->background().color().rgb(), 16) : ((writeDefaultsToo && a->hasProperty(QTextFormat::BackgroundBrush)) ? QString::number(a->background().color().rgb(), 16) : QString()));
        settings << (p->hasProperty(SelectedBackground) ? QString::number(p->selectedBackground().color().rgb(), 16) : ((writeDefaultsToo && a->hasProperty(SelectedBackground)) ? QString::number(a->selectedBackground().color().rgb(), 16) : QString()));
        settings << (p->hasProperty(QTextFormat::FontFamily) ? (p->fontFamily()) : (writeDefaultsToo ? a->fontFamily() : QString()));
        settings << QStringLiteral("---");
        config.writeEntry(p->name(), settings);
    }
}

const QHash<QString, QChar> &KateHighlighting::getCharacterEncodings(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->characterEncodings;
}

const KatePrefixStore &KateHighlighting::getCharacterEncodingsPrefixStore(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->characterEncodingsPrefixStore;
}

const QHash<QChar, QString> &KateHighlighting::getReverseCharacterEncodings(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->reverseCharacterEncodings;
}

int KateHighlighting::getEncodedCharactersInsertionPolicy(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->encodedCharactersInsertionPolicy;
}

void KateHighlighting::addCharacterEncoding(const QString &key, const QString &encoding, const QChar &c)
{
    m_additionalData[ key ]->characterEncodingsPrefixStore.addPrefix(encoding);
    m_additionalData[ key ]->characterEncodings[ encoding ] = c;
    m_additionalData[ key ]->reverseCharacterEncodings[ c ] = encoding;
}

/**
 * Increase the usage count, and trigger initialization if needed.
 */
void KateHighlighting::use()
{
    if (refCount == 0) {
        init();
    }

    refCount = 1;
}

/**
 * Reload the highlighting.
 */

void KateHighlighting::reload()
{
    // nop if not referenced
    if (refCount == 0) {
        return;
    }

    cleanup();
    init();
}

/**
 * Initialize a context for the first time.
 */

void KateHighlighting::init()
{
    // shall be only called if clean!
    Q_ASSERT(m_contexts.empty());

    // fixup internal id list, if empty
    if (internalIDList.isEmpty()) {
        internalIDList.append(KTextEditor::Attribute::Ptr(new KTextEditor::Attribute(i18n("Normal Text"), KTextEditor::dsNormal)));
    }

    // something went wrong or no hl, fill something in
    // FIXME: we do this always ATM
    iHidden = false;
    m_additionalData.insert(QStringLiteral("none"), new HighlightPropertyBag);
    m_additionalData[QStringLiteral("none")]->deliminator = stdDeliminator();
    m_additionalData[QStringLiteral("none")]->wordWrapDeliminator = stdDeliminator();
    m_hlIndex[0] = QStringLiteral("none");
    m_ctxIndex[0] = QStringLiteral("none");

    // create one dummy context!
    m_contexts.push_back(new KateHlContext(identifier, 0,
        KateHlContextModification(), false, KateHlContextModification(),
        false, false, false, KateHlContextModification()));
}

/**
 * KateHighlighting - createKateExtendedAttribute
 * This function reads the itemData entries from the config file, which specifies the
 * default attribute styles for matched items/contexts.
 *
 * @param list A reference to the internal list containing the parsed default config
 */
void KateHighlighting::createKateExtendedAttribute(QList<KTextEditor::Attribute::Ptr> &list)
{
    // trigger hl load, if needed
    use();

    // return internal list, never empty!
    Q_ASSERT(!internalIDList.empty());
    list = internalIDList;
}

/**
 * KateHighlighting - lookupAttrName
 * This function is  a helper for makeContextList and createKateHlItem. It looks the given
 * attribute name in the itemData list up and returns its index
 *
 * @param name the attribute name to lookup
 * @param iDl the list containing all available attributes
 *
 * @return The index of the attribute, or 0 if the attribute isn't found
 */
int  KateHighlighting::lookupAttrName(const QString &name, QList<KTextEditor::Attribute::Ptr> &iDl)
{
    const QString needle = buildPrefix + name;
    for (int i = 0; i < iDl.count(); i++)
        if (iDl.at(i)->name() == needle) {
            return i;
        }

#ifdef HIGHLIGHTING_DEBUG
    qCDebug(LOG_KTE) << "Couldn't resolve itemDataName:" << name;
#endif

    return 0;
}

int KateHighlighting::attribute(int ctx) const
{
    return m_contexts[ctx]->attr;
}

bool KateHighlighting::attributeRequiresSpellchecking(int attr)
{
    if (attr >= 0 && attr < internalIDList.size() && internalIDList[attr]->hasProperty(Spellchecking)) {
        return !internalIDList[attr]->boolProperty(Spellchecking);
    }
    return true;
}

KTextEditor::DefaultStyle KateHighlighting::defaultStyleForAttribute(int attr) const
{
    if (attr >= 0 && attr < internalIDList.size()) {
        return internalIDList[attr]->defaultStyle();
    }
    return KTextEditor::dsNormal;
}

QString KateHighlighting::hlKeyForContext(int i) const
{
    // FIXME
    return QStringLiteral("none");
}

QString KateHighlighting::hlKeyForAttrib(int i) const
{
    // FIXME
    return QStringLiteral("none");
}

bool KateHighlighting::isInWord(QChar c, int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->deliminator.indexOf(c) < 0
           && !c.isSpace()
           && c != QLatin1Char('"') && c != QLatin1Char('\'') && c != QLatin1Char('`');
}

bool KateHighlighting::canBreakAt(QChar c, int attrib) const
{
    static const QString &sq = QStringLiteral("\"'");
    return (m_additionalData[ hlKeyForAttrib(attrib) ]->wordWrapDeliminator.indexOf(c) != -1) && (sq.indexOf(c) == -1);
}

QLinkedList<QRegularExpression> KateHighlighting::emptyLines(int attrib) const
{
#ifdef HIGHLIGHTING_DEBUG
    qCDebug(LOG_KTE) << "hlKeyForAttrib: " << hlKeyForAttrib(attrib);
#endif

    return m_additionalData[hlKeyForAttrib(attrib)]->emptyLines;
}

signed char KateHighlighting::commentRegion(int attr) const
{
    QString commentRegion = m_additionalData[ hlKeyForAttrib(attr) ]->multiLineRegion;
    return (commentRegion.isEmpty() ? 0 : (commentRegion.toShort()));
}

bool KateHighlighting::canComment(int startAttrib, int endAttrib) const
{
    QString k = hlKeyForAttrib(startAttrib);
    return (k == hlKeyForAttrib(endAttrib) &&
            ((!m_additionalData[k]->multiLineCommentStart.isEmpty() && !m_additionalData[k]->multiLineCommentEnd.isEmpty()) ||
             ! m_additionalData[k]->singleLineCommentMarker.isEmpty()));
}

QString KateHighlighting::getCommentStart(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->multiLineCommentStart;
}

QString KateHighlighting::getCommentEnd(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->multiLineCommentEnd;
}

QString KateHighlighting::getCommentSingleLineStart(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->singleLineCommentMarker;
}

KateHighlighting::CSLPos KateHighlighting::getCommentSingleLinePosition(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->singleLineCommentPosition;
}

const QHash<QString, QChar> &KateHighlighting::characterEncodings(int attrib) const
{
    return m_additionalData[ hlKeyForAttrib(attrib) ]->characterEncodings;
}

void KateHighlighting::clearAttributeArrays()
{
    // just clear the hashed attributes, we create them lazy again
    m_attributeArrays.clear();
}

QList<KTextEditor::Attribute::Ptr> KateHighlighting::attributes(const QString &schema)
{
    // found it, already floating around
    if (m_attributeArrays.contains(schema)) {
        return m_attributeArrays[schema];
    }

    /**
     * create list of all known things
     */
    QList<KTextEditor::Attribute::Ptr> array;
    if (m_formats.empty()) {
        KTextEditor::Attribute::Ptr newAttribute(new KTextEditor::Attribute(QLatin1String("Normal"), KTextEditor::dsNormal));
        array.append(newAttribute);
    } else {
        for (const auto &format : m_formats) {
            /**
             * FIXME: atm we just set some theme here for later color generation
             */
            setTheme(KateHlManager::self()->repository().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

            /**
             * convert from KSyntaxHighlighting => KTextEditor type
             * special handle non-1:1 things
             */
            KTextEditor::DefaultStyle defaultStyle = static_cast<KTextEditor::DefaultStyle>(format.textStyle());
            if (format.textStyle() == KSyntaxHighlighting::Theme::Error)
                defaultStyle = KTextEditor::dsError;
            else if (format.textStyle() == KSyntaxHighlighting::Theme::Others)
                defaultStyle = KTextEditor::dsOthers;

            KTextEditor::Attribute::Ptr newAttribute(new KTextEditor::Attribute(format.name(), defaultStyle));

            if (format.hasTextColor(theme()))
                newAttribute->setForeground(format.textColor(theme()));
            if (format.hasBackgroundColor(theme()))
                newAttribute->setBackground(format.backgroundColor(theme()));

            if (format.isBold(theme()))
                newAttribute->setFontWeight(QFont::Bold);
            if (format.isItalic(theme()))
                newAttribute->setFontItalic(true);
            if (format.isUnderline(theme()))
                newAttribute->setFontUnderline(true);
            if (format.isStrikeThrough(theme()))
                newAttribute->setFontStrikeOut(true);

            array.append(newAttribute);
        }
    }
    m_attributeArrays.insert(schema, array);
    return array;
}

KateHlContext *KateHighlighting::contextNum(int n) const
{
    if (n >= 0 && n < m_contexts.size()) {
        return m_contexts[n];
    }

    Q_ASSERT(false);
    return nullptr;
}

QStringList KateHighlighting::getEmbeddedHighlightingModes() const
{
    return embeddedHighlightingModes;
}

bool KateHighlighting::isEmptyLine(const Kate::TextLineData *textline) const
{
    const QString &txt = textline->string();
    if (txt.isEmpty()) {
        return true;
    }

    const QLinkedList<QRegularExpression> l = emptyLines(textline->attribute(0));
    if (l.isEmpty()) {
        return false;
    }

    foreach (const QRegularExpression &re, l) {
        const QRegularExpressionMatch match = re.match (txt, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
        if (match.hasMatch() && match.capturedLength() == txt.length()) {
            return true;
        }
    }

    return false;
}

KateHlContext* KateHighlighting::contextForLocation(KTextEditor::DocumentPrivate* doc, const KTextEditor::Cursor& cursor)
{
    if ( noHighlighting() ) {
        // no highlighting -- nothing to do
        return nullptr;
    }

    Kate::TextLine line = doc->kateTextLine(cursor.line());
    Kate::TextLine previousLine = doc->kateTextLine(cursor.line() - 1);
    Kate::TextLine nextLine = doc->kateTextLine(cursor.line() + 1);
    bool contextChanged;
    QVector<KateHighlighting::ContextChange> contextChanges;
    // Ask the highlighting engine to re-calcualte the highlighting for the line
    // where completion was invoked, and store all the context changes in the process.
    doHighlight(previousLine.data(), line.data(), nextLine.data(),
                contextChanged, 0, &contextChanges);

    // From the list of context changes, find the highlighting context which is
    // active at the position where completion was invoked.
    KateHlContext* context = nullptr;
    foreach ( KateHighlighting::ContextChange change, contextChanges ) {
        if ( change.pos == 0 || change.pos <= cursor.column() ) {
            context = change.toContext;
        }
        if ( change.pos > cursor.column() ) {
            break;
        }
    }
    return context;
}

//END

