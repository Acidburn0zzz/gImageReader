/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Acquirer.cc
 * Copyright (C) 2013 Sandro Mani <manisandro@gmail.com>
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

#include "Acquirer.hh"
#include "Config.hh"
#include "MainWindow.hh"
#include "SourceManager.hh"
#include "Utils.hh"

#include <cstring>

Acquirer::Acquirer()
{
	m_devCombo = Builder("combo:input.acquire.device");
	m_refreshButton = Builder("button:inpute.acquire.device.refresh");
	m_refreshSpinner = Builder("spinner:input.acquire.device");
	m_resCombo = Builder("combo:sources.acquire.resolution");
	m_modeCombo = Builder("combo:sources.acquire.mode");
	m_msgLabel = Builder("label:sources.acquire.message");
	m_outputLabel = Builder("label:sources.acquire.outputname");
	m_buttonBox = Builder("buttonbox:sources.acquire");
	m_scanButton = Builder("button:sources.acquire.scan");
	m_cancelButton = Gtk::manage(new Gtk::Button(Gtk::Stock::CANCEL));
	m_cancelButton->show();

	m_devCombo->set_model(Gtk::ListStore::create(m_devComboCols));
	Gtk::CellRendererText* cell = Gtk::manage(new Gtk::CellRendererText);
	cell->property_ellipsize() = Pango::ELLIPSIZE_END;
	m_devCombo->pack_start(*cell, true);
	m_devCombo->add_attribute(*cell, "text", 0);

	if(m_resCombo->get_active_row_number() == -1) m_resCombo->set_active(2);
	if(m_modeCombo->get_active_row_number() == -1) m_modeCombo->set_active(0);

	m_scanner = Scanner::get_instance();

	CONNECT(m_refreshButton, clicked, [this]{ startDetectDevices(); });
	CONNECT(m_scanButton, clicked, [this]{ startScan(); });
	CONNECT(m_cancelButton, clicked, [this]{ cancelScan(); });
	CONNECT(Builder("button:sources.acquire.output").as<Gtk::Button>(), clicked, [this]{ selectOutputPath(); });
	CONNECT(m_scanner, init_failed, [this]{ scanInitFailed(); });
	CONNECT(m_scanner, update_devices, [this](const std::vector<Scanner::ScanDevice>& devices){ doneDetectDevices(devices); });
	CONNECT(m_scanner, scan_failed, [this](const std::string& msg){ m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1: %2</span>", _("Scan failed"), msg)); });
	CONNECT(m_scanner, scanning_changed, [this](Scanner::ScanState state){ setScanState(state); });
	CONNECT(m_scanner, page_available, [this](const std::string& file){ MAIN->getSourceManager()->addSources({Gio::File::create_for_path(file)});});
	CONNECT(m_devCombo, changed, [this]{ auto it = m_devCombo->get_active(); m_devCombo->set_tooltip_text(it ? static_cast<std::string>((*it)[m_devComboCols.label]) : ""); });

	m_scanner->start();
}

Acquirer::~Acquirer()
{
	m_scanner->stop();
}

void Acquirer::setOutputPath()
{
	m_outputPath = MAIN->getConfig()->getSetting<VarSetting<std::string>>("scanoutput")->getValue();
	if(m_outputPath.empty() || !Glib::file_test(Glib::path_get_dirname(m_outputPath), Glib::FILE_TEST_IS_DIR)){
		m_outputPath = Glib::build_filename(Glib::get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS), _("scan.png"));
		if(!Glib::file_test(Glib::path_get_dirname(m_outputPath), Glib::FILE_TEST_IS_DIR)){
			m_outputPath = Glib::build_filename(Glib::get_home_dir(), _("scan.png"));
		}
	}
	MAIN->getConfig()->getSetting<VarSetting<Glib::ustring>>("scanoutput")->setValue(m_outputPath);
	genOutputPath();
}

void Acquirer::selectOutputPath()
{
	Gtk::FileChooserDialog dialog(*MAIN->getWindow(), _("Choose Output Filename..."), Gtk::FILE_CHOOSER_ACTION_SAVE);
	dialog.add_button("gtk-cancel", Gtk::RESPONSE_CANCEL);
	dialog.add_button("gtk-ok", Gtk::RESPONSE_OK);
	dialog.set_select_multiple(false);
	dialog.set_create_folders(true);
	dialog.set_do_overwrite_confirmation(true);
	dialog.set_local_only(false);
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
	filter->set_name(_("PNG Images"));
	filter->add_mime_type("image/png");
	filter->add_pattern("*.png");
	dialog.add_filter(filter);
	dialog.set_filter(filter);

	int response = dialog.run();
	if(response == Gtk::RESPONSE_OK){
		m_outputPath = dialog.get_filename();
		MAIN->getConfig()->getSetting<VarSetting<Glib::ustring>>("scanoutput")->setValue(m_outputPath);
		genOutputPath();
	}
}

void Acquirer::genOutputPath()
{
	int i = 0;
	std::string base, ext;
	Utils::get_filename_parts(m_outputPath, base, ext);
	base = Glib::Regex::create("_[0-9]+$")->replace(base, 0, "", static_cast<Glib::RegexMatchFlags>(0));
	while(Glib::file_test(m_outputPath, Glib::FILE_TEST_EXISTS)){
		m_outputPath = Glib::ustring::compose("%1_%2.%3", base, ++i, ext);
	}
	m_outputLabel->set_text(m_outputPath);
	m_outputLabel->set_tooltip_text(m_outputPath);
}

void Acquirer::scanInitFailed()
{
	m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1</span>", _("Failed to initialize the scanning backend.")));
	m_scanButton->set_sensitive(false);
	m_refreshButton->set_sensitive(false);
	m_scanner->stop();
}

void Acquirer::startDetectDevices()
{
	m_refreshButton->hide();
	m_refreshSpinner->show();
	m_refreshSpinner->start();
	m_msgLabel->set_text("");
	m_devCombo->set_model(Gtk::ListStore::create(m_devComboCols));
	m_scanButton->set_sensitive(false);
	m_scanner->redetect();
}

void Acquirer::doneDetectDevices(const std::vector<Scanner::ScanDevice>& devices)
{
	m_refreshButton->show();
	m_refreshSpinner->hide();
	m_refreshSpinner->stop();
	if(devices.empty()){
		m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1</span>", _("No scanners were detected.")));
	}else{
		Glib::RefPtr<Gtk::ListStore> store = Glib::RefPtr<Gtk::ListStore>::cast_static(m_devCombo->get_model());
		for(const Scanner::ScanDevice& device : devices){
			Gtk::TreeIter it = store->append();
			it->set_value(m_devComboCols.label, device.label);
			it->set_value(m_devComboCols.name, device.name);
		}
		m_devCombo->set_active(0);
		m_scanButton->set_sensitive(true);
	}
}

void Acquirer::startScan()
{
	m_buttonBox->remove(*m_scanButton);
	m_cancelButton->set_sensitive(true);
	m_buttonBox->pack_start(*m_cancelButton, false, true);
	m_msgLabel->set_text(_("Starting scan..."));

	int res[] = {75, 100, 200, 300, 600, 1200};
	Scanner::ScanMode modes[] = {Scanner::ScanMode::COLOR, Scanner::ScanMode::GRAY};
	genOutputPath();
	Scanner::ScanOptions opts = {m_outputPath, res[m_resCombo->get_active_row_number()], modes[m_modeCombo->get_active_row_number()], 8, Scanner::ScanType::SINGLE, 0, 0};
	genOutputPath();
	m_scanner->scan((*m_devCombo->get_active())[m_devComboCols.name], opts);
}

void Acquirer::setScanState(Scanner::ScanState state)
{
	if(state == Scanner::ScanState::OPEN){
		m_msgLabel->set_text(_("Opening device..."));
	}else if(state == Scanner::ScanState::SET_OPTIONS){
		m_msgLabel->set_text(_("Setting options..."));
	}else if(state == Scanner::ScanState::START){
		m_msgLabel->set_text(_("Starting scan..."));
	}else if(state == Scanner::ScanState::GET_PARAMETERS){
		m_msgLabel->set_text(_("Getting parameters..."));
	}else if(state == Scanner::ScanState::READ){
		m_msgLabel->set_text(_("Transferring data..."));
	}else if(state == Scanner::ScanState::IDLE){
		doneScan();
	}
}

void Acquirer::cancelScan()
{
	m_scanner->cancel();
	m_cancelButton->set_sensitive(false);
	m_msgLabel->set_text(_("Cancelling scan..."));
}

void Acquirer::doneScan()
{
	m_buttonBox->remove(*m_cancelButton);
	m_buttonBox->pack_start(*m_scanButton, false, true);
	m_msgLabel->set_text("");
}
