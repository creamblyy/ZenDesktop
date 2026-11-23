/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <cstdint>
#include <memory>

class QTcpServer;

namespace Relay {

class ZenWsProxy final {
public:
	static ZenWsProxy &Instance();

	bool start(uint16_t preferredPort = 1443);
	void stop();

	[[nodiscard]] bool isRunning() const;
	[[nodiscard]] uint16_t port() const;
	[[nodiscard]] QString secretHex() const;

	void applyEnabled(bool enabled);

private:
	ZenWsProxy() = default;
	~ZenWsProxy();

	ZenWsProxy(const ZenWsProxy &) = delete;
	ZenWsProxy &operator=(const ZenWsProxy &) = delete;

	void handleNewConnection();

	std::unique_ptr<QTcpServer> _server;
	uint16_t _port = 0;
	QByteArray _secret;
	QString _secretHex;
};

} // namespace Relay
