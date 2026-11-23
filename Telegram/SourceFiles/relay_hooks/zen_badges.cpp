/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_badges.h"
#include "relay_hooks/zen_badges_data.h"
#include "logs.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/rp_widget.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtGui/QPainterPath>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Relay {
namespace {

constexpr auto kBadgeWidth = 16;
constexpr auto kBadgeHeight = 16;
constexpr auto kBadgeSpacing = 4;
constexpr auto kBarePeerMask = uint64_t(0xFFFFFFFFFFFFULL);

[[nodiscard]] uint64_t NormalizePeerId(uint64_t peerId) {
	return peerId & kBarePeerMask;
}

ZenBadgeType ParseBadgeType(const QString &str) {
	if (str == "dev" || str == "developer") {
		return ZenBadgeType::Developer;
	} else if (str == "staff" || str == "verified") {
		return ZenBadgeType::Staff;
	} else if (str == "supporter" || str == "vip" || str == "sponsor") {
		return ZenBadgeType::Supporter;
	}
	return ZenBadgeType::Desktop;
}

QString BadgeTypeToString(ZenBadgeType type) {
	switch (type) {
	case ZenBadgeType::Developer: return "dev";
	case ZenBadgeType::Staff: return "staff";
	case ZenBadgeType::Supporter: return "supporter";
	case ZenBadgeType::Desktop:
	default: return "desktop";
	}
}

} // namespace

ZenBadges &ZenBadges::Instance() {
	static ZenBadges instance;
	return instance;
}

ZenBadges::ZenBadges() {
	initBadgeImages();
	loadDefaultBadges();
	const auto exeJson = QCoreApplication::applicationDirPath() + "/zen_badges.json";
	if (!loadFromFile(exeJson)) {
		loadFromFile("zen_badges.json");
	}
	LOG(("ZenBadges initialized. Total assigned badges: %1").arg(_badges.size()));
}

void ZenBadges::initBadgeImages() {
	const auto loadEmbedded = [&](
			ZenBadgeType type,
			const unsigned char *data,
			int size) {
		QImage img;
		if (img.loadFromData(data, size, "PNG")) {
			_badgeImages[type] = img;
		}
	};
	loadEmbedded(ZenBadgeType::Desktop, kBadgeDesktopPng, int(sizeof(kBadgeDesktopPng)));
	loadEmbedded(ZenBadgeType::Developer, kBadgeDevPng, int(sizeof(kBadgeDevPng)));
	loadEmbedded(ZenBadgeType::Staff, kBadgeStaffPng, int(sizeof(kBadgeStaffPng)));
	loadEmbedded(ZenBadgeType::Supporter, kBadgeSupporterPng, int(sizeof(kBadgeSupporterPng)));

	const auto loadImg = [&](ZenBadgeType type, const QString &name) {
		QImage img;
		if (img.load(name)) {
			_badgeImages[type] = img;
		}
	};

	const auto exeDir = QCoreApplication::applicationDirPath() + "/";
	loadImg(ZenBadgeType::Desktop, exeDir + "relay_hooks/badges/badge_desktop_small.png");
	loadImg(ZenBadgeType::Developer, exeDir + "relay_hooks/badges/badge_dev_small.png");
	loadImg(ZenBadgeType::Staff, exeDir + "relay_hooks/badges/badge_staff_small.png");
	loadImg(ZenBadgeType::Supporter, exeDir + "relay_hooks/badges/badge_supporter_small.png");
}

void ZenBadges::loadDefaultBadges() {
	assignBadge(532137983ULL, ZenBadgeType::Developer, "Zen Developer");
	assignBadge(3721819870ULL, ZenBadgeType::Desktop, "Официальный ресурс ZenDesktop");
	assignBadge(4431413090ULL, ZenBadgeType::Desktop, "Официальный ресурс ZenDesktop");
}

void ZenBadges::assignBadge(uint64_t peerId, ZenBadgeType type, const QString &title) {
	const auto bareId = NormalizePeerId(peerId);
	if (!bareId) {
		return;
	}
	const auto resolvedTitle = !title.isEmpty()
		? title
		: (type == ZenBadgeType::Developer ? "Zen Developer"
			: type == ZenBadgeType::Staff ? "Zen Staff"
			: type == ZenBadgeType::Supporter ? "Zen Supporter"
			: "Zen Client");

	_badges[bareId] = ZenBadgeInfo{
		.type = type,
		.title = resolvedTitle,
	};

	LOG(("ZenBadges: Assigned '%1' badge to peer %2").arg(resolvedTitle).arg(bareId));
}

void ZenBadges::removeBadge(uint64_t peerId) {
	_badges.remove(NormalizePeerId(peerId));
}

bool ZenBadges::hasBadge(uint64_t peerId) const {
	return _badges.contains(NormalizePeerId(peerId));
}

std::optional<ZenBadgeInfo> ZenBadges::getBadge(uint64_t peerId) const {
	const auto it = _badges.find(NormalizePeerId(peerId));
	if (it != _badges.end()) {
		return it.value();
	}
	return std::nullopt;
}

bool ZenBadges::loadFromJson(const QString &json) {
	QJsonParseError error;
	const auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("ZenBadges: Failed to parse JSON: %1").arg(error.errorString()));
		return false;
	}

	const auto root = doc.object();
	const auto parseSection = [&](const QString &sectionName) {
		if (root.contains(sectionName) && root[sectionName].isObject()) {
			const auto obj = root[sectionName].toObject();
			for (auto it = obj.begin(); it != obj.end(); ++it) {
				const auto peerId = it.key().toULongLong();
				if (!peerId) continue;
				if (it.value().isObject()) {
					const auto entry = it.value().toObject();
					const auto type = ParseBadgeType(entry["type"].toString());
					const auto title = entry["title"].toString();
					assignBadge(peerId, type, title);
				}
			}
		}
	};

	parseSection("users");
	parseSection("channels");
	parseSection("badges");

	LOG(("ZenBadges: Successfully loaded badges from JSON. Total: %1").arg(_badges.size()));
	return true;
}

bool ZenBadges::loadFromFile(const QString &filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto content = QString::fromUtf8(file.readAll());
	return loadFromJson(content);
}

QString ZenBadges::exportToJson() const {
	QJsonObject usersObj;
	for (auto it = _badges.begin(); it != _badges.end(); ++it) {
		QJsonObject entry;
		entry["type"] = BadgeTypeToString(it.value().type);
		entry["title"] = it.value().title;
		usersObj[QString::number(it.key())] = entry;
	}

	QJsonObject root;
	root["badges"] = usersObj;

	const auto doc = QJsonDocument(root);
	return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

void ZenBadges::syncFromRemote(Window::SessionController *controller) {
	const auto net = new QNetworkAccessManager();
	auto req = QNetworkRequest(QUrl(QString("https://creamblyy.github.io/ZenDesktop/badges.json")));
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	req.setHeader(QNetworkRequest::UserAgentHeader, "ZenDesktop/1.0.0");

	auto *reply = net->get(req);
	QObject::connect(reply, &QNetworkReply::finished, [this, reply, net, controller] {
		reply->deleteLater();
		net->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("ZenBadges: Remote sync failed: %1").arg(reply->errorString()));
			if (controller) {
				controller->show(Ui::MakeInformBox(
					QString("Не удалось обновить бейджи:\n%1").arg(reply->errorString())));
			}
			return;
		}

		const auto content = QString::fromUtf8(reply->readAll());
		if (loadFromJson(content)) {
			LOG(("ZenBadges: Remote sync successful. Total badges: %1").arg(_badges.size()));
			if (controller) {
				controller->show(Ui::MakeInformBox(
					QString("Бейджи успешно обновлены.\nВсего в реестре: %1").arg(_badges.size())));
			}
		} else {
			if (controller) {
				controller->show(Ui::MakeInformBox(
					"Ошибка обработки полученного реестра бейджей."));
			}
		}
	});
}

const QImage &ZenBadges::getBadgeImage(ZenBadgeType type) const {
	static const QImage empty;
	const auto it = _badgeImages.find(type);
	if (it != _badgeImages.end()) {
		return it.value();
	}
	return empty;
}

int ZenBadges::drawBadge(
		QPainter &p,
		const QRect &rectForName,
		int nameWidth,
		int outerWidth,
		uint64_t peerId) {
	const auto info = getBadge(peerId);
	if (!info) {
		return 0;
	}

	const auto &img = getBadgeImage(info->type);
	const auto targetX = rectForName.x() + std::min(nameWidth + kBadgeSpacing, rectForName.width() - kBadgeWidth);
	const auto targetY = rectForName.y() + (rectForName.height() - kBadgeHeight) / 2;

	if (!img.isNull()) {
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRect(targetX, targetY, kBadgeWidth, kBadgeHeight), img);
		p.restore();
	} else {
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		const auto bg = (info->type == ZenBadgeType::Developer)
			? QColor("#2b3cff")
			: (info->type == ZenBadgeType::Staff)
			? QColor("#10b981")
			: (info->type == ZenBadgeType::Supporter)
			? QColor("#f59e0b")
			: QColor("#2b3cff");
		p.setBrush(bg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(QRectF(targetX, targetY, kBadgeWidth, kBadgeHeight), 4.0, 4.0);

		p.setPen(QPen(QColor("#f4efe6"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		if (info->type == ZenBadgeType::Developer) {
			// Crisp </> diamond/code chevron
			p.drawLine(QPointF(targetX + 6.0, targetY + 5.0), QPointF(targetX + 3.5, targetY + 8.0));
			p.drawLine(QPointF(targetX + 3.5, targetY + 8.0), QPointF(targetX + 6.0, targetY + 11.0));
			p.drawLine(QPointF(targetX + 10.0, targetY + 5.0), QPointF(targetX + 12.5, targetY + 8.0));
			p.drawLine(QPointF(targetX + 12.5, targetY + 8.0), QPointF(targetX + 10.0, targetY + 11.0));
		} else {
			// Crisp Z / check emblem
			p.drawLine(QPointF(targetX + 4.5, targetY + 5.5), QPointF(targetX + 11.5, targetY + 5.5));
			p.drawLine(QPointF(targetX + 11.5, targetY + 5.5), QPointF(targetX + 4.5, targetY + 10.5));
			p.drawLine(QPointF(targetX + 4.5, targetY + 10.5), QPointF(targetX + 11.5, targetY + 10.5));
		}
		p.restore();
	}

	return kBadgeWidth + kBadgeSpacing;
}

void ZenBadges::showBadgeInfoBox(
		Window::SessionController *controller,
		uint64_t peerId,
		const QString &peerName) {
	const auto info = getBadge(peerId);
	if (!controller || !info) {
		return;
	}

	const auto displayName = peerName.isEmpty()
		? QString::number(NormalizePeerId(peerId))
		: peerName;

	QString title;
	QString desc;
	QString typeLabel;
	switch (info->type) {
	case ZenBadgeType::Developer:
		title = QString::fromUtf8("Разработчик ZenDesktop");
		desc = QString::fromUtf8("%1 находится в команде разработчиков ZenDesktop.").arg(displayName);
		typeLabel = QString::fromUtf8("Разработчик ZenDesktop");
		break;
	case ZenBadgeType::Staff:
		title = QString::fromUtf8("Команда ZenDesktop");
		desc = QString::fromUtf8("%1 является участником команды ZenDesktop.").arg(displayName);
		typeLabel = QString::fromUtf8("Команда ZenDesktop");
		break;
	case ZenBadgeType::Supporter:
		title = QString::fromUtf8("Поддержка ZenDesktop");
		desc = QString::fromUtf8("%1 поддерживает развитие ZenDesktop.").arg(displayName);
		typeLabel = QString::fromUtf8("Поддержка ZenDesktop");
		break;
	case ZenBadgeType::Desktop:
	default:
		title = QString::fromUtf8("Официальный ресурс ZenDesktop");
		desc = QString::fromUtf8("%1 — официальный ресурс ZenDesktop.").arg(displayName);
		typeLabel = QString::fromUtf8("Официальный ресурс ZenDesktop");
		break;
	}

	const auto badgeImg = getBadgeImage(info->type);

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		const auto layout = box->verticalLayout();

		auto icon = object_ptr<Ui::RpWidget>(layout);
		const auto rawIcon = icon.data();
		rawIcon->resize(88, 88);
		rawIcon->setNaturalWidth(88);
		rawIcon->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(rawIcon);
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			if (!badgeImg.isNull()) {
				p.drawImage(rawIcon->rect(), badgeImg);
			}
		}, rawIcon->lifetime());
		layout->add(std::move(icon), QMargins(0, 16, 0, 12), style::al_top);

		const auto titleLabel = layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				rpl::single(title),
				st::boxTitle),
			QMargins(24, 0, 24, 6),
			style::al_top);
		titleLabel->setSelectable(false);

		const auto descLabel = layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				rpl::single(desc),
				st::boxLabel),
			QMargins(24, 0, 24, 16),
			style::al_top);
		descLabel->resizeToWidth(st::boxWideWidth - 48);

		const auto addField = [&](const QString &value, const QString &caption) {
			const auto valLabel = layout->add(
				object_ptr<Ui::FlatLabel>(
					layout,
					rpl::single(value),
					st::boxLabel),
				QMargins(24, 6, 24, 0),
				style::al_left);
			valLabel->resizeToWidth(st::boxWideWidth - 48);

			const auto capLabel = layout->add(
				object_ptr<Ui::FlatLabel>(
					layout,
					rpl::single(caption),
					st::defaultFlatLabel),
				QMargins(24, 2, 24, 6),
				style::al_left);
			capLabel->setTextColorOverride(st::windowSubTextFg->c);
		};

		addField(displayName, QString::fromUtf8("Владелец"));
		addField(typeLabel, QString::fromUtf8("Тип бейджа"));

		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Relay
