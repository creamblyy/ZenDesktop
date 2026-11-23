/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include "ui/rp_widget.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Relay {

class ZenQuickProxyWidget final : public Ui::RpWidget {
public:
	ZenQuickProxyWidget(
		QWidget *parent,
		Window::SessionController *controller);

	void updateState();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void measurePing();
	void showMenu();
	void toggleProxyQuick();

	Window::SessionController *_controller = nullptr;
	QTimer _pingTimer;
	QElapsedTimer _pingClock;
	int _pingMs = 0; // 0 = unknown, >0 = ms, -1 = timeout/err
	bool _hovered = false;
	base::unique_qptr<Ui::PopupMenu> _menu;
};

} // namespace Relay
