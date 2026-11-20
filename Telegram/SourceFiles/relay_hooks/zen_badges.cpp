/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_badges.h"
#include "logs.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace Relay {
namespace {

constexpr auto kBadgeWidth = 16;
constexpr auto kBadgeHeight = 16;
constexpr auto kBadgeSpacing = 4;

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
	loadFromFile("zen_badges.json");
	LOG(("ZenBadges initialized. Total assigned badges: %1").arg(_badges.size()));
}

void ZenBadges::initBadgeImages() {
	// Try loading prepared PNG assets
	const auto loadImg = [&](ZenBadgeType type, const QString &name) {
		QImage img;
		if (img.load(name)) {
			_badgeImages[type] = img;
		}
	};

	loadImg(ZenBadgeType::Desktop, "relay_hooks/badges/badge_desktop_small.png");
	loadImg(ZenBadgeType::Developer, "relay_hooks/badges/badge_dev_small.png");
	loadImg(ZenBadgeType::Staff, "relay_hooks/badges/badge_staff_small.png");
	loadImg(ZenBadgeType::Supporter, "relay_hooks/badges/badge_supporter_small.png");
}

void ZenBadges::loadDefaultBadges() {
	// Dev badge assigned to project author
	assignBadge(532137983ULL, ZenBadgeType::Developer, "Zen Developer");
}

void ZenBadges::assignBadge(uint64_t peerId, ZenBadgeType type, const QString &title) {
	const auto resolvedTitle = !title.isEmpty()
		? title
		: (type == ZenBadgeType::Developer ? "Zen Developer"
			: type == ZenBadgeType::Staff ? "Zen Staff"
			: type == ZenBadgeType::Supporter ? "Zen Supporter"
			: "Zen Client");

	_badges[peerId] = ZenBadgeInfo{
		.type = type,
		.title = resolvedTitle,
	};

	LOG(("ZenBadges: Assigned '%1' badge to peer %2").arg(resolvedTitle).arg(peerId));
}

void ZenBadges::removeBadge(uint64_t peerId) {
	_badges.remove(peerId);
}

bool ZenBadges::hasBadge(uint64_t peerId) const {
	return _badges.contains(peerId);
}

std::optional<ZenBadgeInfo> ZenBadges::getBadge(uint64_t peerId) const {
	const auto it = _badges.find(peerId);
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
		p.drawImage(QRect(targetX, targetY, kBadgeWidth, kBadgeHeight), img);
	} else {
		// Fallback vector drawing if PNG not loaded
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		p.setBrush(QColor("#656c73"));
		p.setPen(Qt::NoPen);
		p.drawEllipse(targetX, targetY, kBadgeWidth, kBadgeHeight);
		p.restore();
	}

	return kBadgeWidth + kBadgeSpacing;
}

} // namespace Relay
