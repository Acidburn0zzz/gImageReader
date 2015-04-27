/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * OutputTextEdit.cc
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

#include "OutputTextEdit.hh"

#include <QPainter>
#include <QSyntaxHighlighter>


class OutputTextEdit::WhitespaceHighlighter : public QSyntaxHighlighter
{
public:
	WhitespaceHighlighter(QTextDocument* document)
		: QSyntaxHighlighter(document) {}
private:
	void highlightBlock(const QString &text)
	{
		QTextCharFormat fmt;
		fmt.setForeground(Qt::gray);

		QRegExp expression("\\s");
		int index = text.indexOf(expression);
		while (index >= 0) {
			int length = expression.matchedLength();
			setFormat(index, length, fmt);
			index = text.indexOf(expression, index + length);
		}
	}
};


OutputTextEdit::OutputTextEdit(QWidget *parent)
	: QPlainTextEdit(parent)
{
	m_wsHighlighter = new WhitespaceHighlighter(document());

	m_regionCursor = textCursor();
	m_regionCursor.movePosition(QTextCursor::Start);
	m_regionCursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	m_entireRegion = true;

	connect(this, SIGNAL(cursorPositionChanged()), this, SLOT(saveRegionBounds()));
}

OutputTextEdit::~OutputTextEdit()
{
	delete m_wsHighlighter;
}

QTextCursor OutputTextEdit::regionBounds() const{
	QTextCursor c = m_regionCursor;
	if(c.anchor() > c.position()){
		int pos = c.anchor();
		int anchor = c.position();
		c.setPosition(anchor);
		c.setPosition(pos, QTextCursor::KeepAnchor);
	}
	return c;
}

void OutputTextEdit::setDrawWhitespace(bool drawWhitespace)
{
	m_drawWhitespace = drawWhitespace;
	QTextOption textOption = document()->defaultTextOption();
	if(drawWhitespace){
		textOption.setFlags(QTextOption::ShowTabsAndSpaces | QTextOption::AddSpaceForLineAndParagraphSeparators);
	}else{
		textOption.setFlags(0);
	}
	document()->setDefaultTextOption(textOption);
}

void OutputTextEdit::paintEvent(QPaintEvent *e)
{
	QPointF offset = contentOffset();

	if(!m_entireRegion){
		QPainter painter(viewport());
		painter.setBrush(QPalette().highlight().color().lighter(160));
		painter.setPen(Qt::NoPen);
		QTextCursor regionCursor = regionBounds();

		QTextCursor regionStart(document());
		regionStart.setPosition(regionCursor.anchor());
		QTextBlock startBlock = regionStart.block();
		int startLinePos = regionStart.position() - startBlock.position();
		QTextLine startLine = startBlock.layout()->lineForTextPosition(startLinePos);

		QTextCursor regionEnd(document());
		regionEnd.setPosition(regionCursor.position());
		QTextBlock endBlock = regionEnd.block();
		int endLinePos = regionEnd.position() - endBlock.position();
		QTextLine endLine = endBlock.layout()->lineForTextPosition(endLinePos);

		// Draw start rectangle
		qreal top;
		QRectF rect;
		if(startBlock.blockNumber() == endBlock.blockNumber() && startLine.lineNumber() == endLine.lineNumber()){
			top = blockBoundingGeometry(startBlock).translated(offset).top();
			rect = startLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setLeft(startLine.cursorToX(startLinePos) - 0.5);
			rect.setRight(endLine.cursorToX(endLinePos));
			painter.drawRect(rect);
		}else{
			// Draw selection on start line
			top = blockBoundingGeometry(startBlock).translated(offset).top();
			rect = startLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setLeft(startLine.cursorToX(startLinePos) - 0.5);
			painter.drawRect(rect);

			// Draw selections inbetween
			QTextBlock block = startBlock;
			int lineNo = startLine.lineNumber() + 1;
			while(!(block.blockNumber() == endBlock.blockNumber() && lineNo == endLine.lineNumber())){
				if(block.isValid() && lineNo < block.lineCount()){
					painter.drawRect(block.layout()->lineAt(lineNo).naturalTextRect().translated(offset.x() - 0.5, top));
				}
				++lineNo;
				if(lineNo >= block.lineCount()){
					block = block.next();
					top = blockBoundingGeometry(block).translated(offset).top();
					lineNo = 0;
				}
			}

			// Draw selection on end line
			top = blockBoundingGeometry(endBlock).translated(offset).top();
			rect = endLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setRight(endLine.cursorToX(endLinePos));
			painter.drawRect(rect);
		}
	}

	QPlainTextEdit::paintEvent(e);

	if(m_drawWhitespace){
		QTextBlock block = firstVisibleBlock();
		qreal top = blockBoundingGeometry(block).translated(offset).top();
		qreal bottom = top + blockBoundingRect(block).height();

		QPainter painter(viewport());
		painter.setPen(Qt::gray);
		QChar visualArrow((ushort)0x21b5);
		QChar paragraph((ushort)0x00b6);

		// block.next().isValid(): don't draw line break on last block
		while(block.isValid() && block.next().isValid() && top <= e->rect().bottom()){
			if(block.isVisible() && bottom >= e->rect().top()){
				QTextLayout *layout = block.layout();
				// Draw hard line breaks (i.e. those not due to word wrapping)
				QTextLine line = layout->lineAt(layout->lineCount() - 1);
				QRectF lineRect = line.naturalTextRect().translated(offset.x(), top);
				if(line.textLength() == 0){
					painter.drawText(QPointF(lineRect.right(), lineRect.top() + line.ascent()), paragraph);
				}else{
					painter.drawText(QPointF(lineRect.right(), lineRect.top() + line.ascent()), visualArrow);
				}
			}
			block = block.next();
			top = bottom;
			bottom = top + blockBoundingRect(block).height();
		}
	}
}

void OutputTextEdit::saveRegionBounds()
{
	QTextCursor c = textCursor();
	if(hasFocus()){
		bool dorepaint = m_regionCursor.hasSelection() &&
				!((m_regionCursor.anchor() == c.anchor() && m_regionCursor.position() == c.position()) ||
				  (m_regionCursor.anchor() == c.position() && m_regionCursor.position() == c.anchor()));
		m_regionCursor = c;
		if(dorepaint){
			viewport()->repaint();
		}
		// If only one word is selected, don't treat it as a region
		if(!m_regionCursor.selectedText().contains(QRegExp("\\s"))){
			m_regionCursor.clearSelection();
		}
	}
	c.movePosition(QTextCursor::Start);
	c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	// If nothing is selected, set the region to the entire contents
	if(!m_regionCursor.hasSelection()){
		m_regionCursor.setPosition(c.anchor());
		m_regionCursor.setPosition(c.position(), QTextCursor::KeepAnchor);
	}
	m_entireRegion = (m_regionCursor.anchor() == c.anchor() && m_regionCursor.position() == c.position());
}
