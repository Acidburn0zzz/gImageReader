/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Displayer.cc
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

#include "MainWindow.hh"
#include "Displayer.hh"
#include "DisplayManipulator.hh"
#include "DisplayRenderer.hh"
#include "DisplaySelection.hh"
#include "Recognizer.hh"
#include "Utils.hh"

#include <tesseract/baseapi.h>
#include <cassert>

Displayer::Displayer()
{
	m_canvas = Builder("drawingarea:display");
	m_viewport = Builder("viewport:display");
	Gtk::ScrolledWindow* scrollwin = Builder("scrollwin:display");
	m_hadjustment = scrollwin->get_hadjustment();
	m_vadjustment = scrollwin->get_vadjustment();
	m_zoominbtn = Builder("tbbutton:main.zoomin");
	m_zoomoutbtn = Builder("tbbutton:main.zoomout");
	m_zoomonebtn = Builder("tbbutton:main.normsize");
	m_zoomfitbtn = Builder("tbbutton:main.bestfit");
	m_ocrstatelabel = Builder("label:main.recognize.state");
	m_rotspin = Builder("spin:main.rotate");
	m_pagespin = Builder("spin:display.page");
	m_resspin = Builder("spin:display.resolution");
	m_brispin = Builder("spin:display.brightness");
	m_conspin = Builder("spin:display.contrast");
	m_selmenu = Builder("window:selectionmenu");

	m_canvas->set_redraw_on_allocate(false);

	m_renderer = 0;
	clearImage(); // Assigns default values to all state variables
	selectionUpdateColors();

	m_connection_positionAndZoomCanvas = CONNECT(m_viewport, size_allocate, [this](Gdk::Rectangle&){ positionCanvas(true); });
	m_connection_saveHScrollMark = CONNECT(m_hadjustment, value_changed, [this]{ saveScrollMark(m_hadjustment, m_geo.sx); });
	m_connection_saveVScrollMark = CONNECT(m_vadjustment, value_changed, [this]{ saveScrollMark(m_vadjustment, m_geo.sy); });
	m_connection_pageSpinChanged = CONNECT(m_pagespin, value_changed, [this]{ clearSelections(); spinChanged(); });
	CONNECT(m_resspin, value_changed, [this]{ spinChanged(); });
	CONNECT(m_brispin, value_changed, [this]{ spinChanged(); });
	CONNECT(m_conspin, value_changed, [this]{ spinChanged(); });
	m_connection_mouseMove = CONNECT(m_viewport, motion_notify_event, [this](GdkEventMotion* ev){ mouseMove(ev); return true; });
	m_connection_mousePress = CONNECT(m_viewport, button_press_event, [this](GdkEventButton* ev){ mousePress(ev); return true; });
	CONNECT(m_viewport, scroll_event, [this](GdkEventScroll* ev){ return scrollZoom(ev); });
	CONNECT(m_canvas, draw, [this](const Cairo::RefPtr<Cairo::Context>& ctx){ drawCanvas(ctx); return false; });
	CONNECT(m_zoominbtn, clicked, [this]{ setZoom(ZoomMode::In); });
	CONNECT(m_zoomoutbtn, clicked, [this]{ setZoom(ZoomMode::Out); });
	CONNECT(m_zoomfitbtn, clicked, [this]{ setZoom(ZoomMode::Fit); });
	CONNECT(m_zoomonebtn, clicked, [this]{ setZoom(ZoomMode::One); });
	CONNECT(Builder("tbbutton:main.rotleft").as<Gtk::ToolButton>(), clicked,
			[this]{ m_rotspin->set_value((int(m_rotspin->get_value()*10.f + 900.f) % 3600) / 10.f); });
	CONNECT(Builder("tbbutton:main.rotright").as<Gtk::ToolButton>(), clicked,
			[this]{ m_rotspin->set_value((int(3600.f + m_rotspin->get_value()*10.f - 900.f) % 3600) / 10.f); });
	CONNECT(m_rotspin, value_changed, [this]{ rotate(); });
	CONNECT(Builder("tbbutton:main.autolayout").as<Gtk::ToolButton>(), clicked, [this]{ autodetectLayout(); });
	CONNECT(m_selmenu, button_press_event, [this](GdkEventButton* ev){
		Gtk::Allocation a = m_selmenu->get_allocation();
		if(ev->x < a.get_x() || ev->x > a.get_x() + a.get_width() || ev->y < a.get_y() || ev->y > a.get_y() + a.get_height()){
			hideSelectionMenu();
		}
		return true; });
	CONNECT(m_selmenu, key_press_event, [this](GdkEventKey* ev){
		if(ev->keyval == GDK_KEY_Escape) hideSelectionMenu(); return true;
	});

	CONNECT(Builder("window:main").as<Gtk::Window>()->get_style_context(), changed, [this]{ selectionUpdateColors(); });

	m_blurThread = Glib::Threads::Thread::create(sigc::mem_fun(this, &Displayer::blurThread));
}

Displayer::~Displayer()
{
	delete m_renderer;
	clearSelections();
	sendBlurRequest(BlurRequest::Quit, true);
}

void Displayer::blurThread()
{
	m_blurMutex.lock();
	while(true){
		m_blurThreadIdle = true;
		m_blurIdleCond.signal();
		while(!m_blurRequestPending){
			m_blurReqCond.wait(m_blurMutex);
		}
		m_blurRequestPending = false;
		if(m_blurRequest.action == BlurRequest::Quit){
			m_blurIdleCond.signal();
			break;
		}else if(m_blurRequest.action == BlurRequest::Stop){
			continue;
		}
		BlurRequest req = m_blurRequest;
		m_blurThreadIdle = false;
		m_blurMutex.unlock();
#define CHECK_PENDING m_blurMutex.lock(); if(m_blurRequestPending) continue; m_blurMutex.unlock();
		Cairo::RefPtr<Cairo::ImageSurface> blurred = m_renderer->render(req.page, req.res);
		CHECK_PENDING
		Manipulators::adjustBrightness(blurred, req.brightness);
		CHECK_PENDING
		Manipulators::adjustContrast(blurred, req.contrast);
		CHECK_PENDING
		Glib::signal_idle().connect_once([=]{
			if(!m_blurRequestPending){
				m_blurImage = blurred;
				m_canvas->queue_draw();
			}
		});
		m_blurMutex.lock();
	};
	m_blurMutex.unlock();
}

void Displayer::sendBlurRequest(BlurRequest::Action action, bool wait)
{
	m_blurImage.clear();
	BlurRequest request = {action};
	if(request.action == BlurRequest::Start){
		request.res = m_resspin->get_value() * m_geo.s;
		request.page = m_pagespin->get_value_as_int();
		request.brightness = m_brispin->get_value_as_int();
		request.contrast = m_conspin->get_value_as_int();
	}
	m_blurMutex.lock();
	m_blurRequest = request;
	m_blurRequestPending = true;
	m_blurReqCond.signal();
	m_blurMutex.unlock();
	if(wait){
		m_blurMutex.lock();
		while(!m_blurThreadIdle){
			m_blurIdleCond.wait(m_blurMutex);
		}
		m_blurMutex.unlock();
	}
	if(action == BlurRequest::Quit){
		m_blurThread->join();
	}
}

void Displayer::drawCanvas(const Cairo::RefPtr<Cairo::Context> &ctx)
{
	if(!m_image){
		return;
	}
	Gtk::Allocation alloc = m_canvas->get_allocation();
	ctx->save();
	// Set up transformations
	ctx->translate(0.5 * alloc.get_width(), 0.5 * alloc.get_height());
	ctx->rotate(m_geo.a);
//	ctx->scale(m_geo.s, m_geo.s);
//	ctx->translate(-0.5 * m_image->get_width(), -0.5 * m_image->get_height());
	// Set source and apply all transformations to it
	if(!m_blurImage){
		ctx->scale(m_geo.s, m_geo.s);
		ctx->translate(-0.5 * m_image->get_width(), -0.5 * m_image->get_height());
		ctx->set_source(m_image, 0, 0);
	}else{
		ctx->translate(-0.5 * m_blurImage->get_width(), -0.5 * m_blurImage->get_height());
		ctx->set_source(m_blurImage, 0, 0);
	}
//	ctx->set_source(m_image, 0, 0);
//	Cairo::RefPtr<Cairo::SurfacePattern>::cast_dynamic(ctx->get_source())->set_filter(Cairo::FILTER_BEST);
	ctx->paint();
	// Draw selections
	ctx->restore();
	ctx->translate(Utils::round(0.5 * alloc.get_width()), Utils::round(0.5 * alloc.get_height()));
	ctx->scale(m_geo.s, m_geo.s);
	int i = 0;
	for(const DisplaySelection* sel : m_selections){
		sel->draw(ctx, m_geo.s, Glib::ustring::compose("%1", ++i), m_selColors);
	}
	return;
}

void Displayer::positionCanvas(bool zoom)
{
	if(!m_image){
		return;
	}
	double bbw, bbh;
	getBBSize(bbw, bbh);
	m_canvas->get_window()->freeze_updates();
	m_connection_positionAndZoomCanvas.block();
	m_connection_saveHScrollMark.block();
	m_connection_saveVScrollMark.block();
	if(zoom && m_zoomFit){
		m_geo.s = std::min(m_viewport->get_allocated_width() / bbw, m_viewport->get_allocated_height() / bbh);
	}
	m_canvas->set_size_request(Utils::round(bbw * m_geo.s), Utils::round(bbh * m_geo.s));
	// Immediately resize the children
	m_viewport->size_allocate(m_viewport->get_allocation());
	m_viewport->set_allocation(m_viewport->get_allocation());
	// Repeat to cover cases where scrollbars did disappear
	if(zoom && m_zoomFit){
		m_geo.s = std::min(m_viewport->get_allocated_width() / bbw, m_viewport->get_allocated_height() / bbh);
		sendBlurRequest(BlurRequest::Start);
		m_canvas->set_size_request(Utils::round(bbw * m_geo.s), Utils::round(bbh * m_geo.s));
		m_viewport->size_allocate(m_viewport->get_allocation());
		m_viewport->set_allocation(m_viewport->get_allocation());
	}
	m_hadjustment->set_value(m_geo.sx *(m_hadjustment->get_upper() - m_hadjustment->get_page_size()));
	m_vadjustment->set_value(m_geo.sy *(m_vadjustment->get_upper() - m_vadjustment->get_page_size()));
	m_canvas->queue_draw();
	m_connection_positionAndZoomCanvas.unblock();
	m_connection_saveHScrollMark.unblock();
	m_connection_saveVScrollMark.unblock();
	m_canvas->get_window()->thaw_updates();
}

bool Displayer::scrollZoom(GdkEventScroll *ev)
{
	if((ev->state & Gdk::CONTROL_MASK) != 0){
		if(ev->direction == GDK_SCROLL_UP && m_geo.s * 1.25 < 10){
			Gtk::Allocation alloc = m_canvas->get_allocation();
			m_geo.sx = std::max(0., std::min((ev->x + m_hadjustment->get_value() - alloc.get_x())/alloc.get_width(), 1.0));
			m_geo.sy = std::max(0., std::min((ev->y + m_vadjustment->get_value() - alloc.get_y())/alloc.get_height(), 1.0));
			setZoom(ZoomMode::In);
		}else if(ev->direction == GDK_SCROLL_DOWN && m_geo.s * 0.8 > 0.05){
			setZoom(ZoomMode::Out);
		}
		return true;
	}else if((ev->state & Gdk::SHIFT_MASK) != 0){
		if(ev->direction == GDK_SCROLL_UP){
			m_hadjustment->set_value(m_hadjustment->get_value() - m_hadjustment->get_step_increment());
		}else if(ev->direction == GDK_SCROLL_DOWN){
			m_hadjustment->set_value(m_hadjustment->get_value() + m_hadjustment->get_step_increment());
		}
		return true;
	}
	return false;
}

void Displayer::saveScrollMark(Glib::RefPtr<Gtk::Adjustment> adj, double &s)
{
	double den = adj->get_upper() - adj->get_page_size();
	s = std::abs(den) > 1E-4 ? adj->get_value() / den : 0.5;
}

void Displayer::setZoom(ZoomMode zoom)
{
	for(Gtk::Widget* w : {m_zoominbtn, m_zoomoutbtn, m_zoomonebtn, m_zoomfitbtn}){
		w->set_sensitive(true);
	}
	m_zoomFit = false;
	Gtk::Allocation alloc = m_viewport->get_allocation();
	double bbw, bbh;
	getBBSize(bbw, bbh);
	double fit = std::min(alloc.get_width() / bbw, alloc.get_height() / bbh);
	if(zoom == ZoomMode::In){
		m_geo.s = std::min(10., m_geo.s * 1.25);
	}else if(zoom == ZoomMode::Out){
		m_geo.s = std::max(0.05, m_geo.s * 0.8);
	}else if(zoom == ZoomMode::One){
		m_geo.s = 1.0;
	}
	if(zoom == ZoomMode::Fit || (m_geo.s / fit >= 0.9 && m_geo.s / fit <= 1.09)){
		m_zoomFit = true;
		m_geo.s = fit;
		m_zoomfitbtn->set_sensitive(false);
	}
	m_zoomoutbtn->set_sensitive(m_geo.s > 0.05);
	m_zoominbtn->set_sensitive(m_geo.s < 10.);
	m_zoomonebtn->set_sensitive(m_geo.s != 1.);
	sendBlurRequest(BlurRequest::Start);
	positionCanvas();
}

void Displayer::getBBSize(double& w, double& h) const
{
	int iw = m_image->get_width();
	int ih = m_image->get_height();
	double Rex[] = {m_geo.R(0, 0) * iw, m_geo.R(1, 0) * iw};
	double Rey[] = {m_geo.R(0, 1) * ih, m_geo.R(1, 1) * ih};
	w = std::abs(Rex[0]) + std::abs(Rey[0]);
	h = std::abs(Rey[1]) + std::abs(Rex[1]);
}

void Displayer::rotate()
{
	if(!m_renderer){
		return;
	}
	double angle = -m_rotspin->get_value() * 0.0174532925199;
	double delta = angle - m_geo.a;
	m_geo.a = angle;
	m_geo.R = Geometry::Rotation(angle);
	Geometry::Rotation deltaR(delta);
	for(DisplaySelection* sel : m_selections){
		sel->rotate(deltaR);
	}
	positionCanvas(true);
}

void Displayer::spinChanged()
{
	if(m_renderer){
		m_timer.disconnect();
		m_timer = Glib::signal_timeout().connect([this]{
			if(setImage()){ rotate(); }
			return false;
		}, 200);
	}
}

bool Displayer::setSource(const std::string &filename)
{
	if(m_image){
		MAIN->popState();
		clearImage();
	}
	if(filename.empty()){
		return false;
	}
#ifdef G_OS_WIN32
	if(Glib::ustring(filename.substr(filename.length() - 4)).lowercase() == ".pdf"){
#else
	if(Utils::get_content_type(filename) == "application/pdf"){
#endif
		DisplayRenderer* renderer = new PDFRenderer(filename);
		Utils::configure_spin(m_resspin, 300, 50, 600, 50, 100);
		Utils::configure_spin(m_pagespin, 1, 1, renderer->getNPages(), 1, 10);
		m_resspin->set_tooltip_text(_("Resolution"));
		m_pagespin->show();
		m_renderer = renderer;
	}else{
		Utils::configure_spin(m_resspin, 100, 50, 200, 10, 50);
		Utils::configure_spin(m_pagespin, 1, 1, 1, 1, 1);
		m_resspin->set_tooltip_text(_("Scale"));
		m_pagespin->hide();
		m_renderer = new ImageRenderer(filename);
	}
	if(setImage()){
		MAIN->pushState(MainWindow::State::Normal, _("To recognize specific areas, drag rectangles over them."));
		m_canvas->show();
		m_viewport->get_window()->set_cursor(Gdk::Cursor::create(Gdk::TCROSS));
		rotate();
		return true;
	}else{
		Utils::error_dialog(_("Failed to load image"), Glib::ustring::compose(_("The file might not be an image or be corrupt:\n%1"), filename));
		m_image = Cairo::RefPtr<Cairo::ImageSurface>();
		return false;
	}
}

bool Displayer::setCurrentPage(int page)
{
	m_connection_pageSpinChanged.block();
	m_pagespin->set_value(page);
	bool success = setImage();
	m_connection_pageSpinChanged.unblock();
	return success;
}

bool Displayer::setImage()
{
	if(!m_renderer){
		return false;
	}
	int page = m_pagespin->get_value_as_int();
	double res = m_resspin->get_value();
	int bri = m_brispin->get_value_as_int();
	int con = m_conspin->get_value_as_int();
	Cairo::RefPtr<Cairo::ImageSurface> image;
	sendBlurRequest(BlurRequest::Stop, true);
	bool success = Utils::busyTask([this, page, res, bri, con, &image] {
		image = m_renderer->render(page, res);
		Manipulators::adjustBrightness(image, bri);
		Manipulators::adjustContrast(image, con);
		return bool(image);
	}, _("Rendering image..."));
	m_image = image;
	sendBlurRequest(BlurRequest::Start);
	return success;
}

void Displayer::clearImage()
{
	m_geo = {0.0, 0.5, 0.5, 1.0, Geometry::Rotation(0)};
	m_image = Cairo::RefPtr<Cairo::ImageSurface>();
	delete m_renderer;
	m_renderer = 0;
	clearSelections();
	m_zoomFit = true;
	m_canvas->hide();
	if(m_viewport->get_window()){
		m_viewport->get_window()->set_cursor();
	}
	m_brispin->set_value(0);
	m_conspin->set_value(0);
	m_rotspin->set_value(0);
}

Geometry::Point Displayer::getSelectionCoords(double evx, double evy) const
{
	// Selection coordinates are with respect to the center of the image in unscaled (but rotate) coordinates
	Gtk::Allocation alloc = m_canvas->get_allocation();
	double x = (std::max(0., std::min(evx - alloc.get_x(), double(alloc.get_width()))) - 0.5 * alloc.get_width()) / m_geo.s;
	double y = (std::max(0., std::min(evy - alloc.get_y(), double(alloc.get_height()))) - 0.5 * alloc.get_height()) / m_geo.s;
	return Geometry::Point(x, y);
}

Geometry::Rectangle Displayer::getSelectionDamageArea(const Geometry::Rectangle &rect) const
{
	Gtk::Allocation alloc = m_canvas->get_allocation();
	int x = std::floor(rect.x * m_geo.s + 0.5 * alloc.get_width()) - 1;
	int y = std::floor(rect.y * m_geo.s + 0.5 * alloc.get_height()) - 1;
	int w = std::ceil(rect.width * m_geo.s) + 3;
	int h = std::ceil(rect.height * m_geo.s) + 3;
	return Geometry::Rectangle(x, y, w, h);
}

void Displayer::mouseMove(GdkEventMotion *ev)
{
	if(!m_image){
		return;
	}
	Geometry::Point point = getSelectionCoords(ev->x, ev->y);
	for(DisplaySelection* sel : Utils::reverse(m_selections)){
		m_curSel = sel->getResizeHandle(point, m_geo.s);
		if(m_curSel != nullptr){
			m_viewport->get_window()->set_cursor(Gdk::Cursor::create(m_curSel->getResizeCursor()));
			return;
		}
	}
	m_viewport->get_window()->set_cursor(Gdk::Cursor::create(Gdk::TCROSS));
}

void Displayer::mousePress(GdkEventButton* ev)
{
	if(!m_image){
		return;
	}
	Geometry::Point point = getSelectionCoords(ev->x, ev->y);
	if(ev->button == 3){
		for(int i = m_selections.size() - 1; i >= 0; --i){
			if(m_selections[i]->getRect().contains(point)){
				showSelectionMenu(ev, i);
				return;
			}
		}
	}
	m_connection_mouseMove.block();
	m_connection_mousePress.block();
	if(m_curSel == nullptr){
		if((ev->state & Gdk::CONTROL_MASK) == 0){
			// Clear all existing selections
			clearSelections();
			m_canvas->queue_draw();
		}
		// Start new selection
		DisplaySelection* sel = new DisplaySelection(point);
		m_curSel = new DisplaySelectionHandle(sel);
		m_selections.insert(m_selections.end(), sel);
	}
	m_connection_selDo = CONNECT(m_viewport, motion_notify_event, [this](GdkEventMotion* ev){ selectionDo(ev); return true; });
	m_connection_selEnd = CONNECT(m_viewport, button_release_event, [this](GdkEventButton* ev){ selectionEnd(); return true; });
}

void Displayer::selectionDo(GdkEventMotion* ev)
{
	assert(m_curSel != nullptr);
	Geometry::Point point = getSelectionCoords(ev->x, ev->y);
	m_viewport->get_window()->set_cursor(Gdk::Cursor::create(m_curSel->getResizeCursor()));
	Geometry::Rectangle damage = getSelectionDamageArea(m_curSel->setPoint(point));
	m_canvas->queue_draw_area(damage.x, damage.y, damage.width, damage.height);

	m_scrollspeed[0] = m_scrollspeed[1] = 0;
	if(ev->x < m_hadjustment->get_value()){
		m_scrollspeed[0] = ev->x - m_hadjustment->get_value();
	}else if(ev->x > m_hadjustment->get_value() + m_hadjustment->get_page_size()){
		m_scrollspeed[0] = ev->x - m_hadjustment->get_value() - m_hadjustment->get_page_size();
	}
	if(ev->y < m_vadjustment->get_value()){
		m_scrollspeed[1] = ev->y - m_vadjustment->get_value();
	}else if(ev->y > m_vadjustment->get_value() + m_vadjustment->get_page_size()){
		m_scrollspeed[1] = ev->y - m_vadjustment->get_value() - m_vadjustment->get_page_size();
	}
	if((m_scrollspeed[0] != 0 || m_scrollspeed[1] != 0) && !m_timer.connected()){
		m_timer = Glib::signal_timeout().connect([this]{ return scroll(); }, 25);
	}
}

void Displayer::selectionEnd()
{
	assert(m_curSel != nullptr);
	if(m_curSel->getSelection()->isEmpty()){
		selectionRemove(m_curSel->getSelection());
	}
	delete m_curSel;
	m_curSel = nullptr;
	m_timer.disconnect();
	m_connection_selDo.disconnect();
	m_connection_selEnd.disconnect();
	m_connection_mouseMove.unblock();
	m_connection_mousePress.unblock();
	if(!m_selections.empty()){
		m_ocrstatelabel->set_markup(Glib::ustring::compose("<small>%1</small>", _("Recognize selection")));
	}else{
		m_ocrstatelabel->set_markup(Glib::ustring::compose("<small>%1</small>", _("Recognize all")));
	}
}

void Displayer::clearSelections()
{
	for(const DisplaySelection* sel : m_selections){ delete sel; }
	m_selections.clear();
}

bool Displayer::scroll()
{
	if(m_scrollspeed[0] == 0 && m_scrollspeed[1] == 0){
		return false;
	}
	m_geo.sx = std::min(std::max(0., m_geo.sx + m_scrollspeed[0] / 2000.), 1.);
	m_geo.sy = std::min(std::max(0., m_geo.sy + m_scrollspeed[1] / 2000.), 1.);
	positionCanvas();
	return true;
}

void Displayer::showSelectionMenu(GdkEventButton *ev, int i)
{
	DisplaySelection *sel = m_selections[i];
	Gtk::SpinButton* spin = Builder("spin:selectionmenu.order");
	spin->get_adjustment()->set_upper(m_selections.size());
	spin->get_adjustment()->set_value(i + 1);
	m_connection_selmenu_reorder = CONNECT(spin, value_changed, [this,spin,sel]{
		m_selections.erase(std::find(m_selections.begin(), m_selections.end(), sel));
		m_selections.insert(m_selections.begin() + spin->get_value_as_int() - 1, sel);
		m_canvas->queue_draw();
	});
	m_connection_selmenu_delete = CONNECT(Builder("button:selectionmenu.delete").as<Gtk::Button>(), clicked, [this,sel]{
		hideSelectionMenu();
		selectionRemove(sel);
	});
	m_connection_selmenu_recognize = CONNECT(Builder("button:selectionmenu.recognize").as<Gtk::Button>(), clicked, [this, sel]{
		hideSelectionMenu();
		MAIN->getRecognizer()->recognizeImage(getTransformedImage(sel->getRect()), Recognizer::OutputDestination::Buffer);
	});
	m_connection_selmenu_clipboard = CONNECT(Builder("button:selectionmenu.clipboard").as<Gtk::Button>(), clicked, [this, sel]{
		hideSelectionMenu();
		MAIN->getRecognizer()->recognizeImage(getTransformedImage(sel->getRect()), Recognizer::OutputDestination::Clipboard);
	});
	m_connection_selmenu_save = CONNECT(Builder("button:selectionmenu.save").as<Gtk::Button>(), clicked, [this, sel]{
		hideSelectionMenu();
		saveSelection(sel->getRect());
	});
	Glib::RefPtr<const Gdk::Screen> screen = MAIN->getWindow()->get_screen();
	Gdk::Rectangle rect = screen->get_monitor_workarea(screen->get_monitor_at_point(ev->x_root, ev->y_root));
	m_selmenu->show_all();
	int w, h, trash;
	m_selmenu->get_preferred_width(trash, w);
	m_selmenu->get_preferred_height(trash, h);
	int x = std::min(std::max(int(ev->x_root), rect.get_x()), rect.get_x() + rect.get_width() - w);
	int y = std::min(std::max(int(ev->y_root), rect.get_y()), rect.get_y() + rect.get_height() - h);
	m_selmenu->move(x, y);
	GdkWindow* gdkwin = m_selmenu->get_window()->gobj();
	gdk_device_grab(gtk_get_current_event_device(), gdkwin, GDK_OWNERSHIP_APPLICATION, true, GDK_BUTTON_PRESS_MASK, nullptr, ev->time);
}

void Displayer::hideSelectionMenu()
{
	m_selmenu->hide();
	m_connection_selmenu_reorder.disconnect();
	m_connection_selmenu_delete.disconnect();
	m_connection_selmenu_recognize.disconnect();
	m_connection_selmenu_clipboard.disconnect();
	m_connection_selmenu_save.disconnect();
	gdk_device_ungrab(gtk_get_current_event_device(), gtk_get_current_event_time());
}

void Displayer::selectionRemove(const DisplaySelection* sel)
{
	m_selections.erase(std::find(m_selections.begin(), m_selections.end(), sel));
	delete sel;
	m_canvas->queue_draw();
}

void Displayer::selectionUpdateColors()
{
	m_selColors[0] = Gtk::Entry().get_style_context()->get_color(Gtk::STATE_FLAG_SELECTED);
	m_selColors[1] = Gtk::Entry().get_style_context()->get_background_color(Gtk::STATE_FLAG_SELECTED);
}

Cairo::RefPtr<Cairo::ImageSurface> Displayer::getTransformedImage(const Geometry::Rectangle &rect) const
{
	Cairo::RefPtr<Cairo::ImageSurface> surf = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, std::ceil(rect.width), std::ceil(rect.height));
		Cairo::RefPtr<Cairo::Context> ctx = Cairo::Context::create(surf);
		ctx->set_source_rgba(1., 1., 1., 1.);
		ctx->paint();
		ctx->translate(-rect.x, -rect.y);
		ctx->rotate(m_geo.a);
		ctx->translate(-0.5 * m_image->get_width(), -0.5 * m_image->get_height());
		ctx->set_source(m_image, 0, 0);
		ctx->paint();
		return surf;
}

std::vector<Cairo::RefPtr<Cairo::ImageSurface>> Displayer::getSelections() const
{
	std::vector<Geometry::Rectangle> rects;
	if(m_selections.empty()){
		double bbw, bbh;
		getBBSize(bbw, bbh);
		rects.push_back(Geometry::Rectangle(-0.5 * bbw, -0.5 * bbh, bbw, bbh));
	}else{
		rects.reserve(m_selections.size());
		for(const DisplaySelection* sel : m_selections){
			rects.push_back(sel->getRect());
		}
	}
	std::vector<Cairo::RefPtr<Cairo::ImageSurface>> images;
	for(const Geometry::Rectangle& rect : rects){
		images.push_back(getTransformedImage(rect));
	}
	return images;
}

void Displayer::saveSelection(const Geometry::Rectangle& rect) const
{
	Cairo::RefPtr<Cairo::ImageSurface> img = getTransformedImage(rect);
	std::string initialPath = Glib::build_filename(Glib::get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS), _("selection.png"));
	std::string filename = Utils::save_image_dialog(_("Save Selection Image"), initialPath);
	if(!filename.empty()){
		img->write_to_png(filename);
	}
}

void Displayer::autodetectLayout(bool rotated)
{
	clearSelections();

	float avgDeskew = 0.f;
	int nDeskew = 0;
	// Perform layout analysis
	Utils::busyTask([this,&nDeskew,&avgDeskew]{
		tesseract::TessBaseAPI tess;
		tess.InitForAnalysePage();
		tess.SetPageSegMode(tesseract::PSM_AUTO_ONLY);
		double bbw, bbh;
		getBBSize(bbw, bbh);
		Geometry::Rectangle rect = Geometry::Rectangle(-0.5 * bbw, -0.5 * bbh, bbw, bbh);
		Cairo::RefPtr<Cairo::ImageSurface> img = getTransformedImage(rect);
		tess.SetImage(img->get_data(), img->get_width(), img->get_height(), 4, img->get_stride());
		tesseract::PageIterator* it = tess.AnalyseLayout();
		if(it && !it->Empty(tesseract::RIL_BLOCK)){
			std::vector<Geometry::Rectangle> rects;
			do{
				int x1, y1, x2, y2;
				tesseract::Orientation orient;
				tesseract::WritingDirection wdir;
				tesseract::TextlineOrder tlo;
				float deskew;
				it->BoundingBox(tesseract::RIL_BLOCK, &x1, &y1, &x2, &y2);
				it->Orientation(&orient, &wdir, &tlo, &deskew);
				avgDeskew += deskew;
				++nDeskew;
				if(x2-x1 > 10 && y2-y1 > 10){
					rects.push_back(Geometry::Rectangle(x1 - img->get_width()*.5, y1 - img->get_height()*.5, x2-x1, y2-y1));
				}
			}while(it->Next(tesseract::RIL_BLOCK));

			// Merge overlapping rectangles
			for(unsigned i = rects.size() - 1; i-- > 1;) {
				for(unsigned j = i - 1; j-- > 0;) {
					if(rects[j].overlaps(rects[i])) {
						rects[j] = rects[j].unite(rects[i]);
						rects.pop_back();
						break;
					}
				}
			}
			for(const Geometry::Rectangle& rect : rects) {
				m_selections.push_back(new DisplaySelection(rect.x, rect.y, rect.width, rect.height));
			}
		}
		delete it;
		return true;
	}, _("Performing layout analysis"));

	// If a somewhat large deskew angle is detected, automatically rotate image and redetect layout,
	// unless we already attempted to rotate (to prevent endless loops)
	avgDeskew = (avgDeskew/nDeskew)/M_PI * 180.f;
	if(std::abs(avgDeskew > 1.f) && !rotated){
		m_rotspin->set_value((int((m_rotspin->get_value() + avgDeskew) * 10.f) % 3600) / 10.f);
		while(Gtk::Main::events_pending()){ // Wait for rotate to occur
			Gtk::Main::iteration();
		}
		autodetectLayout(true);
	}else{
		m_canvas->queue_draw();
		if(!m_selections.empty()){
			m_ocrstatelabel->set_markup(Glib::ustring::compose("<small>%1</small>", _("Recognize selection")));
		}
	}
}
