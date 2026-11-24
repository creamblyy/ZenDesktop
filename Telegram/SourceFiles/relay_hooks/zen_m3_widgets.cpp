/*
This file is part of Telegram Desktop fork - ZenDesktop (Material 3 UI widgets).
*/
#include "relay_hooks/zen_m3_widgets.h"
#include "relay_hooks/zen_m3_palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <algorithm>
#include <cmath>

namespace Relay {
namespace {

constexpr auto kSidePad = 22.0;
constexpr auto kTrackHeight = 12.0;
constexpr auto kHandleWidthIdle = 4.0;
constexpr auto kHandleWidthDrag = 3.0;
constexpr auto kHandleHeight = 24.0;
constexpr auto kHandleGap = 4.0;
constexpr auto kStopDotSize = 4.0;

} // namespace

ZenM3Slider::ZenM3Slider(
	QWidget *parent,
	QString title,
	int minVal,
	int maxVal,
	int step,
	int initialVal,
	std::function<QString(int)> formatter,
	std::function<void(int)> onChange)
: Ui::RpWidget(parent)
, _title(std::move(title))
, _min(minVal)
, _max(std::max(minVal + 1, maxVal))
, _step(std::max(1, step))
, _value(std::clamp(initialVal, minVal, std::max(minVal + 1, maxVal)))
, _formatter(std::move(formatter))
, _onChange(std::move(onChange)) {
	resize(width(), 64);
	setMinimumHeight(64);
	setMaximumHeight(64);
	setCursor(style::cur_pointer);
	setMouseTracking(true);
}

void ZenM3Slider::setValue(int v, bool notify) {
	const auto snapped = _min + int(std::round(double(v - _min) / _step)) * _step;
	const auto clamped = std::clamp(snapped, _min, _max);
	if (_value == clamped) {
		return;
	}
	_value = clamped;
	update();
	if (notify && _onChange) {
		_onChange(_value);
	}
}

void ZenM3Slider::enterEventHook(QEnterEvent *e) {
	_hovered = true;
	update();
	Ui::RpWidget::enterEventHook(e);
}

void ZenM3Slider::leaveEventHook(QEvent *e) {
	_hovered = false;
	update();
	Ui::RpWidget::leaveEventHook(e);
}

void ZenM3Slider::updateFromX(int x) {
	const auto trackLeft = kSidePad;
	const auto trackRight = double(width()) - kSidePad;
	const auto span = std::max(1.0, trackRight - trackLeft);
	const auto t = std::clamp((double(x) - trackLeft) / span, 0.0, 1.0);
	const auto raw = _min + int(std::round(t * (_max - _min)));
	setValue(raw, true);
}

void ZenM3Slider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = true;
		updateFromX(e->pos().x());
		update();
		return;
	}
	Ui::RpWidget::mousePressEvent(e);
}

void ZenM3Slider::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		updateFromX(e->pos().x());
		return;
	}
	Ui::RpWidget::mouseMoveEvent(e);
}

void ZenM3Slider::mouseReleaseEvent(QMouseEvent *e) {
	if (_dragging && e->button() == Qt::LeftButton) {
		_dragging = false;
		update();
		return;
	}
	Ui::RpWidget::mouseReleaseEvent(e);
}

void ZenM3Slider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto activeColor = st::windowBgActive->c;
	const auto bgColor = st::windowBg->c;
	const auto fgColor = st::windowFg->c;

	// Value pill string
	const auto valText = _formatter ? _formatter(_value) : QString::number(_value);
	const auto pillFont = st::semiboldFont;
	const auto textW = pillFont->width(valText);
	const auto pillW = std::max(44.0, double(textW) + 18.0);
	const auto pillH = 22.0;
	const QRectF pillRect(double(width()) - kSidePad - pillW, 8.0, pillW, pillH);

	// Title label
	p.setFont(st::defaultFlatLabel.style.font);
	p.setPen(fgColor);
	p.drawText(
		QRectF(kSidePad, 7.0, pillRect.left() - kSidePad - 10.0, 24.0),
		Qt::AlignLeft | Qt::AlignVCenter,
		_title);

	// M3 PrimaryContainer value pill
	p.setPen(Qt::NoPen);
	p.setBrush(M3::PrimaryContainer(bgColor, activeColor));
	p.drawRoundedRect(pillRect, pillH * 0.5, pillH * 0.5);
	p.setFont(pillFont);
	p.setPen(activeColor);
	p.drawText(pillRect, Qt::AlignCenter, valText);

	// M3 Slider Track & Handle geometry
	const auto trackLeft = kSidePad;
	const auto trackRight = double(width()) - kSidePad;
	const auto span = std::max(1.0, trackRight - trackLeft);
	const auto ratio = double(_value - _min) / double(_max - _min);
	const auto centerY = 44.0;
	const auto trackTop = centerY - kTrackHeight * 0.5;
	const auto handleW = _dragging ? kHandleWidthDrag : kHandleWidthIdle;
	const auto handleX = std::clamp(
		trackLeft + ratio * span - handleW * 0.5,
		trackLeft,
		trackRight - handleW);

	// Active track (left of handle - gap)
	const auto activeEnd = handleX - kHandleGap;
	if (activeEnd > trackLeft + 4.0) {
		const QRectF activeRect(trackLeft, trackTop, activeEnd - trackLeft, kTrackHeight);
		p.setBrush(activeColor);
		p.drawRoundedRect(activeRect, kTrackHeight * 0.5, kTrackHeight * 0.5);
	}

	// Inactive track (right of handle + gap)
	const auto inactiveStart = handleX + handleW + kHandleGap;
	if (trackRight > inactiveStart + 4.0) {
		const QRectF inactiveRect(inactiveStart, trackTop, trackRight - inactiveStart, kTrackHeight);
		auto trackBg = fgColor;
		trackBg.setAlphaF(_hovered ? 0.18 : 0.13);
		p.setBrush(trackBg);
		p.drawRoundedRect(inactiveRect, kTrackHeight * 0.5, kTrackHeight * 0.5);

		// End stop indicator dot
		if (trackRight - inactiveStart > 14.0) {
			p.setBrush(activeColor);
			p.drawEllipse(QRectF(
				trackRight - kSidePad * 0.0 - 6.0 - kStopDotSize * 0.5,
				centerY - kStopDotSize * 0.5,
				kStopDotSize,
				kStopDotSize));
		}
	}

	// Vertical capsule handle
	const QRectF handleRect(handleX, centerY - kHandleHeight * 0.5, handleW, kHandleHeight);
	p.setBrush(activeColor);
	p.drawRoundedRect(handleRect, handleW * 0.5, handleW * 0.5);
}

} // namespace Relay
