/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Displayer.hh
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

#ifndef DISPLAYER_HH
#define DISPLAYER_HH

#include "common.hh"
#include "Geometry.hh"

#include <cstdint>
#include <vector>
#include <cairomm/cairomm.h>

class DisplayRenderer;
class DisplaySelection;
class DisplaySelectionHandle;

class Displayer {
public:
	Displayer();
	~Displayer();
	bool setSource(const std::string& filename);
	bool setImage(int page = -1);
	std::vector<Cairo::RefPtr<Cairo::ImageSurface>> getSelections() const;
	int getCurrentPage() const{ return m_pagespin->get_value_as_int(); }
	int getNPages(){ double min, max; m_pagespin->get_range(min, max); return int(max); }

private:
	enum class ZoomMode { In, Out, Fit, One };
	struct Geo {
		double a;       // Angle
		double sx, sy;  // Scroll x, y
		double s;       // Scale
		Geometry::Rotation R; // Rotation
	};

	Gtk::DrawingArea* m_canvas;
	Gtk::Viewport* m_viewport;
	Gtk::ScrolledWindow* m_scrollwin;
	Glib::RefPtr<Gtk::Adjustment> m_hadjustment;
	Glib::RefPtr<Gtk::Adjustment> m_vadjustment;
	Gtk::ToolButton* m_zoominbtn;
	Gtk::ToolButton* m_zoomoutbtn;
	Gtk::ToolButton* m_zoomfitbtn;
	Gtk::ToolButton* m_zoomonebtn;
	Gtk::Label* m_ocrstatelabel;
	Gtk::SpinButton* m_rotspin;
	Gtk::SpinButton* m_pagespin;
	Gtk::SpinButton* m_resspin;
	Gtk::SpinButton* m_brispin;
	Gtk::SpinButton* m_conspin;
	Gtk::Window* m_selmenu;

	Cairo::RefPtr<Cairo::ImageSurface> m_image;
	DisplayRenderer* m_renderer;
	bool m_zoomFit;
	int m_scrollspeed[2];
	Geo m_geo;
	Gdk::RGBA m_selColors[2];
	DisplaySelectionHandle* m_curSel = nullptr;
	std::vector<DisplaySelection*> m_selections;

	sigc::connection m_timer;
	sigc::connection m_connection_selDo;
	sigc::connection m_connection_selEnd;
	sigc::connection m_connection_positionAndZoomCanvas;
	sigc::connection m_connection_saveHScrollMark;
	sigc::connection m_connection_saveVScrollMark;
	sigc::connection m_connection_mouseMove;
	sigc::connection m_connection_mousePress;

	sigc::connection m_connection_selmenu_delete;
	sigc::connection m_connection_selmenu_reorder;
	sigc::connection m_connection_selmenu_recognize;
	sigc::connection m_connection_selmenu_clipboard;

	void drawCanvas(const Cairo::RefPtr<Cairo::Context>& ctx);
	void positionCanvas(bool zoom = false);
	bool scrollZoom(GdkEventScroll* ev);
	void saveScrollMark(Glib::RefPtr<Gtk::Adjustment> adj, double& s);
	void setZoom(ZoomMode zoom);
	void getBBSize(double& w, double& h) const;
	void rotate();
	void spinChanged();
	void clearImage();
	Geometry::Point getSelectionCoords(double evx, double evy) const;
	Geometry::Rectangle getSelectionDamageArea(const Geometry::Rectangle& rect) const;
	void mouseMove(GdkEventMotion* ev);
	void mousePress(GdkEventButton* ev);
	void selectionRemove(const DisplaySelection* sel);
	void selectionDo(GdkEventMotion* ev);
	bool scroll();
	void selectionEnd();
	void selectionUpdateColors();
	void autodetectLayout(bool rotated = false);
	void showSelectionMenu(GdkEventButton* ev, int i);
	void hideSelectionMenu();
	Cairo::RefPtr<Cairo::ImageSurface> getTransformedImage(const Geometry::Rectangle& rect) const;
};

#endif // IMAGEDISPLAYER_HH
