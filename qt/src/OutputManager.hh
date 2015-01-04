/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * OutputManager.hh
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

#ifndef OUTPUTMANAGER_HH
#define OUTPUTMANAGER_HH

#include <QtSpell.hpp>

#include "Config.hh"
#include "MainWindow.hh"

class QDBusInterface;
template <class T> class QDBusReply;
class QDBusError;
class SubstitutionsManager;
class UI_MainWindow;

class OutputManager : public QObject {
	Q_OBJECT
public:
	OutputManager(const UI_MainWindow& _ui);
	~OutputManager();
	void addText(const QString& text, bool insert = false);
	bool getBufferModified() const;

public slots:
	bool clearBuffer();
	bool saveBuffer(const QString& filename = "");
	void setLanguage(const Config::Lang &lang, bool force = false);

private:
	enum class InsertMode { Append, Cursor, Replace };

	QDBusInterface* m_dbusIface;
	const UI_MainWindow& ui;
	InsertMode m_insertMode;
	QtSpell::TextEditChecker m_spell;
	MainWindow::Notification m_notifierHandle;
	SubstitutionsManager* m_substitutionsManager;

	void findReplace(bool backwards, bool replace);

private slots:
	void clearErrorState();
	void filterBuffer();
	void findNext();
	void findPrev();
	void replaceAll();
	void replaceNext();
	void scrollCursorIntoView();
	void setFont();
	void setInsertMode(QAction* action);
	void dictionaryAutoinstall();
	void dictionaryAutoinstallDone();
	void dictionaryAutoinstallError(const QDBusError& error);
};

#endif // OUTPUTMANAGER_HH
