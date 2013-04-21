/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Notifier.hh
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

#ifndef NOTIFIER_HH
#define NOTIFIER_HH

#include "common.hh"

#include <functional>

class Notifier {
public:
	struct Action {
		Glib::ustring label;
		std::function<void()> action;
	};

	Notifier();
	~Notifier(){ hide(); }

	void notify(const Glib::ustring& title, const Glib::ustring& message, const std::vector<Action>& actions);
	void hide();

private:
	Gtk::EventBox* m_notifyEvBox;
	Gtk::Box* m_notifyBox;
	Gtk::Label* m_notifyTitle;
	Gtk::Label* m_notifyMessage;
	std::vector<Gtk::Button*> m_buttons;
};

#endif // NOTIFIER_HH
