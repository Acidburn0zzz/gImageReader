/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * OutputEditorHOCR.cc
 * Copyright (C) 2013-2015 Sandro Mani <manisandro@gmail.com>
 *
 * gImageReader is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gImageReader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QFileDialog>
#include <QSyntaxHighlighter>
#include <tesseract/baseapi.h>

#include "MainWindow.hh"
#include "OutputEditorHOCR.hh"
#include "Recognizer.hh"
#include "SourceManager.hh"
#include "Utils.hh"


static const int g_noSpellProp = QTextFormat::UserProperty + 1;


class OutputEditorHOCR::HTMLHighlighter : public QSyntaxHighlighter
{
public:
	HTMLHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
	{
		mFormatMap[NormalState].setForeground(QColor(Qt::black));
		mFormatMap[InTag].setForeground(QColor(75, 75, 255));
		mFormatMap[InTag].setProperty(g_noSpellProp, 1);
		mFormatMap[InAttrKey].setForeground(QColor(75, 200, 75));
		mFormatMap[InAttrKey].setProperty(g_noSpellProp, 1);
		mFormatMap[InAttrValue].setForeground(QColor(255, 75, 75));
		mFormatMap[InAttrValue].setProperty(g_noSpellProp, 1);
		mFormatMap[InAttrValueDblQuote].setForeground(QColor(255, 75, 75));
		mFormatMap[InAttrValueDblQuote].setProperty(g_noSpellProp, 1);

		mStateMap[NormalState].append({QRegExp("<"), InTag, false});
		mStateMap[InTag].append({QRegExp(">"), NormalState, true});
		mStateMap[InTag].append({QRegExp("\\w+="), InAttrKey, false});
		mStateMap[InAttrKey].append({QRegExp("'"), InAttrValue, false});
		mStateMap[InAttrKey].append({QRegExp("\""), InAttrValueDblQuote, false});
		mStateMap[InAttrKey].append({QRegExp("\\s"), NormalState, false});
		mStateMap[InAttrValue].append({QRegExp("'[^']*'"), InTag, true});
		mStateMap[InAttrValueDblQuote].append({QRegExp("\"[^\"]*\""), InTag, true});
	}

private:
	enum State { NormalState = -1, InComment, InTag, InAttrKey, InAttrValue, InAttrValueDblQuote };
	struct Rule {
		QRegExp pattern;
		State nextState;
		bool addMatched; // add matched length to pos
	};

	QMap<State,QTextCharFormat> mFormatMap;
	QMap<State,QList<Rule>> mStateMap;

	void highlightBlock(const QString &text)
	{
		int pos = 0;
		int len = text.length();
		State state = static_cast<State>(previousBlockState());
		while(pos < len) {
			State minState = state;
			int minPos = -1;
			for(const Rule& rule : mStateMap.value(state)) {
				int matchPos = rule.pattern.indexIn(text, pos);
				if(matchPos != -1 && (minPos < 0 || matchPos < minPos)) {
					minPos = matchPos + (rule.addMatched ? rule.pattern.matchedLength() : 0);
					minState = rule.nextState;
				}
			}
			if(minPos == -1) {
				setFormat(pos, len - pos, mFormatMap[state]);
				pos = len;
			} else {
				setFormat(pos, minPos - pos, mFormatMap[state]);
				pos = minPos;
				state = minState;
			}
		}

		setCurrentBlockState(state);
	}
};

OutputEditorHOCR::OutputEditorHOCR()
{
	m_widget = new QWidget;
	ui.setupUi(m_widget);
	m_highlighter = new HTMLHighlighter(ui.plainTextEditOutput->document());
	ui.frameOutputSearch->setVisible(false);

	m_spell.setDecodeLanguageCodes(true);
	m_spell.setShowCheckSpellingCheckbox(true);
	m_spell.setTextEdit(ui.plainTextEditOutput);
	m_spell.setUndoRedoEnabled(true);
	m_spell.setNoSpellingPropertyId(g_noSpellProp);

	ui.actionOutputReplace->setShortcut(Qt::CTRL + Qt::Key_F);
	ui.actionOutputSave->setShortcut(Qt::CTRL + Qt::Key_S);

	connect(ui.actionSelectBox, SIGNAL(triggered(bool)), this, SLOT(selectCurrentBox()));
	connect(ui.actionOutputReplace, SIGNAL(toggled(bool)), ui.frameOutputSearch, SLOT(setVisible(bool)));
	connect(ui.actionOutputReplace, SIGNAL(toggled(bool)), ui.lineEditOutputSearch, SLOT(clear()));
	connect(ui.actionOutputReplace, SIGNAL(toggled(bool)), ui.lineEditOutputReplace, SLOT(clear()));
	connect(ui.actionOutputUndo, SIGNAL(triggered()), &m_spell, SLOT(undo()));
	connect(ui.actionOutputRedo, SIGNAL(triggered()), &m_spell, SLOT(redo()));
	connect(ui.actionOutputSave, SIGNAL(triggered()), this, SLOT(save()));
	connect(ui.actionOutputClear, SIGNAL(triggered()), this, SLOT(clear()));
	connect(&m_spell, SIGNAL(undoAvailable(bool)), ui.actionOutputUndo, SLOT(setEnabled(bool)));
	connect(&m_spell, SIGNAL(redoAvailable(bool)), ui.actionOutputRedo, SLOT(setEnabled(bool)));
	connect(ui.checkBoxOutputSearchMatchCase, SIGNAL(toggled(bool)), this, SLOT(clearErrorState()));
	connect(ui.lineEditOutputSearch, SIGNAL(textChanged(QString)), this, SLOT(clearErrorState()));
	connect(ui.lineEditOutputSearch, SIGNAL(returnPressed()), this, SLOT(findNext()));
	connect(ui.lineEditOutputReplace, SIGNAL(returnPressed()), this, SLOT(replaceNext()));
	connect(ui.toolButtonOutputFindNext, SIGNAL(clicked()), this, SLOT(findNext()));
	connect(ui.toolButtonOutputFindPrev, SIGNAL(clicked()), this, SLOT(findPrev()));
	connect(ui.toolButtonOutputReplace, SIGNAL(clicked()), this, SLOT(replaceNext()));
	connect(ui.toolButtonOutputReplaceAll, SIGNAL(clicked()), this, SLOT(replaceAll()));
	connect(MAIN->getConfig()->getSetting<FontSetting>("customoutputfont"), SIGNAL(changed()), this, SLOT(setFont()));
	connect(MAIN->getConfig()->getSetting<SwitchSetting>("systemoutputfont"), SIGNAL(changed()), this, SLOT(setFont()));

	MAIN->getConfig()->addSetting(new SwitchSetting("searchmatchcase", ui.checkBoxOutputSearchMatchCase));
	MAIN->getConfig()->addSetting(new VarSetting<QString>("outputdir", Utils::documentsFolder()));

	setFont();
}

OutputEditorHOCR::~OutputEditorHOCR()
{
	delete m_widget;
}

void OutputEditorHOCR::clearErrorState()
{
	ui.lineEditOutputSearch->setStyleSheet("");
}

void OutputEditorHOCR::setFont()
{
	if(MAIN->getConfig()->getSetting<SwitchSetting>("systemoutputfont")->getValue()){
		ui.plainTextEditOutput->setFont(QFont());
	}else{
		ui.plainTextEditOutput->setFont(MAIN->getConfig()->getSetting<FontSetting>("customoutputfont")->getValue());
	}
}

void OutputEditorHOCR::findNext()
{
	findReplace(false, false);
}

void OutputEditorHOCR::findPrev()
{
	findReplace(true, false);
}

void OutputEditorHOCR::replaceNext()
{
	findReplace(false, true);
}

void OutputEditorHOCR::replaceAll()
{
	MAIN->pushState(MainWindow::State::Busy, _("Replacing..."));
	QString searchstr = ui.lineEditOutputSearch->text();
	QString replacestr = ui.lineEditOutputReplace->text();
	if(!ui.plainTextEditOutput->replaceAll(searchstr, replacestr, ui.checkBoxOutputSearchMatchCase->isChecked())){
		ui.lineEditOutputSearch->setStyleSheet("background: #FF7777; color: #FFFFFF;");
	}
	MAIN->popState();
}

void OutputEditorHOCR::findReplace(bool backwards, bool replace)
{
	clearErrorState();
	QString searchstr = ui.lineEditOutputSearch->text();
	QString replacestr = ui.lineEditOutputReplace->text();
	if(!ui.plainTextEditOutput->findReplace(backwards, replace, ui.checkBoxOutputSearchMatchCase->isChecked(), searchstr, replacestr)){
		ui.lineEditOutputSearch->setStyleSheet("background: #FF7777; color: #FFFFFF;");
	}
}

void OutputEditorHOCR::read(tesseract::TessBaseAPI &tess, ReadSessionData *data)
{
	char* text = tess.GetHOCRText(data->page);
	QMetaObject::invokeMethod(this, "addText", Qt::QueuedConnection, Q_ARG(QString, QString::fromUtf8(text)), Q_ARG(ReadSessionData, *data));
	delete[] text;
}

void OutputEditorHOCR::readError(const QString &errorMsg, ReadSessionData */*data*/)
{
	QMetaObject::invokeMethod(this, "addText", Qt::QueuedConnection, Q_ARG(QString, errorMsg));
}

void OutputEditorHOCR::adjustBBox(QDomElement element, const QRectF& rect)
{
	static QRegExp bboxRx("bbox\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)");
	static QRegExp idRx("([A-Za-z]+)_\\d+_(\\d+)");
	while(!element.isNull()) {
		if(bboxRx.indexIn(element.attribute("title")) != -1) {
			int x1 = bboxRx.cap(1).toInt() + rect.x();
			int y1 = bboxRx.cap(2).toInt() + rect.y();
			int x2 = bboxRx.cap(3).toInt() + rect.x();
			int y2 = bboxRx.cap(4).toInt() + rect.y();
			QString newBBox = QString("bbox %1 %2 %3 %4").arg(x1).arg(y1).arg(x2).arg(y2);
			element.setAttribute("title", element.attribute("title").replace(bboxRx, newBBox));
		}
		if(idRx.indexIn(element.attribute("id")) != -1) {
			QString newId = QString("%1_%2_%3").arg(idRx.cap(1)).arg(m_pageCounter).arg(idRx.cap(2));
			element.setAttribute("id", newId);
		}
		adjustBBox(element.firstChildElement(), rect);
		element = element.nextSiblingElement();
	}
}

void OutputEditorHOCR::addText(const QString& text, ReadSessionData data)
{
	QDomDocument doc;
	doc.setContent(text);
	QDomElement pageDiv = doc.firstChildElement("div");
	QString pageTitle = QString("image '%1'; bbox %2 %3 %4 %5; pageno %6; rot %7")
			.arg(data.file)
			.arg(data.rect.left()).arg(data.rect.top()).arg(data.rect.right()).arg(data.rect.bottom())
			.arg(data.page)
			.arg(data.angle);
	pageDiv.setAttribute("title", pageTitle);
	pageDiv.setAttribute("id", QString("page_%1").arg(++m_pageCounter));
	adjustBBox(pageDiv.firstChildElement("div"), data.rect);

	QTextCursor cursor = ui.plainTextEditOutput->textCursor();
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(doc.toString());
	ui.plainTextEditOutput->setTextCursor(cursor);
	MAIN->setOutputPaneVisible(true);
}

bool OutputEditorHOCR::save(const QString& filename)
{
	QString outname = filename;
	if(outname.isEmpty()){
		QList<Source*> sources = MAIN->getSourceManager()->getSelectedSources();
		QString base = !sources.isEmpty() ? QFileInfo(sources.first()->displayname).baseName() : _("output");
		outname = QDir(MAIN->getConfig()->getSetting<VarSetting<QString>>("outputdir")->getValue()).absoluteFilePath(base + ".txt");
		outname = QFileDialog::getSaveFileName(MAIN, _("Save Output..."), outname, QString("%1 (*.txt)").arg(_("Text Files")));
		if(outname.isEmpty()){
			return false;
		}
		MAIN->getConfig()->getSetting<VarSetting<QString>>("outputdir")->setValue(QFileInfo(outname).absolutePath());
	}
	QFile file(outname);
	if(!file.open(QIODevice::WriteOnly)){
		QMessageBox::critical(MAIN, _("Failed to save output"), _("Check that you have writing permissions in the selected folder."));
		return false;
	}
	file.write(ui.plainTextEditOutput->toPlainText().toLocal8Bit());
	ui.plainTextEditOutput->document()->setModified(false);
	return true;
}

bool OutputEditorHOCR::clear()
{
	if(!m_widget->isVisible()){
		return true;
	}
	if(ui.plainTextEditOutput->document()->isModified()){
		int response = QMessageBox::question(MAIN, _("Output not saved"), _("Save output before proceeding?"), QMessageBox::Save, QMessageBox::Discard, QMessageBox::Cancel);
		if(response == QMessageBox::Save){
			if(!save()){
				return false;
			}
		}else if(response != QMessageBox::Discard){
			return false;
		}
	}
	ui.plainTextEditOutput->clear();
	m_spell.clearUndoRedo();
	ui.plainTextEditOutput->document()->setModified(false);
	MAIN->setOutputPaneVisible(false);
	return true;
}

bool OutputEditorHOCR::getModified() const
{
	return ui.plainTextEditOutput->document()->isModified();
}

void OutputEditorHOCR::setLanguage(const Config::Lang& lang, bool force)
{
	QString code = lang.code;
	if(!code.isEmpty() || force){
		m_spell.setLanguage(code);
	}
}

void OutputEditorHOCR::selectCurrentBox()
{
	QTextCursor cursor = ui.plainTextEditOutput->textCursor();
	// Move to beginning of parent ocr tag
	cursor = ui.plainTextEditOutput->document()->find("class=\"ocr", cursor, QTextDocument::FindBackward);
	cursor = ui.plainTextEditOutput->document()->find(QRegExp("<\\w+"), cursor, QTextDocument::FindBackward);
	if(cursor.isNull()) {
		return;
	}
	// Move to end of tag
	QTextCursor tagEndCursor = ui.plainTextEditOutput->document()->find(">", cursor);
	if(tagEndCursor.isNull()) {
		return;
	}
	cursor.setPosition(tagEndCursor.position(), QTextCursor::KeepAnchor);
	QString tagText = cursor.selectedText();

	static QRegExp idRx("id=\"[A-Za-z]+_(\\d+)");
	static QRegExp bboxRx("bbox\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)");
	if(idRx.indexIn(tagText) == -1 || bboxRx.indexIn(tagText) == -1) {
		return;
	}
	int pageId = idRx.cap(1).toInt();
	int x1 = bboxRx.cap(1).toInt();
	int y1 = bboxRx.cap(2).toInt();
	int x2 = bboxRx.cap(3).toInt();
	int y2 = bboxRx.cap(4).toInt();

	// Search div tag of page
	QTextCursor divCursor = ui.plainTextEditOutput->document()->find(QString("id=\"page_%1\"").arg(pageId), cursor, QTextDocument::FindBackward);
	divCursor = ui.plainTextEditOutput->document()->find("<div", divCursor, QTextDocument::FindBackward);
	tagEndCursor = ui.plainTextEditOutput->document()->find(">", divCursor);
	if(divCursor.isNull() || tagEndCursor.isNull()) {
		return;
	}
	divCursor.setPosition(tagEndCursor.position(), QTextCursor::KeepAnchor);
	QString divText = divCursor.selectedText();
	static QRegExp titleRx("title=\"image\\s+'(.+)';\\s+bbox\\s+\\d+\\s+\\d+\\s+\\d+\\s+\\d+;\\s+pageno\\s+(\\d+);\\s+rot\\s+(\\d+\\.?\\d*)\"");
	if(titleRx.indexIn(divText) == -1) {
		return;
	}
	QString filename = titleRx.cap(1);
	int page = titleRx.cap(2).toInt();
	double angle = titleRx.cap(3).toInt();

	MAIN->getSourceManager()->addSource(filename);
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	int dummy;
	if(MAIN->getDisplayer()->getCurrentImage(dummy) != filename) {
		return;
	}
	MAIN->getDisplayer()->setCurrentPage(page);
	MAIN->getDisplayer()->setRotation(angle);
	MAIN->getDisplayer()->setSelection(QRect(x1, y1, x2 - x1, y2 - y1));
}
