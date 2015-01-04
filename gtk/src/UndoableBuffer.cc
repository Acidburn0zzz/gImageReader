/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * UndoableBuffer.cc
 * Based on Python code by Florian Heinle which is
 *   Copyright 2009 Florian Heinle
 * Modifications are
 *   Copyright (C) 2013-2015 Sandro Mani <manisandro@gmail.com>
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

#include "UndoableBuffer.hh"
#include <cctype>

struct UndoableBuffer::Action {
	virtual ~Action(){}
};

struct UndoableBuffer::UndoableInsert : public UndoableBuffer::Action {
	Glib::ustring text;
	int offset;
	bool isWhitespace;
	bool isMergeable;

	UndoableInsert(const Gtk::TextIter& _iter, const Glib::ustring& _text){
		offset = _iter.get_offset();
		text = _text;
		isWhitespace = text.length() == 1 && !isspace(text[0]);
		isMergeable = (text.length() == 1);
	}
};

struct UndoableBuffer::UndoableDelete : public UndoableBuffer::Action {
	Glib::ustring text;
	int start, end;
	bool deleteKeyUsed;
	bool isWhitespace;
	bool isMergeable;

	UndoableDelete(Gtk::TextBuffer& _buffer, const Gtk::TextIter& _startIter, const Gtk::TextIter& _endIter){
		text = _buffer.get_text(_startIter, _endIter, false);
		start = _startIter.get_offset();
		end = _endIter.get_offset();
		deleteKeyUsed = (_buffer.get_iter_at_mark(_buffer.get_insert()).get_offset() <= start);
		isWhitespace = text.length() == 1 && !isspace(text[0]);
		isMergeable = (text.length() == 1);
	}
};

UndoableBuffer::UndoableBuffer()
	: Gtk::TextBuffer()
{
	m_undoInProgress = false;
	CONNECT(this, insert, [this](const Gtk::TextIter& it, const Glib::ustring& text, int len){ onInsertText( it, text, len); }, false); // false: run_after_default = false
	CONNECT(this, erase, [this](const Gtk::TextIter& start, const Gtk::TextIter& end){ onDeleteRange(start, end); }, false);
}

void UndoableBuffer::onInsertText(const Gtk::TextIter &it, const Glib::ustring &text, int len)
{
	if(m_undoInProgress || text.empty()){
		return;
	}
	freeStack(m_redoStack);
	UndoableInsert* undoAction = new UndoableInsert(it, text);
	if(m_undoStack.empty() || !dynamic_cast<UndoableInsert*>(m_undoStack.top())){
		m_undoStack.push(undoAction);
		m_signal_histroyChanged.emit();
		return;
	}
	UndoableInsert* prevInsert = static_cast<UndoableInsert*>(m_undoStack.top());
	if(insertMergeable(prevInsert, undoAction)){
		prevInsert->text += undoAction->text;
	}else{
		m_undoStack.push(undoAction);
	}
	m_signal_histroyChanged.emit();
}

void UndoableBuffer::onDeleteRange(const Gtk::TextIter &start, const Gtk::TextIter &end)
{
	if(m_undoInProgress || start == end){
		return;
	}
	freeStack(m_redoStack);
	UndoableDelete* undoAction = new UndoableDelete(*this, start, end);
	if(m_undoStack.empty() || !dynamic_cast<UndoableDelete*>(m_undoStack.top())){
		m_undoStack.push(undoAction);
		m_signal_histroyChanged.emit();
		return;
	}
	UndoableDelete* prevDelete = static_cast<UndoableDelete*>(m_undoStack.top());
	if(deleteMergeable(prevDelete, undoAction)){
		if(prevDelete->start == undoAction->start){ // Delete key used
			prevDelete->text += undoAction->text;
			prevDelete->end += (undoAction->end - undoAction->start);
		}else{ // Backspace used
			prevDelete->text = undoAction->text + prevDelete->text;
			prevDelete->start = undoAction->start;
		}
	}else{
		m_undoStack.push(undoAction);
	}
	m_signal_histroyChanged.emit();
}

void UndoableBuffer::undo()
{
	if(m_undoStack.empty()){
		return;
	}
	m_undoInProgress = true;
	Action* undoAction = m_undoStack.top();
	m_undoStack.pop();
	m_redoStack.push(undoAction);
	if(dynamic_cast<UndoableInsert*>(undoAction)){
		UndoableInsert* insertAction = static_cast<UndoableInsert*>(undoAction);
		Gtk::TextIter start = get_iter_at_offset(insertAction->offset);
		Gtk::TextIter end = get_iter_at_offset(insertAction->offset + insertAction->text.length());
		place_cursor(erase(start, end));
		if(!m_undoStack.empty() && isReplace(dynamic_cast<UndoableDelete*>(m_undoStack.top()), insertAction)){
			undo();
		}
	}else{
		UndoableDelete* deleteAction = static_cast<UndoableDelete*>(undoAction);
		Gtk::TextIter start = get_iter_at_offset(deleteAction->start);
		Gtk::TextIter it = insert(start, deleteAction->text);
		if(deleteAction->deleteKeyUsed){
			place_cursor(get_iter_at_offset(deleteAction->start));
		}else{
			place_cursor(it);
		}
	}
	m_signal_histroyChanged.emit();
	m_undoInProgress = false;
}

void UndoableBuffer::redo()
{
	if(m_redoStack.empty()){
		return;
	}
	m_undoInProgress = true;
	Action* redoAction = m_redoStack.top();
	m_redoStack.pop();
	m_undoStack.push(redoAction);
	if(dynamic_cast<UndoableInsert*>(redoAction)){
		UndoableInsert* insertAction = static_cast<UndoableInsert*>(redoAction);
		Gtk::TextIter start = get_iter_at_offset(insertAction->offset);
		insert(start, insertAction->text);
		place_cursor(get_iter_at_offset(insertAction->offset + insertAction->text.length()));
	}else{
		UndoableDelete* deleteAction = static_cast<UndoableDelete*>(redoAction);
		Gtk::TextIter start = get_iter_at_offset(deleteAction->start);
		Gtk::TextIter end = get_iter_at_offset(deleteAction->end);
		place_cursor(erase(start, end));
		if(!m_redoStack.empty() && isReplace(deleteAction, dynamic_cast<UndoableInsert*>(m_redoStack.top()))){
			redo();
		}
	}
	m_signal_histroyChanged.emit();
	m_undoInProgress = false;
}

void UndoableBuffer::save_selection_bounds(bool viewSelected)
{
	Gtk::TextIter ins = get_iter_at_mark(get_insert());
	Gtk::TextIter sel = get_iter_at_mark(get_selection_bound());
	if(viewSelected || ins.get_offset() != sel.get_offset()){
		if(ins.get_offset() < sel.get_offset()){
			m_selStart = ins;
			m_selEnd = sel;
		}else{
			m_selStart = sel;
			m_selEnd = ins;
		}
	}
}

bool UndoableBuffer::get_selection_bounds(Gtk::TextIter& start, Gtk::TextIter& end) const
{
	start = m_selStart;
	end = m_selEnd;
	return m_selEnd.get_offset() - m_selStart.get_offset() > 0;
}

Gtk::TextIter UndoableBuffer::replace_range(const Glib::ustring &text, const Gtk::TextIter& start, const Gtk::TextIter& end)
{
	Gtk::TextIter it = erase(start, end);
	return insert(it, text);
}

void UndoableBuffer::freeStack(std::stack<Action *> &stack)
{
	while(!stack.empty()){
		delete stack.top();
		stack.pop();
	}
}

bool UndoableBuffer::insertMergeable(const UndoableInsert* prev, const UndoableInsert* cur) const
{
	return (cur->offset == prev->offset + int(prev->text.length())) &&
		   (cur->isWhitespace == prev->isWhitespace) &&
		   (cur->isMergeable && prev->isMergeable);
}

bool UndoableBuffer::deleteMergeable(const UndoableDelete* prev, const UndoableDelete* cur) const
{
	return (prev->deleteKeyUsed == cur->deleteKeyUsed) &&
		   (cur->isWhitespace == prev->isWhitespace) &&
		   (cur->isMergeable && prev->isMergeable) &&
		   (prev->start == cur->start || prev->start == cur->end);
}

bool UndoableBuffer::isReplace(const UndoableDelete* del, const UndoableInsert* ins) const
{
	return del && ins && del->start == ins->offset;
}
