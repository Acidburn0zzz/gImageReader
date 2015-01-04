/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Displayer.cc
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

#include "MainWindow.hh"
#include "Displayer.hh"
#include "DisplayRenderer.hh"
#include "DisplaySelection.hh"
#include "SourceManager.hh"
#include "Utils.hh"

#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <tesseract/baseapi.h>
#include <cassert>


Displayer::Displayer(const UI_MainWindow& _ui, QWidget* parent)
	: QGraphicsView(parent), ui(_ui), m_scaleThread(std::bind(&Displayer::scaleThread, this))
{
	setScene(&m_scene);
	setBackgroundBrush(Qt::gray);
	setRenderHint(QPainter::Antialiasing);

	m_renderTimer.setSingleShot(true);
	m_scaleTimer.setSingleShot(true);

	ui.actionRotateLeft->setData(270.);
	ui.actionRotateRight->setData(90.);

	connect(ui.actionRotateLeft, SIGNAL(triggered()), this, SLOT(rotate90()));
	connect(ui.actionRotateRight, SIGNAL(triggered()), this, SLOT(rotate90()));
	connect(ui.actionAutodetectLayout, SIGNAL(triggered()), this, SLOT(autodetectLayout()));
	connect(ui.spinBoxRotation, SIGNAL(valueChanged(double)), this, SLOT(setRotation(double)));
	connect(ui.spinBoxPage, SIGNAL(valueChanged(int)), this, SLOT(queueRenderImage()));
	connect(ui.spinBoxBrightness, SIGNAL(valueChanged(int)), this, SLOT(queueRenderImage()));
	connect(ui.spinBoxContrast, SIGNAL(valueChanged(int)), this, SLOT(queueRenderImage()));
	connect(ui.spinBoxResolution, SIGNAL(valueChanged(int)), this, SLOT(queueRenderImage()));
	connect(ui.checkBoxInvertColors, SIGNAL(toggled(bool)), this, SLOT(queueRenderImage()));
	connect(ui.actionZoomIn, SIGNAL(triggered()), this, SLOT(zoomIn()));
	connect(ui.actionZoomOut, SIGNAL(triggered()), this, SLOT(zoomOut()));
	connect(ui.actionBestFit, SIGNAL(triggered()), this, SLOT(zoomFit()));
	connect(ui.actionOriginalSize, SIGNAL(triggered()), this, SLOT(zoomOriginal()));
	connect(&m_renderTimer, SIGNAL(timeout()), this, SLOT(renderImage()));
	connect(&m_scaleTimer, SIGNAL(timeout()), this, SLOT(scaleTimerElapsed()));

	MAIN->getConfig()->addSetting(new VarSetting<QString>("selectionsavefile", QDir(Utils::documentsFolder()).absoluteFilePath(_("selection.png"))));
}

bool Displayer::setCurrentPage(int page)
{
	Utils::setSpinBlocked(ui.spinBoxPage, page);
	return renderImage();
}

int Displayer::getCurrentPage() const
{
	return ui.spinBoxPage->value();
}

int Displayer::getNPages() const
{
	return ui.spinBoxPage->maximum();
}

bool Displayer::setSource(Source* source)
{
	m_scaleTimer.stop();
	if(m_scaleThread.isRunning()){
		sendScaleRequest({ScaleRequest::Quit});
		m_scaleThread.wait();
	}
	clearSelections();
	m_renderTimer.stop();
	m_scene.clear();
	delete m_renderer;
	m_renderer = nullptr;
	m_pixmap = QPixmap();
	m_imageItem = nullptr;
	ui.actionBestFit->setChecked(true);
	ui.spinBoxPage->setEnabled(false);
	ui.spinBoxPage->setValue(1);
	ui.spinBoxRotation->setValue(0.);
	ui.spinBoxBrightness->setValue(0);
	ui.spinBoxContrast->setValue(0);
	ui.spinBoxResolution->setValue(100);
	ui.checkBoxInvertColors->setChecked(false);
	ui.actionBestFit->setChecked(true);
	ui.actionOriginalSize->setChecked(false);
	ui.actionZoomIn->setEnabled(true);
	ui.actionZoomOut->setEnabled(true);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	unsetCursor();

	m_source = source;

	if(!m_source){
		return false;
	}

	if(source->path.endsWith(".pdf", Qt::CaseInsensitive)){
		m_renderer = new PDFRenderer(source->path);
		if(source->resolution == -1) source->resolution = 300;
	}else{
		m_renderer = new ImageRenderer(source->path);
		if(source->resolution == -1) source->resolution = 100;
	}
	ui.spinBoxPage->setMaximum(m_renderer->getNPages());
	ui.actionPage->setVisible(m_renderer->getNPages() > 1);
	Utils::setSpinBlocked(ui.spinBoxRotation, source->angle);
	Utils::setSpinBlocked(ui.spinBoxPage, source->page);
	Utils::setSpinBlocked(ui.spinBoxBrightness, source->brightness);
	Utils::setSpinBlocked(ui.spinBoxContrast, source->contrast);
	Utils::setSpinBlocked(ui.spinBoxResolution, source->resolution);
	ui.checkBoxInvertColors->blockSignals(true);
	ui.checkBoxInvertColors->setChecked(source->invert);
	ui.checkBoxInvertColors->blockSignals(false);
	setCursor(Qt::CrossCursor);

	m_imageItem = new QGraphicsPixmapItem();
	m_imageItem->setTransformationMode(Qt::SmoothTransformation);
	m_scene.addItem(m_imageItem);
	m_scaleThread.start();
	if(!renderImage()){
		setSource(nullptr);
		QMessageBox::critical(this, _("Failed to load image"), _("The file might not be an image or be corrupt:\n%1").arg(source->path));
		return false;
	}
	return true;
}

bool Displayer::renderImage()
{
	sendScaleRequest({ScaleRequest::Abort});
	if(m_source->resolution != ui.spinBoxResolution->value()){
		double factor = double(ui.spinBoxResolution->value()) / double(m_source->resolution);
		for(DisplaySelection* sel : m_selections){
			sel->scale(factor);
		}
	}
	m_source->page = ui.spinBoxPage->value();
	m_source->brightness = ui.spinBoxBrightness->value();
	m_source->contrast = ui.spinBoxContrast->value();
	m_source->resolution = ui.spinBoxResolution->value();
	m_source->invert = ui.checkBoxInvertColors->isChecked();
	QImage image = m_renderer->render(m_source->page, m_source->resolution);
	if(image.isNull()){
		return false;
	}
	m_renderer->adjustImage(image, m_source->brightness, m_source->contrast, m_source->invert);
	m_pixmap = QPixmap::fromImage(image);
	m_imageItem->setPixmap(m_pixmap);
	m_imageItem->setScale(1.);
	m_imageItem->setTransformOriginPoint(m_imageItem->boundingRect().center());
	m_imageItem->setPos(m_imageItem->pos() - m_imageItem->sceneBoundingRect().center());
	m_scene.setSceneRect(m_imageItem->sceneBoundingRect());
	centerOn(sceneRect().center());
	setRotation(ui.spinBoxRotation->value());
	if(m_scale < 1.0){
		m_pendingScaleRequest = {ScaleRequest::Scale, m_scale, m_source->resolution, m_source->page, m_source->brightness, m_source->contrast, m_source->invert};
		m_scaleTimer.start(100);
	}
	return true;
}

void Displayer::setZoom(Zoom action, ViewportAnchor anchor)
{
	if(!m_imageItem){
		return;
	}
	sendScaleRequest({ScaleRequest::Abort});

	QRectF bb = m_imageItem->sceneBoundingRect();
	double fit = qMin(viewport()->width() / bb.width(), viewport()->height() / bb.height());

	if(action == Zoom::Original){
		m_scale = 1.0;
	}else if(action == Zoom::In){
		m_scale = qMin(10., m_scale * 1.25);
	}else if(action == Zoom::Out){
		m_scale = qMax(0.05, m_scale * 0.8);
	}
	ui.actionBestFit->setChecked(false);
	if(action == Zoom::Fit || (m_scale / fit >= 0.9 && m_scale / fit <= 1.09)){
		m_scale = fit;
		ui.actionBestFit->setChecked(true);
	}
	Qt::ScrollBarPolicy scrollPolicy = m_scale <= fit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
	setHorizontalScrollBarPolicy(scrollPolicy);
	setVerticalScrollBarPolicy(scrollPolicy);
	ui.actionOriginalSize->setChecked(m_scale == 1.0);
	ui.actionZoomIn->setEnabled(m_scale < 10);
	ui.actionZoomOut->setEnabled(m_scale > 0.01);
	setTransformationAnchor(anchor);
	QTransform t;
	t.scale(m_scale, m_scale);
	setTransform(t);
	if(m_scale < 1.0){
		m_pendingScaleRequest = {ScaleRequest::Scale, m_scale, m_source->resolution, m_source->page, m_source->brightness, m_source->contrast, m_source->invert};
		m_scaleTimer.start(100);
	}else{
		m_imageItem->setPixmap(m_pixmap);
		m_imageItem->setScale(1.);
		m_imageItem->setTransformOriginPoint(m_imageItem->boundingRect().center());
		m_imageItem->setPos(m_imageItem->pos() - m_imageItem->sceneBoundingRect().center());
	}
}

void Displayer::setRotation(double angle)
{
	if(m_imageItem){
		angle = angle < 0 ? angle + 360. : angle >= 360 ? angle - 360 : angle,
		Utils::setSpinBlocked(ui.spinBoxRotation, angle);
		double delta = angle - m_source->angle;
		m_source->angle = angle;
		m_imageItem->setRotation(angle);
		QTransform t;
		t.rotate(delta);
		for(DisplaySelection* sel : m_selections){
			sel->rotate(t);
		}
		m_scene.setSceneRect(m_imageItem->sceneBoundingRect());
		if(ui.actionBestFit->isChecked()){
			setZoom(Zoom::Fit);
		}
	}
}

void Displayer::queueRenderImage()
{
	if(m_renderer){
		m_renderTimer.start(500);
	}
}

void Displayer::resizeEvent(QResizeEvent *event)
{
	QGraphicsView::resizeEvent(event);
	if(ui.actionBestFit->isChecked()){
		setZoom(Zoom::Fit);
	}
}

void Displayer::mousePressEvent(QMouseEvent *event)
{
	event->ignore();
	QGraphicsView::mousePressEvent(event);
	if(!event->isAccepted() && event->button() == Qt::LeftButton && m_source && m_curSel == nullptr){
		if((event->modifiers() & Qt::ControlModifier) == 0){
			clearSelections();
		}
		m_curSel = new DisplaySelection(this, 1 + m_selections.size(), mapToSceneClamped(event->pos()));
		m_curSel->setZValue(1 + m_selections.size());
		m_scene.addItem(m_curSel);
		event->accept();
	}
}

void Displayer::mouseMoveEvent(QMouseEvent *event)
{
	event->ignore();
	QGraphicsView::mouseMoveEvent(event);
	if(!event->isAccepted() && m_curSel){
		QPointF p = mapToSceneClamped(event->pos());
		m_curSel->setPoint(p);
		ensureVisible(QRectF(p, p));
		event->accept();
	}
}

void Displayer::mouseReleaseEvent(QMouseEvent *event)
{
	event->ignore();
	QGraphicsView::mouseReleaseEvent(event);
	if(!event->isAccepted() && m_curSel){
		if(m_curSel->rect().width() < 5. || m_curSel->rect().height() < 5.){
			delete m_curSel;
		}else{
			m_selections.append(m_curSel);
			emit selectionChanged(true);
		}
		m_curSel = nullptr;
		event->accept();
	}
}

void Displayer::wheelEvent(QWheelEvent *event)
{
	if(event->modifiers() & Qt::ControlModifier){
		setZoom(event->delta() > 0 ? Zoom::In : Zoom::Out, QGraphicsView::AnchorUnderMouse);
		event->accept();
	}else if(event->modifiers() & Qt::ShiftModifier){
		QScrollBar* hscroll = horizontalScrollBar();
		if(event->delta() < 0){
			hscroll->setValue(hscroll->value() + hscroll->singleStep());
		}else{
			hscroll->setValue(hscroll->value() - hscroll->singleStep());
		}
		event->accept();
	}else{
		QGraphicsView::wheelEvent(event);
	}
}

QPointF Displayer::mapToSceneClamped(const QPoint &p) const
{
	QPointF q = mapToScene(p);
	QRectF bb = m_imageItem->sceneBoundingRect();
	q.rx() = qMin(qMax(bb.x(), q.x()), bb.x() + bb.width());
	q.ry() = qMin(qMax(bb.y(), q.y()), bb.y() + bb.height());
	return q;
}

void Displayer::rotate90()
{
	double angle = ui.spinBoxRotation->value() + qobject_cast<QAction*>(QObject::sender())->data().toDouble();
	ui.spinBoxRotation->setValue(angle >= 360. ? angle - 360. : angle);
}

void Displayer::clearSelections()
{
	qDeleteAll(m_selections);
	m_selections.clear();
	emit selectionChanged(false);
}

void Displayer::removeSelection(int num)
{
	delete m_selections[num - 1];
	m_selections.removeAt(num - 1);
	for(int i = 0, n = m_selections.size(); i < n; ++i){
		m_selections[i]->setNumber(1 + i);
		m_selections[i]->setZValue(1 + i);
		m_scene.invalidate(m_selections[i]->rect());
	}
}

void Displayer::reorderSelection(int oldNum, int newNum)
{
	DisplaySelection* sel = m_selections[oldNum - 1];
	m_selections.removeAt(oldNum - 1);
	m_selections.insert(newNum - 1, sel);
	for(int i = 0, n = m_selections.size(); i < n; ++i){
		m_selections[i]->setNumber(1 + i);
		m_selections[i]->setZValue(1 + i);
		m_scene.invalidate(m_selections[i]->rect());
	}
}

QList<QImage> Displayer::getSelections()
{
	QList<QImage> images;
	if(m_selections.empty()){
		images.append(getImage(m_imageItem->sceneBoundingRect()));
	}else{
		for(const DisplaySelection* sel : m_selections){
			images.append(getImage(sel->rect()));
		}
	}
	return images;
}

void Displayer::saveSelection(DisplaySelection *selection)
{
	QImage img = getImage(selection->rect());
	QString filename = Utils::makeOutputFilename(MAIN->getConfig()->getSetting<VarSetting<QString>>("selectionsavefile")->getValue());
	filename = QFileDialog::getSaveFileName(MAIN, _("Save Selection Image"), filename, QString("%1 (*.png)").arg(_("PNG Images")));
	if(!filename.isEmpty()){
		MAIN->getConfig()->getSetting<VarSetting<QString>>("selectionsavefile")->setValue(filename);
		img.save(filename);
	}
}

QImage Displayer::getImage(const QRectF& rect)
{
	QImage image(rect.width(), rect.height(), QImage::Format_RGB32);
	image.fill(Qt::black);
	QPainter painter(&image);
	QTransform t;
	t.translate(-rect.x(), -rect.y());
	t.rotate(ui.spinBoxRotation->value());
	t.translate(-0.5 * m_pixmap.width(), -0.5 * m_pixmap.height());
	painter.setTransform(t);
	painter.drawPixmap(0, 0, m_pixmap);
	return image;
}

void Displayer::autodetectLayout(bool rotated)
{
	clearSelections();

	double avgDeskew = 0.;
	int nDeskew = 0;
	QList<QRectF> rects;
	QImage img = getImage(m_imageItem->sceneBoundingRect());

	// Perform layout analysis
	Utils::busyTask([this,&nDeskew,&avgDeskew,&rects,&img]{
		tesseract::TessBaseAPI tess;
		tess.InitForAnalysePage();
		tess.SetPageSegMode(tesseract::PSM_AUTO_ONLY);
		tess.SetImage(img.bits(), img.width(), img.height(), 4, img.bytesPerLine());
		tesseract::PageIterator* it = tess.AnalyseLayout();
		if(it && !it->Empty(tesseract::RIL_BLOCK)){
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
				float width = x2 - x1, height = y2 - y1;
				if(width > 10 && height > 10){
					rects.append(QRectF(x1 - 0.5 * img.width(), y1 - 0.5 * img.height(), width, height));
				}
			}while(it->Next(tesseract::RIL_BLOCK));
		}
		delete it;
		return true;
	}, _("Performing layout analysis"));

	// If a somewhat large deskew angle is detected, automatically rotate image and redetect layout,
	// unless we already attempted to rotate (to prevent endless loops)
	avgDeskew = qRound(((avgDeskew/nDeskew)/M_PI * 180.) * 10.) / 10.;
	if(qAbs(avgDeskew > .1) && !rotated){
		setRotation(ui.spinBoxRotation->value() - avgDeskew);
		autodetectLayout(true);
	}else{
		// Merge overlapping rectangles
		for(int i = rects.size(); i-- > 1;) {
			for(int j = i; j-- > 0;) {
				if(rects[j].intersects(rects[i])) {
					rects[j] = rects[j].united(rects[i]);
					rects.removeAt(i);
					break;
				}
			}
		}
		for(int i = 0, n = rects.size(); i < n; ++i){
			m_selections.append(new DisplaySelection(this, 1 + i, rects[i].topLeft()));
			m_selections.back()->setPoint(rects[i].bottomRight());
			m_scene.addItem(m_selections.back());
		}
		emit selectionChanged(true);
	}
}

void Displayer::sendScaleRequest(const ScaleRequest& request)
{
	m_scaleMutex.lock();
	m_scaleRequests.append(request);
	m_scaleCond.wakeOne();
	m_scaleMutex.unlock();
}

void Displayer::scaleThread()
{
	m_scaleMutex.lock();
	while(true){
		while(m_scaleRequests.isEmpty()){
			m_scaleCond.wait(&m_scaleMutex);
		}
		ScaleRequest req = m_scaleRequests.takeFirst();
		if(req.type == ScaleRequest::Quit){
			break;
		}else if(req.type == ScaleRequest::Scale){
			m_scaleMutex.unlock();
			QImage image = m_renderer->render(req.page, req.scale * req.resolution);

			m_scaleMutex.lock();
			if(!m_scaleRequests.isEmpty() && m_scaleRequests.first().type == ScaleRequest::Abort){
				m_scaleRequests.removeFirst();
				continue;
			}
			m_scaleMutex.unlock();

			m_renderer->adjustImage(image, req.brightness, req.contrast, req.invert);

			m_scaleMutex.lock();
			if(!m_scaleRequests.isEmpty() && m_scaleRequests.first().type == ScaleRequest::Abort){
				m_scaleRequests.removeFirst();
				continue;
			}
			m_scaleMutex.unlock();

			QMetaObject::invokeMethod(this, "setScaledImage", Qt::BlockingQueuedConnection, Q_ARG(QImage, image), Q_ARG(double, m_scale));
			m_scaleMutex.lock();
		}
	}
	m_scaleMutex.unlock();
}

void Displayer::setScaledImage(const QImage &image, double scale)
{
	m_scaleMutex.lock();
	if(!m_scaleRequests.isEmpty() && m_scaleRequests.first().type == ScaleRequest::Abort){
		m_scaleRequests.removeFirst();
	}else{
		m_imageItem->setPixmap(QPixmap::fromImage(image));
		m_imageItem->setScale(1.f / scale);
		m_imageItem->setTransformOriginPoint(m_imageItem->boundingRect().center());
		m_imageItem->setPos(m_imageItem->pos() - m_imageItem->sceneBoundingRect().center());
	}
	m_scaleMutex.unlock();
}
