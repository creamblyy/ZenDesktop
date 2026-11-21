/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_updater.h"
#include "logs.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtNetwork/QNetworkRequest>

namespace Relay {
namespace {

constexpr auto kZenVersion = "1.0.0";
constexpr auto kZenVersionCode = 2601;
constexpr auto kUpdateManifestUrl = "https://creamblyy.github.io/ZenDesktop/update.json";
constexpr auto kCheckIntervalMs = 90 * 60 * 1000; // 1.5 hours (90 minutes)
constexpr auto kInitialCheckDelayMs = 12 * 1000;      // 12 seconds after launch

} // namespace

ZenUpdater &ZenUpdater::Instance() {
	static ZenUpdater instance;
	return instance;
}

ZenUpdater::ZenUpdater() {
	connect(&_checkTimer, &QTimer::timeout, this, [this] {
		checkForUpdates(false);
	});
}

void ZenUpdater::init(Window::SessionController *controller) {
	if (controller) {
		_controller = controller;
	}

	QTimer::singleShot(kInitialCheckDelayMs, this, [this] {
		checkForUpdates(false);
		_checkTimer.start(kCheckIntervalMs);
	});

	LOG(("ZenUpdater initialized. Current version: %1 (%2)").arg(kZenVersion).arg(kZenVersionCode));
}

void ZenUpdater::setController(Window::SessionController *controller) {
	_controller = controller;
}

const QString &ZenUpdater::currentVersion() const {
	static const QString v = kZenVersion;
	return v;
}

int ZenUpdater::currentVersionCode() const {
	return kZenVersionCode;
}

bool ZenUpdater::isChecking() const {
	return _isChecking;
}

void ZenUpdater::checkForUpdates(bool manual) {
	if (_isChecking) {
		return;
	}

	_isChecking = true;
	LOG(("ZenUpdater: Checking for updates from %1 (manual: %2)").arg(kUpdateManifestUrl).arg(manual));

	QNetworkRequest req(QUrl(QString(kUpdateManifestUrl)));
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("ZenDesktop/%1").arg(kZenVersion));

	auto *reply = _net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
		_isChecking = false;
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("ZenUpdater: Network error while checking: %1").arg(reply->errorString()));
			if (manual && _controller) {
				_controller->show(Ui::MakeInformBox(
					QString("Ошибка при проверке обновлений:\n%1").arg(reply->errorString())));
			}
			return;
		}

		handleManifest(reply->readAll(), manual);
	});
}

void ZenUpdater::handleManifest(const QByteArray &data, bool manual) {
	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(data, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("ZenUpdater: JSON parse error: %1").arg(err.errorString()));
		return;
	}

	const auto obj = doc.object();
	ZenUpdateInfo info;
	info.version = obj["version"].toString();
	info.versionCode = obj["version_code"].toInt();
	info.downloadUrl = obj["url"].toString();
	info.sha256 = obj["sha256"].toString();

	if (obj.contains("changelog") && obj["changelog"].isArray()) {
		for (const auto &item : obj["changelog"].toArray()) {
			info.changelog.append(item.toString());
		}
	}

	LOG(("ZenUpdater: Manifest parsed. Remote version: %1 (%2), current: %3 (%4)")
		.arg(info.version)
		.arg(info.versionCode)
		.arg(kZenVersion)
		.arg(kZenVersionCode));

	if (info.versionCode > kZenVersionCode) {
		_updateAvailable = true;
		_availableUpdate = info;
		showUpdateModal(info);
	} else if (manual && _controller) {
		_controller->show(Ui::MakeInformBox(
			QString("У вас установлена последняя версия ZenDesktop %1.").arg(kZenVersion)));
	}
}

void ZenUpdater::showChangelog(Window::SessionController *controller) {
	const auto ctrl = controller ? controller : _controller;
	if (!ctrl) {
		return;
	}

	const QString changes = QString::fromUtf8(
		"ZenDesktop v1.0.0 (build 2601) • RelayCore 2.4\n\n"
		"• Первый публичный релиз ZenDesktop (Windows x64)\n"
		"• Ghost Mode: скрытие статуса набора текста и отчетов о прочтении\n"
		"• Anti-Recall: сохранение удаленных собеседником сообщений локально\n"
		"• Время с секундами (HH:MM:SS) в баблах сообщений\n"
		"• Отключение приветственных анимированных стикеров в пустых чатах\n"
		"• Download Boost: параллельная загрузка до 16 MTProto сессий\n"
		"• Система значков верификации RelayHooks (dev, staff, supporter, desktop)\n"
		"• Expressive Material 3 скругления баблов (24px / 8px)\n"
		"• Автообновления и ручная проверка через GitHub Releases"
	);

	ctrl->show(Ui::MakeInformBox(changes));
}

void ZenUpdater::showUpdateModal(const ZenUpdateInfo &info) {
	if (!_controller) {
		LOG(("ZenUpdater: Cannot show modal, controller is null"));
		return;
	}

	QString changes;
	if (!info.changelog.isEmpty()) {
		changes = "\n\nЧто нового:\n";
		for (const auto &ch : info.changelog) {
			changes += QString("• %1\n").arg(ch);
		}
	}

	const auto text = QString(
		"Доступна новая версия ZenDesktop %1!\n"
		"Текущая версия: %2%3\n\n"
		"Загрузить и установить обновление сейчас?")
		.arg(info.version)
		.arg(kZenVersion)
		.arg(changes);

	_controller->show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [this, info] {
			downloadAndInstall(info);
		},
		.cancelled = [] {},
		.confirmText = "Обновить",
		.cancelText = "Позже",
		.title = "Обновление ZenDesktop",
	}));
}

void ZenUpdater::downloadAndInstall(const ZenUpdateInfo &info) {
	if (info.downloadUrl.isEmpty()) {
		LOG(("ZenUpdater: Download URL is empty"));
		return;
	}

	const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const auto targetPath = QDir(tempDir).filePath("ZenDesktop_update.exe");

	LOG(("ZenUpdater: Starting download %1 -> %2").arg(info.downloadUrl).arg(targetPath));

	if (_controller) {
		_controller->show(Ui::MakeInformBox(
			"Загрузка обновления началась в фоне. После завершения ZenDesktop перезапустится автоматически."));
	}

	startDownload(info.downloadUrl, targetPath);
}

void ZenUpdater::startDownload(const QString &url, const QString &targetPath) {
	QNetworkRequest req(QUrl(url));
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("ZenDesktop/%1").arg(kZenVersion));

	auto *reply = _net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath] {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("ZenUpdater: Download failed: %1").arg(reply->errorString()));
			if (_controller) {
				_controller->show(Ui::MakeInformBox(
					QString("Не удалось загрузить обновление:\n%1").arg(reply->errorString())));
			}
			return;
		}

		QFile outFile(targetPath);
		if (!outFile.open(QIODevice::WriteOnly)) {
			LOG(("ZenUpdater: Failed to open %1 for writing").arg(targetPath));
			return;
		}

		outFile.write(reply->readAll());
		outFile.close();

		LOG(("ZenUpdater: Download finished successfully (%1 bytes). Applying update...").arg(outFile.size()));
		launchUpdaterAndExit(targetPath);
	});
}

void ZenUpdater::launchUpdaterAndExit(const QString &newExePath) {
	const auto currentExePath = QCoreApplication::applicationFilePath();
	const auto pid = QCoreApplication::applicationPid();
	const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const auto scriptPath = QDir(tempDir).filePath("zen_apply_update.bat");

	// Batch script that waits for current process to terminate, replaces binary, and restarts it
	QFile scriptFile(scriptPath);
	if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream out(&scriptFile);
		out << "@echo off\r\n";
		out << "timeout /t 1 /nobreak >nul\r\n";
		out << ":wait_loop\r\n";
		out << QString("tasklist /fi \"pid eq %1\" | find \"%1\" >nul\r\n").arg(pid);
		out << "if %errorlevel% equ 0 (\r\n";
		out << "    timeout /t 1 /nobreak >nul\r\n";
		out << "    goto wait_loop\r\n";
		out << ")\r\n";
		out << QString("copy /y \"%1\" \"%2\" >nul\r\n").arg(QDir::toNativeSeparators(newExePath)).arg(QDir::toNativeSeparators(currentExePath));
		out << QString("del \"%1\" >nul 2>&1\r\n").arg(QDir::toNativeSeparators(newExePath));
		out << QString("start \"\" \"%1\"\r\n").arg(QDir::toNativeSeparators(currentExePath));
		out << "del \"%~f0\" >nul 2>&1\r\n";
		scriptFile.close();
	}

	LOG(("ZenUpdater: Starting updater script %1 and quitting application...").arg(scriptPath));
	QProcess::startDetached("cmd.exe", QStringList{ "/c", scriptPath });
	QCoreApplication::quit();
}

} // namespace Relay
