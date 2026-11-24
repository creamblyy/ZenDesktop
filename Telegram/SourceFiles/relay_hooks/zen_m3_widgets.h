/*
This file is part of Telegram Desktop fork - ZenDesktop (Material 3 UI widgets).
*/
#pragma once

#include "ui/rp_widget.h"
#include <QtCore/QString>
#include <functional>

namespace Relay {

// Native Material 3 slider matching m3.material.io/components/sliders/specs:
// 12px tall rounded track with 4px gap around a vertical capsule handle + stop dot.
class ZenM3Slider final : public Ui::RpWidget {
public:
	ZenM3Slider(
		QWidget *parent,
		QString title,
		int minVal,
		int maxVal,
		int step,
		int initialVal,
		std::function<QString(int)> formatter,
		std::function<void(int)> onChange);

	[[nodiscard]] int value() const { return _value; }
	void setValue(int v, bool notify = false);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void updateFromX(int x);

	QString _title;
	int _min = 0;
	int _max = 100;
	int _step = 1;
	int _value = 0;
	std::function<QString(int)> _formatter;
	std::function<void(int)> _onChange;
	bool _dragging = false;
	bool _hovered = false;
};

} // namespace Relay
