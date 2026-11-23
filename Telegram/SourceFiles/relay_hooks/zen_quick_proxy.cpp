/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_quick_proxy.h"
#include "relay_hooks/relay_hooks.h"
#include "relay_hooks/zen_ws_proxy.h"
#include "boxes/connection_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "mtproto/mtproto_proxy_data.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtNetwork/QTcpSocket>

namespace Relay {

ZenQuickProxyWidget::ZenQuickProxyWidget(
		QWidget *parent,
		Window::SessionController *controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	resize(56, 28);
	setCursor(style::cur_pointer);
	setMouseTracking(true);

	_pingTimer.setInterval(15000);
	connect(&_pingTimer, &QTimer::timeout, this, [this] {
		measurePing();
	});
	_pingTimer.start();
	QTimer::singleShot(1500, this, [this] {
		measurePing();
	});

	updateState();
}

void ZenQuickProxyWidget::updateState() {
	setVisible(RelayHooks::Instance().isQuickProxyEnabled());
	update();
}

void ZenQuickProxyWidget::enterEventHook(QEnterEvent *e) {
	_hovered = true;
	update();
	Ui::RpWidget::enterEventHook(e);
}

void ZenQuickProxyWidget::leaveEventHook(QEvent *e) {
	_hovered = false;
	update();
	Ui::RpWidget::leaveEventHook(e);
}

void ZenQuickProxyWidget::measurePing() {
	if (!RelayHooks::Instance().isQuickProxyEnabled()) {
		return;
	}
	const auto &proxySettings = Core::App().settings().proxy();
	if (proxySettings.settings() != MTP::ProxyData::Settings::Enabled) {
		_pingMs = 0;
		update();
		return;
	}

	QString host;
	uint16 port = 0;
	if (RelayHooks::Instance().isWsProxyEnabled()) {
		host = "127.0.0.1";
		port = 1080;
	} else {
		const auto selected = proxySettings.selected();
		host = selected.host;
		port = selected.port;
	}
	if (host.isEmpty() || port == 0) {
		_pingMs = 0;
		update();
		return;
	}

	auto *sock = new QTcpSocket(this);
	_pingClock.restart();
	auto *timeout = new QTimer(sock);
	timeout->setSingleShot(true);
	timeout->start(3000);

	connect(sock, &QTcpSocket::connected, this, [this, sock, timeout] {
		timeout->stop();
		_pingMs = std::max(1, int(_pingClock.elapsed()));
		sock->abort();
		sock->deleteLater();
		update();
	});
	connect(sock, &QTcpSocket::errorOccurred, this, [this, sock, timeout](QAbstractSocket::SocketError) {
		timeout->stop();
		_pingMs = -1;
		sock->deleteLater();
		update();
	});
	connect(timeout, &QTimer::timeout, this, [this, sock] {
		_pingMs = -1;
		sock->abort();
		sock->deleteLater();
		update();
	});
	sock->connectToHost(host, port);
}

void ZenQuickProxyWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	auto bg = st::windowFg->c;
	bg.setAlphaF(_hovered ? 0.12 : 0.06);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(r, 14.0, 14.0);

	const auto &proxySettings = Core::App().settings().proxy();
	const auto enabled = (proxySettings.settings() == MTP::ProxyData::Settings::Enabled);
	const auto ws = RelayHooks::Instance().isWsProxyEnabled();

	QColor dotColor = st::windowSubTextFg->c;
	QString label = "OFF";
	if (enabled || ws) {
		if (_pingMs < 0) {
			dotColor = QColor(255, 159, 10);
			label = "--";
		} else {
			dotColor = ws ? QColor(0x2b, 0x3c, 0xff) : QColor(52, 199, 89);
			label = (_pingMs > 0) ? QString("%1ms").arg(std::min(_pingMs, 999)) : QString("ON");
		}
	}

	p.setBrush(dotColor);
	p.drawEllipse(QRectF(7.0, (height() - 6.0) / 2.0, 6.0, 6.0));

	auto f = st::semiboldFont;
	p.setFont(f);
	p.setPen(enabled || ws ? st::windowFg->c : st::windowSubTextFg->c);
	p.drawText(QRectF(15.0, 0.0, width() - 18.0, height()), Qt::AlignCenter, label);
}

void ZenQuickProxyWidget::toggleProxyQuick() {
	auto &proxy = Core::App().settings().proxy();
	if (proxy.settings() == MTP::ProxyData::Settings::Enabled) {
		Core::App().setCurrentProxy(proxy.selected(), MTP::ProxyData::Settings::Disabled);
		if (RelayHooks::Instance().isWsProxyEnabled()) {
			RelayHooks::Instance().setWsProxyEnabled(false);
			ZenWsProxy::Instance().stop();
		}
	} else {
		if (!proxy.selected().host.isEmpty()) {
			Core::App().setCurrentProxy(proxy.selected(), MTP::ProxyData::Settings::Enabled);
		} else if (!proxy.list().empty()) {
			Core::App().setCurrentProxy(proxy.list().back(), MTP::ProxyData::Settings::Enabled);
		} else {
			showMenu();
			return;
		}
	}
	Core::App().saveSettingsDelayed();
	measurePing();
	update();
}

void ZenQuickProxyWidget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto &proxySettings = Core::App().settings().proxy();
	const auto enabled = (proxySettings.settings() == MTP::ProxyData::Settings::Enabled);
	const auto selected = proxySettings.selected();

	for (const auto &item : proxySettings.list()) {
		const auto isCurrent = enabled
			&& (item.host == selected.host)
			&& (item.port == selected.port)
			&& (item.type == selected.type);
		const auto typeName = (item.type == MTP::ProxyData::Type::Mtproto)
			? "MTProto"
			: (item.type == MTP::ProxyData::Type::Socks5 ? "SOCKS5" : "HTTP");
		const auto title = QString("%1%2 (%3:%4)")
			.arg(isCurrent ? QString::fromUtf8("✓ ") : QString())
			.arg(typeName)
			.arg(item.host)
			.arg(item.port);
		_menu->addAction(title, [this, item] {
			Core::App().setCurrentProxy(item, MTP::ProxyData::Settings::Enabled);
			Core::App().saveSettingsDelayed();
			measurePing();
			update();
		});
	}

	if (!proxySettings.list().empty()) {
		_menu->addSeparator();
	}

	const auto wsOn = RelayHooks::Instance().isWsProxyEnabled();
	_menu->addAction(
		QString::fromUtf8(wsOn ? "✓ WS Proxy (ZenDesktop)" : "WS Proxy (ZenDesktop)"),
		[this, wsOn] {
			const auto next = !wsOn;
			RelayHooks::Instance().setWsProxyEnabled(next);
			if (next) {
				ZenWsProxy::Instance().start();
			} else {
				ZenWsProxy::Instance().stop();
			}
			measurePing();
			update();
		});

	_menu->addAction(QString::fromUtf8("Отключить прокси"), [this] {
		const auto &p = Core::App().settings().proxy();
		Core::App().setCurrentProxy(p.selected(), MTP::ProxyData::Settings::Disabled);
		if (RelayHooks::Instance().isWsProxyEnabled()) {
			RelayHooks::Instance().setWsProxyEnabled(false);
			ZenWsProxy::Instance().stop();
		}
		Core::App().saveSettingsDelayed();
		measurePing();
		update();
	});

	if (_controller) {
		_menu->addSeparator();
		_menu->addAction(QString::fromUtf8("Настройки прокси..."), [ctrl = _controller] {
			ProxiesBoxController::Show(ctrl);
		});
	}

	_menu->popup(QCursor::pos());
}

void ZenQuickProxyWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::RightButton) {
		toggleProxyQuick();
		return;
	}
	if (e->button() == Qt::LeftButton) {
		showMenu();
		return;
	}
	Ui::RpWidget::mousePressEvent(e);
}

} // namespace Relay
