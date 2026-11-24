/*
This file is part of Telegram Desktop fork - ZenDesktop (Material 3 UI widgets).
*/
#include "relay_hooks/zen_m3_widgets.h"
#include "relay_hooks/zen_m3_palette.h"
#include "relay_hooks/relay_hooks.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "settings/detailed_settings_button.h"
#include "ui/effects/animations.h"
#include "ui/style/style_core_direction.h"
#include "ui/ui_utility.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Relay {
namespace {

constexpr auto kSidePad = 22.0;
constexpr auto kTrackHeight = 12.0;
constexpr auto kHandleWidthIdle = 4.0;
constexpr auto kHandleWidthDrag = 3.0;
constexpr auto kHandleHeight = 24.0;
constexpr auto kHandleGap = 4.0;
constexpr auto kStopDotSize = 4.0;

struct M3ThemeState final : QObject {
	explicit M3ThemeState(QObject *parent) : QObject(parent) {}
	std::vector<QPointer<QWidget>> rows;
	std::function<void()> scan;
	bool scanScheduled = false;
	bool refreshScheduled = false;
};

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

	// M3 Island surface behind slider
	auto islandBg = fgColor;
	islandBg.setAlphaF(0.06);
	p.setPen(Qt::NoPen);
	p.setBrush(islandBg);
	p.drawRoundedRect(QRectF(12.0, 2.0, double(width()) - 24.0, double(height()) - 4.0), 16.0, 16.0);

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

void PaintZenM3Switch(
		QPainter &p,
		const QRectF &trackRect,
		float64 progress,
		bool hovered,
		bool pressed,
		bool disabled,
		const style::icon *lockIcon) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	if (disabled) {
		p.setOpacity(0.42);
	}

	const auto t = std::clamp(progress, 0.0, 1.0);
	const auto bgColor = st::windowBg->c;
	const auto fgColor = st::windowFg->c;
	const auto activeColor = st::windowBgActive->c;
	const auto onActiveColor = st::windowFgActive->c;
	const auto isDark = (bgColor.lightnessF() < 0.5);

	// Track capsule (opaque off surface covers underlying stock ToggleView)
	const auto r = trackRect.height() * 0.5;
	const auto offTrackBg = M3::Blend(bgColor, fgColor, isDark ? 0.14f : 0.10f);
	const auto trackBg = M3::Blend(offTrackBg, activeColor, float(t));
	p.setPen(Qt::NoPen);
	p.setBrush(trackBg);
	p.drawRoundedRect(trackRect, r, r);

	if (1.0 - t > 0.01) {
		auto outline = M3::Blend(bgColor, fgColor, isDark ? 0.50f : 0.44f);
		outline.setAlphaF(outline.alphaF() * (1.0 - t));
		p.setPen(QPen(outline, 1.8));
		p.setBrush(Qt::NoBrush);
		const auto innerR = std::max(0.0, r - 0.9);
		p.drawRoundedRect(trackRect.adjusted(0.9, 0.9, -0.9, -0.9), innerR, innerR);
	}

	// Handle / Thumb geometry
	const auto h = trackRect.height();
	const auto minCx = trackRect.left() + h * 0.5;
	const auto maxCx = trackRect.right() - h * 0.5;
	const auto cx = minCx + (maxCx - minCx) * t;
	const auto cy = trackRect.center().y();

	const auto hasLock = (lockIcon && !lockIcon->empty());
	const auto onDiam = h * 0.78;
	const auto offDiam = hasLock ? onDiam : (h * 0.50);
	const auto pressBoost = pressed ? (h * 0.10) : 0.0;
	const auto diam = std::min(h - 2.0, offDiam + (onDiam - offDiam) * t + pressBoost);

	// Hover / Press State Layer
	if ((hovered || pressed) && !disabled) {
		auto halo = (t > 0.5) ? activeColor : fgColor;
		halo.setAlphaF(pressed ? 0.14 : 0.08);
		const auto haloR = h * 0.64;
		p.setPen(Qt::NoPen);
		p.setBrush(halo);
		p.drawEllipse(QPointF(cx, cy), haloR, haloR);
	}

	// Thumb circle
	const auto offThumb = M3::Blend(
		bgColor,
		fgColor,
		isDark ? (hovered ? 0.72f : 0.60f) : (hovered ? 0.36f : 0.46f));
	const auto thumbColor = M3::Blend(offThumb, onActiveColor, float(t));
	p.setPen(Qt::NoPen);
	p.setBrush(thumbColor);
	p.drawEllipse(QPointF(cx, cy), diam * 0.5, diam * 0.5);

	// Optional lock icon or M3 checkmark inside thumb
	if (hasLock) {
		const QRect iconRect(
			int(std::round(cx - 8.0)),
			int(std::round(cy - 8.0)),
			16,
			16);
		lockIcon->paintInCenter(p, iconRect, (t > 0.5) ? activeColor : bgColor);
	} else if (t > 0.35) {
		const auto iconT = std::clamp((t - 0.35) / 0.65, 0.0, 1.0);
		const auto s = (diam / 20.0) * (0.65 + 0.35 * iconT);
		auto checkColor = activeColor;
		checkColor.setAlphaF(iconT);
		QPainterPath check;
		check.moveTo(cx - 4.0 * s, cy + 0.2 * s);
		check.lineTo(cx - 1.2 * s, cy + 3.0 * s);
		check.lineTo(cx + 4.3 * s, cy - 2.8 * s);
		p.setPen(QPen(checkColor, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(check);
	}

	p.restore();
}

void AttachZenM3Switch(not_null<Ui::SettingsButton*> button) {
	if (!button->accessibilityState().checkable
		|| button->property("zen_m3_switch_attached").toBool()) {
		return;
	}
	button->setProperty("zen_m3_switch_attached", true);

	struct SwitchState {
		Ui::Animations::Simple toggleAnim;
		bool initialized = false;
		bool checked = false;
	};

	const auto btn = button.get();
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(btn);
	overlay->setObjectName(u"zen_m3_switch_overlay"_q);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto state = overlay->lifetime().make_state<SwitchState>();

	constexpr auto kTrackW = 42;
	constexpr auto kTrackH = 25;
	constexpr auto kPad = 5;

	button->toggledValue(
	) | rpl::on_next([=](bool value) {
		if (!state->initialized) {
			state->initialized = true;
			state->checked = value;
			overlay->update();
			return;
		}
		if (value == state->checked) {
			return;
		}
		const auto from = state->toggleAnim.value(state->checked ? 1.0 : 0.0);
		state->checked = value;
		state->toggleAnim.start(
			[=] { overlay->update(); },
			from,
			state->checked ? 1.0 : 0.0,
			220,
			anim::easeOutCubic);
	}, overlay->lifetime());

	button->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Enter:
		case QEvent::Leave:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::EnabledChange:
			overlay->update();
			break;
		default:
			break;
		}
	}, overlay->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &sz) {
		const auto origRight = sz.width() - btn->st().toggleSkip;
		const auto trackRight = origRight + 1;
		const auto trackLeft = trackRight - kTrackW;
		const auto trackTop = (sz.height() - kTrackH) / 2;
		overlay->setGeometry(style::rtlrect(
			trackLeft - kPad,
			trackTop - kPad,
			kTrackW + 2 * kPad,
			kTrackH + 2 * kPad,
			sz.width()));
	}, overlay->lifetime());

	overlay->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(overlay);
		const auto progress = state->toggleAnim.value(
			state->checked ? 1.0 : 0.0);
		const auto locked = btn->property("zen_toggle_locked").toBool();
		const style::icon *lockIcon = (locked && !btn->st().toggle.lockIcon.empty())
			? &btn->st().toggle.lockIcon
			: nullptr;
		PaintZenM3Switch(
			p,
			QRectF(kPad, kPad, kTrackW, kTrackH),
			progress,
			btn->isOver(),
			btn->isDown(),
			btn->isDisabled(),
			lockIcon);
	}, overlay->lifetime());

	overlay->show();
	overlay->raise();
}

void ApplyZenM3SettingsTheme(not_null<QWidget*> root) {
	if (root->property("zen_m3_theme_root").toBool()) {
		return;
	}
	root->setProperty("zen_m3_theme_root", true);

	const auto rootPtr = QPointer<QWidget>(root.get());
	const auto statePtr = QPointer<M3ThemeState>(new M3ThemeState(root.get()));

	const auto scheduleRefreshRows = [=] {
		if (!rootPtr || !statePtr || statePtr->refreshScheduled) {
			return;
		}
		statePtr->refreshScheduled = true;
		base::call_delayed(0, rootPtr.data(), [=] {
			if (!statePtr) {
				return;
			}
			statePtr->refreshScheduled = false;
			if (!rootPtr) {
				return;
			}
			for (const auto &rowPtr : statePtr->rows) {
				if (rowPtr && rowPtr->isVisible()) {
					rowPtr->update();
				}
			}
		});
	};

	const auto attachRowOverlay = [=](not_null<Ui::RpWidget*> row, int insetLeft) {
		if (!statePtr
			|| row->property("zen_m3_skip_row").toBool()
			|| row->property("zen_m3_row_applied").toBool()) {
			return;
		}
		row->setProperty("zen_m3_row_applied", true);
		const auto rowRaw = row.get();
		statePtr->rows.push_back(rowRaw);

		const auto overlay = Ui::CreateChild<Ui::RpWidget>(rowRaw);
		overlay->setObjectName(u"zen_m3_row_overlay"_q);
		overlay->setAttribute(Qt::WA_TransparentForMouseEvents);

		row->sizeValue(
		) | rpl::on_next([=](const QSize &sz) {
			overlay->setGeometry(0, 0, sz.width(), sz.height());
		}, overlay->lifetime());

		base::install_event_filter(rowRaw, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Show || e->type() == QEvent::Hide) {
				scheduleRefreshRows();
			}
			return base::EventFilterResult::Continue;
		});

		overlay->paintRequest(
		) | rpl::on_next([=] {
			if (!rootPtr || !statePtr) {
				return;
			}
			const auto rootW = rootPtr->width();
			const auto rowW = rowRaw->width();
			if (rootW <= 0 || rowW < std::max(160, rootW - 64)) {
				return;
			}
			const auto myTop = rowRaw->mapTo(rootPtr.data(), QPoint(0, 0)).y();
			const auto myBottom = myTop + rowRaw->height();
			auto hasPrev = false;
			auto hasNext = false;
			auto nextGapIsZero = false;
			for (const auto &other : statePtr->rows) {
				if (!other || other == rowRaw || !other->isVisible() || other->height() <= 0) {
					continue;
				}
				if (other->width() < std::max(160, rootW - 64)) {
					continue;
				}
				const auto otherTop = other->mapTo(rootPtr.data(), QPoint(0, 0)).y();
				const auto otherBottom = otherTop + other->height();
				if (otherBottom >= myTop - 6 && otherBottom <= myTop + 2) {
					hasPrev = true;
				}
				if (otherTop >= myBottom - 2 && otherTop <= myBottom + 6) {
					hasNext = true;
				}
				if (otherTop >= myBottom - 1 && otherTop <= myBottom + 1) {
					nextGapIsZero = true;
				}
			}

			const auto isFirst = !hasPrev;
			const auto isLast = !hasNext;
			if (!rowRaw->property("zen_m3_is_first").isValid()
				|| rowRaw->property("zen_m3_is_first").toBool() != isFirst) {
				rowRaw->setProperty("zen_m3_is_first", isFirst);
			}
			if (!rowRaw->property("zen_m3_is_last").isValid()
				|| rowRaw->property("zen_m3_is_last").toBool() != isLast) {
				rowRaw->setProperty("zen_m3_is_last", isLast);
			}
			if (!rowRaw->property("zen_m3_next_gap_zero").isValid()
				|| rowRaw->property("zen_m3_next_gap_zero").toBool() != nextGapIsZero) {
				rowRaw->setProperty("zen_m3_next_gap_zero", nextGapIsZero);
			}

			auto p = QPainter(overlay);
			p.setRenderHint(QPainter::Antialiasing, true);

			if (Relay::RelayHooks::Instance().isMaterial3BubblesEnabled()) {
				constexpr auto kMarginX = 12.0;
				constexpr auto kOuterR = 16.0;
				constexpr auto kInnerR = 6.0;
				const auto w = double(rowRaw->width());
				const auto h = double(rowRaw->height());
				if (w <= 2.0 * kMarginX || h <= 4.0) {
					return;
				}
				const auto x1 = kMarginX;
				const auto x2 = w - kMarginX;
				const auto y1 = isFirst ? 1.0 : 2.5;
				const auto y2 = isLast ? (h - 1.0) : (h - 2.5);
				const auto rt = isFirst ? kOuterR : kInnerR;
				const auto rb = isLast ? kOuterR : kInnerR;

				QPainterPath cardPath;
				cardPath.moveTo(x1 + rt, y1);
				cardPath.lineTo(x2 - rt, y1);
				cardPath.arcTo(QRectF(x2 - 2.0 * rt, y1, 2.0 * rt, 2.0 * rt), 90.0, -90.0);
				cardPath.lineTo(x2, y2 - rb);
				cardPath.arcTo(QRectF(x2 - 2.0 * rb, y2 - 2.0 * rb, 2.0 * rb, 2.0 * rb), 0.0, -90.0);
				cardPath.lineTo(x1 + rb, y2);
				cardPath.arcTo(QRectF(x1, y2 - 2.0 * rb, 2.0 * rb, 2.0 * rb), 270.0, -90.0);
				cardPath.lineTo(x1, y1 + rt);
				cardPath.arcTo(QRectF(x1, y1, 2.0 * rt, 2.0 * rt), 180.0, -90.0);
				cardPath.closeSubpath();

				QPainterPath full;
				full.addRect(QRectF(0.0, 0.0, w, h));
				const auto maskPath = full.subtracted(cardPath);

				const auto *ab = dynamic_cast<Ui::AbstractButton*>(rowRaw);
				const auto isOver = ab && (ab->isOver() || ab->isDown()) && !ab->isDisabled();
				const auto isDark = (st::windowBg->c.lightnessF() < 0.5);
				auto tint = st::windowFg->c;
				tint.setAlphaF(isOver ? 0.02 : (isDark ? 0.055 : 0.042));

				p.fillPath(maskPath, st::windowBg->c);
				p.fillPath(cardPath, tint);
			} else if (!isLast && nextGapIsZero) {
				auto lineColor = st::windowFg->c;
				lineColor.setAlphaF(0.08);
				const auto w = std::max(0, rowRaw->width() - insetLeft - 20);
				if (w > 0 && rowRaw->height() > 0) {
					p.fillRect(insetLeft, rowRaw->height() - 1, w, 1, lineColor);
				}
			}
		}, overlay->lifetime());

		overlay->show();
		overlay->raise();
	};

	const auto scheduleScan = [=] {
		if (!rootPtr || !statePtr || statePtr->scanScheduled) {
			return;
		}
		statePtr->scanScheduled = true;
		base::call_delayed(0, rootPtr.data(), [=] {
			if (!statePtr) {
				return;
			}
			statePtr->scanScheduled = false;
			if (rootPtr && statePtr->scan) {
				statePtr->scan();
			}
		});
	};

	const auto watchContainer = [=](QWidget *w) {
		if (!w || w->property("zen_m3_watch_children").toBool()) {
			return;
		}
		w->setProperty("zen_m3_watch_children", true);
		base::install_event_filter(w, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::ChildAdded) {
				scheduleScan();
			}
			return base::EventFilterResult::Continue;
		});
	};

	statePtr->scan = [=] {
		if (!rootPtr || !statePtr) {
			return;
		}
		auto &rows = statePtr->rows;
		rows.erase(
			std::remove_if(rows.begin(), rows.end(), [](const QPointer<QWidget> &p) {
				return p.isNull();
			}),
			rows.end());
		watchContainer(rootPtr.data());
		for (auto *child : rootPtr->findChildren<QWidget*>()) {
			if (auto *layout = dynamic_cast<Ui::VerticalLayout*>(child)) {
				watchContainer(layout);
			} else if (auto *div = dynamic_cast<Ui::BoxContentDivider*>(child)) {
				if (!div->property("zen_m3_divider_applied").toBool()) {
					div->setProperty("zen_m3_divider_applied", true);
					base::install_event_filter(div, [=](not_null<QEvent*> e) {
						if (e->type() == QEvent::Paint) {
							auto p = QPainter(div);
							p.fillRect(div->rect(), st::windowBg->c);
							if (!Relay::RelayHooks::Instance().isMaterial3BubblesEnabled()) {
								auto c = st::windowFg->c;
								c.setAlphaF(0.08);
								const auto w = std::max(0, div->width() - 44);
								if (w > 0) {
									const auto y = (div->height() <= 16) ? (div->height() / 2) : 0;
									p.fillRect(22, y, w, 1, c);
								}
							}
							return base::EventFilterResult::Cancel;
						}
						return base::EventFilterResult::Continue;
					});
				}
			} else if (auto *btn = dynamic_cast<Ui::SettingsButton*>(child)) {
				AttachZenM3Switch(btn);
				attachRowOverlay(btn, btn->st().padding.left());
				for (auto *rowChild : btn->children()) {
					if (rowChild->isWidgetType()
						&& rowChild->objectName() == u"zen_m3_switch_overlay"_q) {
						static_cast<QWidget*>(rowChild)->raise();
					}
				}
			} else if (auto *dbtn = dynamic_cast<DetailedSettingsButton*>(child)) {
				attachRowOverlay(dbtn, 68);
			}
		}
		scheduleRefreshRows();
	};

	statePtr->scan();
	scheduleScan();
}

} // namespace Relay
