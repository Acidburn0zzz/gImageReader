/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Scanner.hh
 *   Copyright (C) 2013-2014 Sandro Mani <manisandro@gmail.com>
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

#ifndef SCANNER_HPP
#define SCANNER_HPP

#include "ScanBackend.hh"
#include "Utils.hh"

class Scanner {
public:
	enum class State { IDLE = 0, OPEN, SET_OPTIONS, START, GET_PARAMETERS, READ };

	void start();
	void redetect();
	void scan(const std::string& device, ScanBackend::Options options);
	void cancel();
	void stop();

	sigc::signal<void> signal_initFailed() const{ return m_signal_initFailed; }
	sigc::signal<void,std::vector<ScanBackend::Device>> signal_devicesDetected() const{ return m_signal_devicesDetected; }
	sigc::signal<void,Glib::ustring> signal_scanFailed() const{ return m_signal_scanFailed; }
	sigc::signal<void,Scanner::State> signal_scanStateChanged() const{ return m_signal_scanStateChanged; }
	sigc::signal<void,std::string> signal_pageAvailable() const{ return m_signal_pageAvailable; }

private:
	struct Request {
		enum class Type { Redetect, StartScan, Cancel, Quit } type;
		void* data;
	};

	Glib::Threads::Thread* m_thread = nullptr;
	ScanBackend* m_backend = nullptr;
	State m_state = State::IDLE;
	Utils::AsyncQueue<Request> m_requestQueue;
	std::queue<ScanBackend::Job*> m_jobQueue;

	void run();
	void doCompleteDocument();
	void doCompletePage();
	void doSetOptions();
	void doGetParameters();
	void doOpen();
	void doRead();
	void doRedetect();
	void doStart();
	void doClose();
	void setState(State state);
	void failScan(const Glib::ustring& errorString);

	sigc::signal<void> m_signal_initFailed;
	sigc::signal<void,std::vector<ScanBackend::Device>> m_signal_devicesDetected;
	sigc::signal<void,Glib::ustring> m_signal_scanFailed;
	sigc::signal<void,Scanner::State> m_signal_scanStateChanged;
	sigc::signal<void,std::string> m_signal_pageAvailable;
};

#endif // SCANNER_HPP
