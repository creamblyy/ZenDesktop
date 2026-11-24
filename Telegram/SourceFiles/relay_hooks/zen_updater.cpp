/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_updater.h"
#include "relay_hooks/zen_badges.h"
#include "base/weak_ptr.h"
#include "logs.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <chrono>
#include <functional>
#include <memory>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtNetwork/QNetworkRequest>

namespace Relay {
namespace {

constexpr auto kZenVersion = "1.0.0";
constexpr auto kZenVersionCode = 2601;
constexpr auto kUpdateManifestUrl = "https://creamblyy.github.io/ZenDesktop/update.json";
constexpr auto kCheckIntervalMs = 90 * 60 * 1000; // 1.5 hours (90 minutes)
constexpr auto kInitialCheckDelayMs = 12 * 1000;      // 12 seconds after launch

base::weak_ptr<Window::SessionController> gWeakController;

[[nodiscard]] bool ValidateDownloadedExe(
		const QString &path,
		const QString &expectedSha256,
		QString &errorOut) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		errorOut = QString::fromUtf8("Не удалось открыть скачанный файл.");
		return false;
	}
	if (f.size() < 512 * 1024) {
		errorOut = QString::fromUtf8("Файл обновления слишком мал и повреждён.");
		return false;
	}
	const auto magic = f.peek(2);
	if (magic.size() < 2 || magic[0] != 'M' || magic[1] != 'Z') {
		errorOut = QString::fromUtf8("Скачанный файл не является исполняемым файлом Windows.");
		return false;
	}
	const auto expected = expectedSha256.trimmed().toLower();
	if (!expected.isEmpty()) {
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!f.atEnd()) {
			const auto chunk = f.read(256 * 1024);
			if (!chunk.isEmpty()) {
				hash.addData(chunk);
			}
		}
		const auto actual = QString::fromLatin1(hash.result().toHex()).toLower();
		if (actual != expected) {
			errorOut = QString::fromUtf8("Контрольная сумма SHA-256 обновления не совпала.");
			return false;
		}
	}
	return true;
}

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
		gWeakController = base::make_weak(controller);
	}

	QTimer::singleShot(kInitialCheckDelayMs, this, [this] {
		checkForUpdates(false);
		_checkTimer.start(kCheckIntervalMs);
	});

	LOG(("ZenUpdater initialized. Current version: %1 (%2)").arg(kZenVersion).arg(kZenVersionCode));
}

void ZenUpdater::setController(Window::SessionController *controller) {
	_controller = controller;
	gWeakController = controller
		? base::make_weak(controller)
		: base::weak_ptr<Window::SessionController>();
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

bool ZenUpdater::isAutoCheckEnabled() const {
	return _autoCheck;
}

void ZenUpdater::setAutoCheckEnabled(bool enabled) {
	_autoCheck = enabled;
	if (enabled) {
		if (!_checkTimer.isActive()) {
			_checkTimer.start(kCheckIntervalMs);
		}
	} else {
		_checkTimer.stop();
	}
}

void ZenUpdater::checkForUpdates(bool manual) {
	if (_isChecking) {
		return;
	}

	_isChecking = true;
	const auto envUrl = qEnvironmentVariable("ZEN_UPDATE_URL");
	const auto manifestUrl = envUrl.isEmpty()
		? QString::fromLatin1(kUpdateManifestUrl)
		: envUrl;
	LOG(("ZenUpdater: Checking for updates from %1 (manual: %2)").arg(manifestUrl).arg(manual));

	auto req = QNetworkRequest(QUrl(manifestUrl));
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(std::chrono::milliseconds(15000));
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("ZenDesktop/%1").arg(kZenVersion));

	auto *reply = _net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
		_isChecking = false;
		reply->deleteLater();

		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
			LOG(("ZenUpdater: Network error while checking (HTTP %1): %2").arg(status).arg(reply->errorString()));
			if (manual) {
				if (const auto ctrl = gWeakController.get()) {
					ctrl->show(Ui::MakeInformBox(
						QString::fromUtf8("Ошибка при проверке обновлений:\n%1").arg(reply->errorString())));
				}
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
	} else if (manual) {
		if (const auto ctrl = gWeakController.get()) {
			ctrl->show(Ui::MakeInformBox(
				QString::fromUtf8("У вас установлена последняя версия ZenDesktop %1.").arg(kZenVersion)));
		}
	}
}

void ZenUpdater::showChangelog(Window::SessionController *controller) {
	const auto ctrl = controller ? controller : gWeakController.get();
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
	const auto ctrl = gWeakController.get();
	if (!ctrl) {
		LOG(("ZenUpdater: Cannot show modal, controller is null"));
		return;
	}

	const auto logoImg = ZenBadges::Instance().getBadgeImage(ZenBadgeType::Desktop);

	ctrl->show(Box([this, info, logoImg](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);

		const auto layout = box->verticalLayout();
		const auto cardWidth = st::boxWideWidth - 40;

		// Hero header card
		auto header = object_ptr<Ui::RpWidget>(layout);
		const auto rawHeader = header.data();
		rawHeader->resize(cardWidth, 88);
		rawHeader->setNaturalWidth(cardWidth);
		rawHeader->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(rawHeader);
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::SmoothPixmapTransform);

			const auto rect = QRectF(rawHeader->rect());
			QLinearGradient grad(rect.topLeft(), rect.bottomRight());
			grad.setColorAt(0.0, QColor(0x2b, 0x3c, 0xff));
			grad.setColorAt(1.0, QColor(0x4b, 0x5f, 0xff));
			p.setPen(Qt::NoPen);
			p.setBrush(grad);
			p.drawRoundedRect(rect, 14.0, 14.0);

			// Subtle icon backdrop circle + logo
			const QRectF badgeCircle(16.0, 17.0, 54.0, 54.0);
			p.setBrush(QColor(255, 255, 255, 38));
			p.drawEllipse(badgeCircle);
			if (!logoImg.isNull()) {
				p.drawImage(QRectF(23.0, 24.0, 40.0, 40.0), logoImg);
			}

			// Title + subtitle
			const auto titleFont = st::boxTitle.style.font;
			p.setFont(titleFont);
			p.setPen(QColor(255, 255, 255));
			p.drawText(
				QRectF(82.0, 18.0, rect.width() - 96.0, 26.0),
				Qt::AlignLeft | Qt::AlignVCenter,
				QString::fromUtf8("Обновление ZenDesktop"));

			// Version pill: v1.0.0 -> v1.0.1
			const auto pillText = QString("v%1  →  v%2")
				.arg(kZenVersion)
				.arg(info.version);
			const auto subFont = st::semiboldFont;
			const auto textW = subFont->width(pillText);
			const QRectF pillRect(82.0, 48.0, textW + 20.0, 22.0);
			p.setBrush(QColor(255, 255, 255, 44));
			p.drawRoundedRect(pillRect, 11.0, 11.0);
			p.setFont(subFont);
			p.setPen(QColor(244, 239, 230));
			p.drawText(pillRect, Qt::AlignCenter, pillText);
		}, rawHeader->lifetime());
		layout->add(std::move(header), QMargins(20, 18, 20, 12));

		// Changelog card
		auto card = object_ptr<Ui::RpWidget>(layout);
		const auto rawCard = card.data();
		rawCard->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(rawCard);
			p.setRenderHint(QPainter::Antialiasing);
			auto bg = st::windowFg->c;
			bg.setAlphaF(0.05);
			auto border = st::windowFg->c;
			border.setAlphaF(0.09);
			p.setBrush(bg);
			p.setPen(QPen(border, 1.0));
			p.drawRoundedRect(QRectF(rawCard->rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12.0, 12.0);
		}, rawCard->lifetime());

		const auto cardLayout = Ui::CreateChild<Ui::VerticalLayout>(rawCard);
		const auto cardTitle = cardLayout->add(
			object_ptr<Ui::FlatLabel>(
				cardLayout,
				rpl::single(QString::fromUtf8("Что нового")),
				st::boxLabel),
			QMargins(16, 12, 16, 6));
		cardTitle->setTextColorOverride(st::windowSubTextFg->c);
		cardTitle->setSelectable(false);

		const auto changesList = info.changelog.isEmpty()
			? QStringList{ QString::fromUtf8("Улучшения стабильности и производительности") }
			: info.changelog;
		for (int idx = 0; idx < changesList.size(); ++idx) {
			const auto item = cardLayout->add(
				object_ptr<Ui::FlatLabel>(
					cardLayout,
					rpl::single(QString("• %1").arg(changesList[idx])),
					st::boxLabel),
				QMargins(16, 2, 16, (idx + 1 == changesList.size()) ? 14 : 4));
			item->resizeToWidth(cardWidth - 32);
			item->setSelectable(false);
		}
		cardLayout->resizeToWidth(cardWidth);
		rawCard->resize(cardWidth, cardLayout->height());
		rawCard->setNaturalWidth(cardWidth);
		layout->add(std::move(card), QMargins(20, 0, 20, 10));

		// Hidden-until-download progress section
		const auto progressRatio = std::make_shared<double>(0.0);
		const auto activeReply = std::make_shared<QPointer<QNetworkReply>>();
		const auto activeFile = std::make_shared<std::shared_ptr<QFile>>();
		const auto downloadCompleted = std::make_shared<bool>(false);
		const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
		const auto targetPath = QDir(tempDir).filePath("ZenDesktop_update.exe");

		auto progressWrapObj = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout));
		const auto progressWrap = progressWrapObj.data();
		const auto progressLayout = progressWrap->entity();

		const auto statusLabel = progressLayout->add(
			object_ptr<Ui::FlatLabel>(
				progressLayout,
				rpl::single(QString::fromUtf8("Подготовка к загрузке...")),
				st::defaultFlatLabel),
			QMargins(20, 4, 20, 6));
		statusLabel->resizeToWidth(cardWidth);
		statusLabel->setSelectable(false);

		auto bar = object_ptr<Ui::RpWidget>(progressLayout);
		const auto rawBar = bar.data();
		rawBar->resize(cardWidth, 8);
		rawBar->setNaturalWidth(cardWidth);
		rawBar->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(rawBar);
			p.setRenderHint(QPainter::Antialiasing);
			const auto r = QRectF(rawBar->rect());
			auto track = st::windowFg->c;
			track.setAlphaF(0.10);
			p.setPen(Qt::NoPen);
			p.setBrush(track);
			p.drawRoundedRect(r, 4.0, 4.0);

			const auto w = std::max(0.0, std::min(1.0, *progressRatio)) * r.width();
			if (w > 1.0) {
				const QRectF fillRect(0.0, 0.0, w, r.height());
				QLinearGradient g(fillRect.topLeft(), fillRect.topRight());
				g.setColorAt(0.0, QColor(0x2b, 0x3c, 0xff));
				g.setColorAt(1.0, QColor(0x4b, 0x5f, 0xff));
				p.setBrush(g);
				p.drawRoundedRect(fillRect, 4.0, 4.0);
			}
		}, rawBar->lifetime());
		progressLayout->add(std::move(bar), QMargins(20, 0, 20, 12));

		progressWrap->toggle(false, anim::type::instant);
		layout->add(std::move(progressWrapObj));

		box->lifetime().add([activeReply, activeFile, downloadCompleted, targetPath] {
			if (!*downloadCompleted) {
				if (*activeReply) {
					(*activeReply)->abort();
				}
				if (*activeFile && (*activeFile)->isOpen()) {
					(*activeFile)->close();
				}
				QFile::remove(targetPath);
			}
		});

		const auto weakBox = QPointer<Ui::GenericBox>(box);
		const auto startBoxDownload = std::make_shared<std::function<void()>>();
		box->lifetime().add([startBoxDownload] {});
		const auto weakRetry = std::weak_ptr<std::function<void()>>(startBoxDownload);

		*startBoxDownload = [=] {
			if (!weakBox || info.downloadUrl.isEmpty()) {
				return;
			}
			*downloadCompleted = false;
			*progressRatio = 0.0;
			statusLabel->setTextColorOverride(std::nullopt);
			statusLabel->setText(QString::fromUtf8("Подключение к серверу..."));
			rawBar->update();
			progressWrap->toggle(true, anim::type::normal);

			weakBox->clearButtons();
			weakBox->addButton(tr::lng_cancel(), [=] {
				if (*activeReply) {
					(*activeReply)->abort();
				}
				if (weakBox) {
					weakBox->closeBox();
				}
			});

			auto outFile = std::make_shared<QFile>(targetPath);
			if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				LOG(("ZenUpdater: Failed to open %1 for writing").arg(targetPath));
				statusLabel->setTextColorOverride(QColor(235, 87, 87));
				statusLabel->setText(QString::fromUtf8("Не удалось открыть временный файл обновления."));
				weakBox->clearButtons();
				weakBox->addButton(tr::lng_close(), [=] { weakBox->closeBox(); });
				return;
			}
			*activeFile = outFile;
			const auto writeOk = std::make_shared<bool>(true);

			LOG(("ZenUpdater: Starting download %1 -> %2").arg(info.downloadUrl).arg(targetPath));

			auto req = QNetworkRequest(QUrl(info.downloadUrl));
			req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
			req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
			req.setTransferTimeout(std::chrono::milliseconds(60000));
			req.setHeader(QNetworkRequest::UserAgentHeader, QString("ZenDesktop/%1").arg(kZenVersion));

			auto *reply = _net.get(req);
			*activeReply = reply;

			connect(reply, &QNetworkReply::readyRead, this, [reply, outFile, writeOk] {
				const auto chunk = reply->readAll();
				if (!chunk.isEmpty() && outFile->isOpen()) {
					if (outFile->write(chunk) != chunk.size()) {
						*writeOk = false;
					}
				}
			});

			connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total) {
				if (!weakBox) {
					return;
				}
				if (total > 0) {
					const auto ratio = double(received) / double(total);
					*progressRatio = ratio;
					const auto pct = int(ratio * 100.0);
					const auto recMb = double(received) / (1024.0 * 1024.0);
					const auto totMb = double(total) / (1024.0 * 1024.0);
					statusLabel->setText(QString::fromUtf8("Загрузка: %1% (%2 / %3 МБ)")
						.arg(pct)
						.arg(recMb, 0, 'f', 1)
						.arg(totMb, 0, 'f', 1));
				} else {
					const auto recMb = double(received) / (1024.0 * 1024.0);
					statusLabel->setText(QString::fromUtf8("Загрузка: %1 МБ").arg(recMb, 0, 'f', 1));
				}
				rawBar->update();
			});

			connect(reply, &QNetworkReply::finished, this, [this, info, reply, targetPath, outFile, writeOk, downloadCompleted, weakBox, statusLabel, progressRatio, rawBar, weakRetry] {
				reply->deleteLater();
				if (outFile->isOpen()) {
					const auto tail = reply->readAll();
					if (!tail.isEmpty() && outFile->write(tail) != tail.size()) {
						*writeOk = false;
					}
					outFile->close();
				}

				const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				if (reply->error() == QNetworkReply::OperationCanceledError) {
					QFile::remove(targetPath);
					return;
				}
				if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300 || !*writeOk) {
					QFile::remove(targetPath);
					LOG(("ZenUpdater: Download failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
					if (weakBox) {
						statusLabel->setTextColorOverride(QColor(235, 87, 87));
						statusLabel->setText(QString::fromUtf8("Ошибка загрузки: %1").arg(reply->errorString()));
						weakBox->clearButtons();
						weakBox->addButton(rpl::single(QString::fromUtf8("Повторить")), [weakRetry] {
							if (const auto fn = weakRetry.lock(); fn && *fn) {
								(*fn)();
							}
						});
						weakBox->addButton(tr::lng_close(), [=] { weakBox->closeBox(); });
					}
					return;
				}

				QString validationErr;
				if (!ValidateDownloadedExe(targetPath, info.sha256, validationErr)) {
					QFile::remove(targetPath);
					LOG(("ZenUpdater: Validation failed: %1").arg(validationErr));
					if (weakBox) {
						statusLabel->setTextColorOverride(QColor(235, 87, 87));
						statusLabel->setText(validationErr);
						weakBox->clearButtons();
						weakBox->addButton(rpl::single(QString::fromUtf8("Повторить")), [weakRetry] {
							if (const auto fn = weakRetry.lock(); fn && *fn) {
								(*fn)();
							}
						});
						weakBox->addButton(tr::lng_close(), [=] { weakBox->closeBox(); });
					}
					return;
				}

				*downloadCompleted = true;
				*progressRatio = 1.0;
				if (weakBox) {
					statusLabel->setText(QString::fromUtf8("Установка обновления..."));
					rawBar->update();
				}
				LOG(("ZenUpdater: Download finished successfully (%1 bytes). Applying update...").arg(QFileInfo(targetPath).size()));
				launchUpdaterAndExit(targetPath);
			});
		};

		box->addButton(rpl::single(QString::fromUtf8("Обновить")), [weakRetry] {
			if (const auto fn = weakRetry.lock(); fn && *fn) {
				(*fn)();
			}
		});
		box->addButton(rpl::single(QString::fromUtf8("Позже")), [=] { box->closeBox(); });
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

	if (const auto ctrl = gWeakController.get()) {
		ctrl->show(Ui::MakeInformBox(
			QString::fromUtf8("Загрузка обновления началась в фоне. После завершения ZenDesktop перезапустится автоматически.")));
	}

	startDownload(info.downloadUrl, targetPath);
}

void ZenUpdater::startDownload(const QString &url, const QString &targetPath) {
	auto outFile = std::make_shared<QFile>(targetPath);
	if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOG(("ZenUpdater: Failed to open %1 for writing").arg(targetPath));
		return;
	}
	const auto writeOk = std::make_shared<bool>(true);

	auto req = QNetworkRequest(QUrl(url));
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(std::chrono::milliseconds(60000));
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("ZenDesktop/%1").arg(kZenVersion));

	auto *reply = _net.get(req);
	connect(reply, &QNetworkReply::readyRead, this, [reply, outFile, writeOk] {
		const auto chunk = reply->readAll();
		if (!chunk.isEmpty() && outFile->isOpen()) {
			if (outFile->write(chunk) != chunk.size()) {
				*writeOk = false;
			}
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath, outFile, writeOk] {
		reply->deleteLater();
		if (outFile->isOpen()) {
			const auto tail = reply->readAll();
			if (!tail.isEmpty() && outFile->write(tail) != tail.size()) {
				*writeOk = false;
			}
			outFile->close();
		}

		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300 || !*writeOk) {
			QFile::remove(targetPath);
			LOG(("ZenUpdater: Download failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
			if (const auto ctrl = gWeakController.get()) {
				ctrl->show(Ui::MakeInformBox(
					QString::fromUtf8("Не удалось загрузить обновление:\n%1").arg(reply->errorString())));
			}
			return;
		}

		QString validationErr;
		if (!ValidateDownloadedExe(targetPath, _availableUpdate.sha256, validationErr)) {
			QFile::remove(targetPath);
			LOG(("ZenUpdater: Background download validation failed: %1").arg(validationErr));
			if (const auto ctrl = gWeakController.get()) {
				ctrl->show(Ui::MakeInformBox(validationErr));
			}
			return;
		}

		LOG(("ZenUpdater: Download finished successfully (%1 bytes). Applying update...").arg(QFileInfo(targetPath).size()));
		launchUpdaterAndExit(targetPath);
	});
}

void ZenUpdater::launchUpdaterAndExit(const QString &newExePath) {
	const auto currentExePath = QCoreApplication::applicationFilePath();
	const auto pid = QCoreApplication::applicationPid();
	QStringList appArgs;
	const auto workDir = QDir::cleanPath(cWorkingDir());
	const auto exeDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
	if (!workDir.isEmpty() && workDir.compare(exeDir, Qt::CaseInsensitive) != 0) {
		appArgs << QString("-workdir") << QDir::toNativeSeparators(workDir);
	}
	const auto updaterPath = QDir(QCoreApplication::applicationDirPath()).filePath("Updater.exe");
	if (QFileInfo::exists(updaterPath)) {
		auto args = QStringList{
			"--pid",
			QString::number(pid),
			"--source",
			QDir::toNativeSeparators(newExePath),
			"--target",
			QDir::toNativeSeparators(currentExePath),
		};
		if (!appArgs.isEmpty()) {
			args << "--" << appArgs;
		}
		LOG(("ZenUpdater: Starting native updater %1 and quitting application...").arg(updaterPath));
		if (QProcess::startDetached(updaterPath, args)) {
			QCoreApplication::quit();
			return;
		}
		LOG(("ZenUpdater: Failed to launch Updater.exe, falling back to batch script."));
	}

	const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const auto scriptPath = QDir(tempDir).filePath("zen_apply_update.bat");

	QStringList quotedArgs;
	for (const auto &arg : appArgs) {
		auto escaped = arg;
		escaped.replace('"', "\\\"");
		quotedArgs.append(arg.contains(' ') ? QString("\"%1\"").arg(escaped) : escaped);
	}
	const auto joinedArgs = quotedArgs.isEmpty()
		? QString()
		: QString(" %1").arg(quotedArgs.join(' '));

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
		out << QString("start \"\" \"%1\"%2\r\n").arg(QDir::toNativeSeparators(currentExePath)).arg(joinedArgs);
		out << "del \"%~f0\" >nul 2>&1\r\n";
		scriptFile.close();
	}

	LOG(("ZenUpdater: Starting updater script %1 and quitting application...").arg(scriptPath));
	QProcess::startDetached("cmd.exe", QStringList{ "/c", scriptPath });
	QCoreApplication::quit();
}

} // namespace Relay
