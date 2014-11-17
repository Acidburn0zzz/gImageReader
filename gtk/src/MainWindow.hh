/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * MainWindow.hh
 * Copyright (C) 2013-2014 Sandro Mani <manisandro@gmail.com>
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

#ifndef MAINWINDOW_HH
#define MAINWINDOW_HH

#include "common.hh"

#define MAIN MainWindow::getInstance()

class Config;
class Acquirer;
class Displayer;
class OutputManager;
class Recognizer;
class Source;
class SourceManager;

class MainWindow {
public:
	enum class State { Idle, Normal, Busy };

	static MainWindow* getInstance(){ return s_instance; }

	MainWindow();
	~MainWindow();

	void setMenuModel(const Glib::RefPtr<Gio::MenuModel>& menuModel);
	void openFiles(const std::vector<Glib::RefPtr<Gio::File>>& files);
	void pushState(State state, const Glib::ustring& msg);
	void popState();

	Config* getConfig(){ return m_config; }
	Displayer* getDisplayer(){ return m_displayer; }
	OutputManager* getOutputManager(){ return m_outputManager; }
	Recognizer* getRecognizer(){ return m_recognizer; }
	SourceManager* getSourceManager(){ return m_sourceManager; }
	Gtk::Window* getWindow() const{ return m_window; }
	void redetectLanguages();
	void showConfig();
	void showHelp(const std::string& chapter = "");
	void showAbout();

private:
	static MainWindow* s_instance;

	Gtk::ApplicationWindow* m_window;
	Gtk::AboutDialog* m_aboutdialog;
	Gtk::Statusbar* m_statusbar;

	Config* m_config = nullptr;
	Acquirer* m_acquirer = nullptr;
	Displayer* m_displayer = nullptr;
	OutputManager* m_outputManager = nullptr;
	Recognizer* m_recognizer = nullptr;
	SourceManager* m_sourceManager = nullptr;

	Glib::Threads::Thread* m_newVerThread = nullptr;

	std::vector<Gtk::Widget*> m_idlegroup;
	std::vector<State> m_stateStack;

	void onSourceChanged(Source* source);
	bool quit(GdkEventAny*);
	void setOutputPaneOrientation(Gtk::ComboBoxText* combo);
	void setState(State state);
#if ENABLE_VERSIONCHECK
	void getNewestVersion();
	void checkVersion(const Glib::ustring& newver);
#endif
};

#endif
