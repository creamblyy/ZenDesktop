/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_quick_proxy.h"
#include "relay_hooks/relay_hooks.h"
#include "relay_hooks/zen_ws_proxy.h"
#include "base/weak_ptr.h"
#include "boxes/connection_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "mtproto/mtproto_proxy_data.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QTcpSocket>
#include <chrono>
#include <memory>

namespace Relay {
namespace {

QHash<const ZenQuickProxyWidget*, base::weak_ptr<Window::SessionController>> gWeakControllers;

struct ProxyGeoEntry {
	QString label;
	qint64 fetchedMs = 0;
	qint64 ttlMs = 0;
};

QHash<QString, ProxyGeoEntry> gGeoCache;
QSet<QString> gGeoInFlight;

QNetworkAccessManager *ProxyGeoNet() {
	static QNetworkAccessManager manager;
	return &manager;
}

[[nodiscard]] bool IsLocalProxyHost(const QString &host) {
	const auto h = host.trimmed().toLower();
	return h.isEmpty()
		|| h == u"127.0.0.1"_q
		|| h == u"localhost"_q
		|| h == u"::1"_q
		|| h == u"0.0.0.0"_q
		|| h.startsWith(u"192.168."_q)
		|| h.startsWith(u"10."_q);
}

[[nodiscard]] QString CachedProxyGeo(const QString &host) {
	const auto h = host.trimmed().toLower();
	if (IsLocalProxyHost(h)) {
		return QString();
	}
	const auto it = gGeoCache.constFind(h);
	if (it == gGeoCache.constEnd()) {
		return QString();
	}
	if (QDateTime::currentMSecsSinceEpoch() - it->fetchedMs > it->ttlMs) {
		return QString();
	}
	return it->label;
}

void RequestProxyGeo(const QString &host, ZenQuickProxyWidget *widget) {
	const auto h = host.trimmed().toLower();
	if (IsLocalProxyHost(h) || gGeoInFlight.contains(h)) {
		return;
	}
	const auto it = gGeoCache.constFind(h);
	if (it != gGeoCache.constEnd()
		&& (QDateTime::currentMSecsSinceEpoch() - it->fetchedMs <= it->ttlMs)) {
		return;
	}
	gGeoInFlight.insert(h);
	const QUrl url(QString::fromUtf8("https://ipwho.is/%1").arg(
		QString::fromUtf8(QUrl::toPercentEncoding(h))));
	QNetworkRequest req(url);
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(std::chrono::milliseconds(4000));
	auto *reply = ProxyGeoNet()->get(req);
	const auto widgetPtr = QPointer<ZenQuickProxyWidget>(widget);
	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, h, widgetPtr] {
		reply->deleteLater();
		gGeoInFlight.remove(h);
		const auto now = QDateTime::currentMSecsSinceEpoch();
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
			QJsonParseError err;
			const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
			if (err.error == QJsonParseError::NoError && doc.isObject()) {
				const auto obj = doc.object();
				if (obj.value(u"success"_q).toBool()) {
					const auto cc = obj.value(u"country_code"_q).toString();
					const auto country = obj.value(u"country"_q).toString();
					const auto city = obj.value(u"city"_q).toString();
					const auto conn = obj.value(u"connection"_q).toObject();
					auto isp = conn.value(u"org"_q).toString();
					if (isp.isEmpty()) {
						isp = conn.value(u"isp"_q).toString();
					}
					QStringList parts;
					if (!cc.isEmpty()) {
						parts.append(cc);
					} else if (!country.isEmpty()) {
						parts.append(country);
					}
					if (!city.isEmpty()) {
						parts.append(city);
					}
					if (!isp.isEmpty()) {
						parts.append(isp);
					}
					if (!parts.isEmpty()) {
						gGeoCache.insert(h, {
							parts.join(QString::fromUtf8(" · ")),
							now,
							30LL * 60 * 1000,
						});
						if (widgetPtr) {
							widgetPtr->updateState();
						}
						return;
					}
				}
			}
		}
		gGeoCache.insert(h, { QString(), now, 3LL * 60 * 1000 });
	});
}

} // namespace

ZenQuickProxyWidget::ZenQuickProxyWidget(
		QWidget *parent,
		Window::SessionController *controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	if (controller) {
		gWeakControllers.insert(this, base::make_weak(controller));
	}
	lifetime().add([this] {
		gWeakControllers.remove(this);
	});
	resize(32, 32);
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
	const auto &proxySettings = Core::App().settings().proxy();
	const auto enabled = (proxySettings.settings() == MTP::ProxyData::Settings::Enabled);
	const auto ws = RelayHooks::Instance().isWsProxyEnabled();
	if (ws) {
		setToolTip((_pingMs > 0)
			? QString::fromUtf8("WS Proxy: %1 ms").arg(_pingMs)
			: QString::fromUtf8("WS Proxy (вкл.)"));
	} else if (enabled) {
		auto tip = (_pingMs > 0)
			? QString::fromUtf8("Прокси: %1 ms").arg(_pingMs)
			: (_pingMs < 0
				? QString::fromUtf8("Прокси: нет ответа")
				: QString::fromUtf8("Прокси (вкл.)"));
		if (const auto geo = CachedProxyGeo(proxySettings.selected().host); !geo.isEmpty()) {
			tip += u"\n"_q + geo;
		}
		setToolTip(tip);
	} else {
		setToolTip(QString::fromUtf8("Прокси отключён (ЛКМ — выбрать, ПКМ — вкл/выкл)"));
	}
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
		port = ZenWsProxy::Instance().port();
		if (!port) {
			port = 1443;
		}
	} else {
		const auto selected = proxySettings.selected();
		host = selected.host;
		port = selected.port;
		if (!host.isEmpty()) {
			RequestProxyGeo(host, this);
		}
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
	const auto finished = std::make_shared<bool>(false);

	connect(sock, &QTcpSocket::connected, this, [this, sock, timeout, finished] {
		if (*finished) {
			return;
		}
		*finished = true;
		sock->disconnect(this);
		timeout->stop();
		_pingMs = std::max(1, int(_pingClock.elapsed()));
		sock->abort();
		sock->deleteLater();
		updateState();
	});
	connect(sock, &QTcpSocket::errorOccurred, this, [this, sock, timeout, finished](QAbstractSocket::SocketError) {
		if (*finished) {
			return;
		}
		*finished = true;
		sock->disconnect(this);
		timeout->stop();
		_pingMs = -1;
		sock->abort();
		sock->deleteLater();
		updateState();
	});
	connect(timeout, &QTimer::timeout, this, [this, sock, finished] {
		if (*finished) {
			return;
		}
		*finished = true;
		sock->disconnect(this);
		_pingMs = -1;
		sock->abort();
		sock->deleteLater();
		updateState();
	});
	sock->connectToHost(host, port);
}

void ZenQuickProxyWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	if (_hovered) {
		auto hoverBg = st::windowFg->c;
		hoverBg.setAlphaF(0.08);
		p.setPen(Qt::NoPen);
		p.setBrush(hoverBg);
		p.drawEllipse(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
	}

	const auto &proxySettings = Core::App().settings().proxy();
	const auto enabled = (proxySettings.settings() == MTP::ProxyData::Settings::Enabled);
	const auto ws = RelayHooks::Instance().isWsProxyEnabled();

	const auto iconColor = (ws
		? QColor(0x2b, 0x3c, 0xff)
		: (enabled ? st::windowBgActive->c : st::windowSubTextFg->c));
	const auto iconLeft = (width() - st::menuIconPermissions.width()) / 2;
	const auto iconTop = (height() - st::menuIconPermissions.height()) / 2;
	st::menuIconPermissions.paint(p, iconLeft, iconTop, width(), iconColor);

	if (enabled || ws) {
		const auto dotColor = (_pingMs < 0)
			? QColor(255, 159, 10)
			: (ws ? QColor(0x2b, 0x3c, 0xff) : QColor(52, 199, 89));
		p.setPen(QPen(st::windowBg->c, 1.5));
		p.setBrush(dotColor);
		p.drawEllipse(QRectF(width() - 9.0, height() - 9.0, 6.0, 6.0));
	}
}

void ZenQuickProxyWidget::toggleProxyQuick() {
	if (RelayHooks::Instance().isWsProxyEnabled()) {
		RelayHooks::Instance().setWsProxyEnabled(false);
		ZenWsProxy::Instance().applyEnabled(false);
		Core::App().saveSettingsDelayed();
		measurePing();
		updateState();
		return;
	}
	auto &proxy = Core::App().settings().proxy();
	if (proxy.settings() == MTP::ProxyData::Settings::Enabled) {
		Core::App().setCurrentProxy(proxy.selected(), MTP::ProxyData::Settings::Disabled);
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
	updateState();
}

void ZenQuickProxyWidget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto &proxySettings = Core::App().settings().proxy();
	const auto enabled = (proxySettings.settings() == MTP::ProxyData::Settings::Enabled);
	const auto selected = proxySettings.selected();
	const auto wsOn = RelayHooks::Instance().isWsProxyEnabled();

	const auto pingText = wsOn
		? ((_pingMs > 0) ? QString::fromUtf8("WS Proxy • %1 ms").arg(_pingMs) : QString::fromUtf8("WS Proxy активен"))
		: (enabled
			? ((_pingMs > 0) ? QString::fromUtf8("Пинг прокси: %1 ms").arg(_pingMs) : QString::fromUtf8("Прокси активен"))
			: QString::fromUtf8("Прокси отключён"));
	const auto headerAct = _menu->addAction(pingText, [] {});
	headerAct->setEnabled(false);
	if (enabled && !wsOn && !selected.host.isEmpty()) {
		RequestProxyGeo(selected.host, this);
		if (const auto geo = CachedProxyGeo(selected.host); !geo.isEmpty()) {
			const auto geoAct = _menu->addAction(geo, [] {});
			geoAct->setEnabled(false);
		}
	}
	_menu->addSeparator();

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

	_menu->addAction(
		QString::fromUtf8(wsOn ? "✓ WS Proxy (ZenDesktop)" : "WS Proxy (ZenDesktop)"),
		[this, wsOn] {
			const auto next = !wsOn;
			RelayHooks::Instance().setWsProxyEnabled(next);
			ZenWsProxy::Instance().applyEnabled(next);
			Core::App().saveSettingsDelayed();
			measurePing();
			updateState();
		});

	_menu->addAction(QString::fromUtf8("Отключить прокси"), [this] {
		const auto &p = Core::App().settings().proxy();
		Core::App().setCurrentProxy(p.selected(), MTP::ProxyData::Settings::Disabled);
		if (RelayHooks::Instance().isWsProxyEnabled()) {
			RelayHooks::Instance().setWsProxyEnabled(false);
			ZenWsProxy::Instance().applyEnabled(false);
		}
		Core::App().saveSettingsDelayed();
		measurePing();
		updateState();
	});

	const auto weakCtrl = gWeakControllers.value(this);
	if (weakCtrl.get()) {
		_menu->addSeparator();
		_menu->addAction(QString::fromUtf8("Настройки прокси..."), [weakCtrl] {
			if (const auto ctrl = weakCtrl.get()) {
				ProxiesBoxController::Show(ctrl);
			}
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
