/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * MainWindow.cc
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

#include "Application.hh"
#include "MainWindow.hh"
#include "Acquirer.hh"
#include "Config.hh"
#include "Displayer.hh"
#include "Notifier.hh"
#include "OutputManager.hh"
#include "Recognizer.hh"
#include "SourceManager.hh"
#include "Utils.hh"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <unistd.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif


#if ENABLE_VERSIONCHECK
#define CHECKURL "http://sourceforge.net/projects/gimagereader/files/LATEST/download?use_mirror=autoselect"
#define DOWNLOADURL "http://sourceforge.net/projects/gimagereader/files"
#define CHANGELOGURL "http://sourceforge.net/projects/gimagereader/files/changelog.txt/download?use_mirror=autoselect"
#endif // ENABLE_VERSIONCHECK

void crash_handler(int sig)
{
	std::signal(sig, nullptr);
	std::string filename;
	if(MAIN->getOutputManager()->getModified()){
		filename = Glib::build_filename(g_get_home_dir(), Glib::ustring::compose("%1_crash-save.txt", PACKAGE_NAME));
		int i = 0;
		while(Glib::file_test(filename, Glib::FILE_TEST_EXISTS)){
			++i;
			filename = Glib::build_filename(g_get_home_dir(), Glib::ustring::compose("%1_crash-save_%2.txt", PACKAGE_NAME, i));
		}
		MAIN->getOutputManager()->saveBuffer(filename);
	}
	Glib::RefPtr<Application> app = Glib::RefPtr<Application>::cast_static(Gio::Application::get_default());
#ifdef G_OS_WIN32
	Glib::spawn_async("", std::vector<std::string>{pkgExePath, "crashhandle", Glib::ustring::compose("%1", getpid()), filename});
	std::exit(1);
#else
	Glib::spawn_sync("", std::vector<std::string>{pkgExePath, "crashhandle", Glib::ustring::compose("%1", getpid()), filename});
	std::raise(sig);
#endif
}

MainWindow* MainWindow::s_instance = nullptr;

MainWindow::MainWindow()
{
	s_instance = this;

	m_config = new Config;
	m_acquirer = new Acquirer;
	m_displayer = new Displayer;
	m_outputManager = new OutputManager;
	m_recognizer = new Recognizer;
	m_sourceManager = new SourceManager;

	m_window = Builder("window:main");
	m_aboutdialog = Builder("dialog:about");
	m_statusbar = Builder("statusbar:main");
	m_aboutdialog->set_version(PACKAGE_VERSION);

	m_idlegroup.push_back(Builder("tbbutton:main.zoomin"));
	m_idlegroup.push_back(Builder("tbbutton:main.zoomout"));
	m_idlegroup.push_back(Builder("tbbutton:main.normsize"));
	m_idlegroup.push_back(Builder("tbbutton:main.bestfit"));
	m_idlegroup.push_back(Builder("tbbutton:main.rotleft"));
	m_idlegroup.push_back(Builder("tbbutton:main.rotright"));
	m_idlegroup.push_back(Builder("spin:main.rotate"));
	m_idlegroup.push_back(Builder("spin:display.page"));
	m_idlegroup.push_back(Builder("spin:display.brightness"));
	m_idlegroup.push_back(Builder("spin:display.contrast"));
	m_idlegroup.push_back(Builder("spin:display.resolution"));
	m_idlegroup.push_back(Builder("tbbutton:main.autolayout"));
	m_idlegroup.push_back(Builder("tbmenu:main.recognize"));

	Gtk::ToggleToolButton* showOutputPaneBtn = Builder("tbbutton:main.outputpane");

	CONNECT(m_window, delete_event, [this](GdkEventAny* ev) { return quit(ev); });
	CONNECT(Builder("menuitem:main.redetect").as<Gtk::MenuItem>(), activate, [this]{ m_config->updateLanguagesMenu(); });
	CONNECT(Builder("menuitem:main.configure").as<Gtk::MenuItem>(), activate, [this]{ m_config->showDialog(); });
	CONNECT(Builder("menuitem:main.help").as<Gtk::MenuItem>(), activate, [this]{ showHelp(); });
	CONNECT(Builder("menuitem:main.about").as<Gtk::MenuItem>(), activate, [this]{ showAbout(); });
	CONNECTS(Builder("combo:config.settings.paneorient").as<Gtk::ComboBoxText>(), changed,
			 [this](Gtk::ComboBoxText* box){ setOutputPaneOrientation(box); });
	CONNECT(showOutputPaneBtn, toggled, [this,showOutputPaneBtn] { m_outputManager->setVisible(showOutputPaneBtn->get_active()); });
	CONNECTS(Builder("tbbutton:main.controls").as<Gtk::ToggleToolButton>(), toggled,
			 [this](Gtk::ToggleToolButton* b) { Builder("toolbar:display").as<Gtk::Toolbar>()->set_visible(b->get_active()); });
	CONNECT(m_config, languageChanged, [this](const Config::Lang& lang){ m_outputManager->setLanguage(lang); });
	CONNECT(m_sourceManager, sourceChanged, [this](const std::string& newsrc){
		m_displayer->setSource(newsrc);
		m_window->set_title(newsrc.empty() ? PACKAGE_NAME : Glib::path_get_basename(newsrc) + " - " + PACKAGE_NAME);
	});
	CONNECT(Builder("box:output").as<Gtk::Widget>(), show, [showOutputPaneBtn]{ showOutputPaneBtn->set_active(true); });
	CONNECT(Builder("box:output").as<Gtk::Widget>(), hide, [showOutputPaneBtn]{ showOutputPaneBtn->set_active(false); });

	m_config->addSetting("outputorient", new ComboSetting("combo:config.settings.paneorient"));
	m_config->addSetting("showcontrols", new SwitchSettingT<Gtk::ToggleToolButton>("tbbutton:main.controls"));

	m_config->updateLanguagesMenu();

	m_acquirer->start();

	m_statusbar->push(_("Select an image to begin..."));
	m_stateStack.push_back(State::Idle);
	setState(State::Idle);

	const std::vector<int>& geom = m_config->getSetting<VarSetting<std::vector<int>>>("wingeom")->getValue();
	if(geom.size() == 4){
		m_window->resize(geom[2], geom[3]);
		m_window->move(geom[0], geom[1]);
	}
#if ENABLE_VERSIONCHECK
	if(m_config->getSetting<SwitchSetting>("updatecheck")->getValue()){
		m_newVerThread = Glib::Threads::Thread::create([this]{ getNewestVersion(); });
	}
#else
	Builder("check:config.settings.update").as<Gtk::Widget>()->hide();
#endif

	std::signal(SIGSEGV, crash_handler);
	std::signal(SIGABRT, crash_handler);
}

MainWindow::~MainWindow()
{
	delete m_acquirer;
	delete m_displayer;
	delete m_outputManager;
	delete m_recognizer;
	delete m_sourceManager;
	delete m_config;
	s_instance = nullptr;
}

void MainWindow::openFiles(const std::vector<Glib::RefPtr<Gio::File>>& files)
{
	m_sourceManager->addSources(files);
}

void MainWindow::pushState(State state, const Glib::ustring &msg)
{
	m_statusbar->push(msg);
	State old = m_stateStack.back();
	m_stateStack.push_back(state);
	if(m_stateStack.back() != old){
		setState(m_stateStack.back());
	}
}

void MainWindow::popState()
{
	m_statusbar->pop();
	State old = m_stateStack.back();
	m_stateStack.pop_back();
	if(m_stateStack.back() != old){
		setState(m_stateStack.back());
	}
}

void MainWindow::setState(State state)
{
	m_window->set_sensitive(true);
	if(state == State::Idle){
		for(Gtk::Widget* w : m_idlegroup){ w->set_sensitive(false); }
	}else if(state == State::Normal){
		for(Gtk::Widget* w : m_idlegroup){ w->set_sensitive(true); }
	}else if(state == State::Busy){
		m_window->set_sensitive(false);
	}
}

bool MainWindow::quit(GdkEventAny*)
{
	if(!m_outputManager->clearBuffer()){
		return true;
	}
	if((m_window->get_window()->get_state() & Gdk::WINDOW_STATE_MAXIMIZED) == 0){
		std::vector<int> geom(4);
		m_window->get_position(geom[0], geom[1]);
		m_window->get_size(geom[2], geom[3]);
		m_config->getSetting<VarSetting<std::vector<int>>>("wingeom")->setValue(geom);
	}
	return false;
}

void MainWindow::showAbout()
{
	m_aboutdialog->run();
	m_aboutdialog->hide();
}

void MainWindow::showHelp(const std::string& chapter)
{
	std::string manualFile = Glib::build_path("/", std::vector<std::string>{pkgDataDir, "manual.html"});
	std::string manualURI = Glib::filename_to_uri(Utils::make_absolute_path(manualFile)) + chapter;
#ifdef G_OS_WIN32
	ShellExecute(nullptr, "open", manualURI.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	gtk_show_uri(nullptr, manualURI.c_str(), GDK_CURRENT_TIME, 0);
#endif
}

void MainWindow::setOutputPaneOrientation(Gtk::ComboBoxText* combo)
{
	int active = combo->get_active_row_number();
	Gtk::Orientation orient = active ? Gtk::ORIENTATION_HORIZONTAL : Gtk::ORIENTATION_VERTICAL;
	Builder("paned:output").as<Gtk::Paned>()->set_orientation(orient);
}

#if ENABLE_VERSIONCHECK
void MainWindow::getNewestVersion()
{
	char buf[16] = {};
	try {
		Glib::RefPtr<Gio::File> file = Gio::File::create_for_uri(CHECKURL);
		Glib::RefPtr<Gio::FileInputStream> stream = file->read();
		stream->read(buf, 16);
	} catch (const Glib::Error&) {
		return;
	}
	std::string newver(buf);
	newver.erase(std::remove_if(newver.begin(), newver.end(), ::isspace), newver.end());
	if(Glib::Regex::create(R"(^[\d+\.]+\d+?$)")->match(newver, 0, Glib::RegexMatchFlags(0))){
		Glib::signal_idle().connect_once([this,newver]{ checkVersion(newver); });
	}
}

void MainWindow::checkVersion(const Glib::ustring& newver)
{
	m_newVerThread->join();
	Glib::ustring curver = m_aboutdialog->get_version();
	// Remove anything after a - (i.e. 1.0.0-svn)
	Glib::ustring::size_type pos = curver.find('-');
	if(pos != Glib::ustring::npos){
		curver = curver.substr(0, pos);
	}

	if(newver.compare(curver) > 0){
		Notifier::notify(_("New version"), Glib::ustring::compose(_("gImageReader %1 is available"), newver),
			{{_("Download"), []{ gtk_show_uri(Gdk::Screen::get_default()->gobj(), DOWNLOADURL, GDK_CURRENT_TIME, 0); return false; }},
			 {_("Changelog"), []{ gtk_show_uri(Gdk::Screen::get_default()->gobj(), CHANGELOGURL, GDK_CURRENT_TIME, 0); return false; }},
			 {_("Don't notify again"), [this]{ m_config->getSetting<SwitchSetting>("updatecheck")->setValue(false); return true; }}});
	}
}
#endif // ENABLE_VERSIONCHECK
