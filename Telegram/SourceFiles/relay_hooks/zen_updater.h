/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Window {
class SessionController;
} // namespace Window

namespace Relay {

struct ZenUpdateInfo {
	QString version;
	int versionCode = 0;
	QString downloadUrl;
	QString sha256;
	QStringList changelog;
};

class ZenUpdater final : public QObject {
public:
	static ZenUpdater &Instance();

	void init(Window::SessionController *controller = nullptr);
	void setController(Window::SessionController *controller);
	void checkForUpdates(bool manual = false);
	void showChangelog(Window::SessionController *controller);
	void downloadAndInstall(const ZenUpdateInfo &info);

	[[nodiscard]] const QString &currentVersion() const;
	[[nodiscard]] int currentVersionCode() const;
	[[nodiscard]] bool isChecking() const;
	[[nodiscard]] bool isAutoCheckEnabled() const;
	void setAutoCheckEnabled(bool enabled);

private:
	ZenUpdater();
	~ZenUpdater() override = default;

	void handleManifest(const QByteArray &data, bool manual);
	void showUpdateModal(const ZenUpdateInfo &info);
	void startDownload(const QString &url, const QString &targetPath);
	void launchUpdaterAndExit(const QString &newExePath);

	Window::SessionController *_controller = nullptr;
	QNetworkAccessManager _net;
	QTimer _checkTimer;
	bool _isChecking = false;
	bool _updateAvailable = false;
	bool _autoCheck = true;
	ZenUpdateInfo _availableUpdate;
};

} // namespace Relay
