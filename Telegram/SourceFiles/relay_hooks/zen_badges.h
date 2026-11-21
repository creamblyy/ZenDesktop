/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QMap>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <cstdint>
#include <optional>

namespace Window {
class SessionController;
} // namespace Window

namespace Relay {

enum class ZenBadgeType : uint8_t {
	Desktop = 0,    // PC Monitor (Zen Core Client)
	Developer = 1,  // >_ Terminal (Maintainer / Developer)
	Staff = 2,      // Lightning Bolt (Verified / Team)
	Supporter = 3,  // 4-Point Spark (VIP / Sponsor)
};

struct ZenBadgeInfo {
	ZenBadgeType type = ZenBadgeType::Desktop;
	QString title;
	QString customColor;
};

class ZenBadges {
public:
	static ZenBadges &Instance();

	// Badge Assignment API (for specific users, channels, bots)
	void assignBadge(uint64_t peerId, ZenBadgeType type, const QString &title = QString());
	void removeBadge(uint64_t peerId);
	[[nodiscard]] bool hasBadge(uint64_t peerId) const;
	[[nodiscard]] std::optional<ZenBadgeInfo> getBadge(uint64_t peerId) const;

	// Remote / Local JSON synchronization
	bool loadFromJson(const QString &json);
	bool loadFromFile(const QString &filePath);
	[[nodiscard]] QString exportToJson() const;
	void syncFromRemote(Window::SessionController *controller = nullptr);
	void refresh(Window::SessionController *controller = nullptr) {
		syncFromRemote(controller);
	}

	// Drawing helper in chat lists and headers
	int drawBadge(QPainter &p, const QRect &rectForName, int nameWidth, int outerWidth, uint64_t peerId);

	[[nodiscard]] const QImage &getBadgeImage(ZenBadgeType type) const;

private:
	ZenBadges();
	~ZenBadges() = default;

	ZenBadges(const ZenBadges &) = delete;
	ZenBadges &operator=(const ZenBadges &) = delete;

	void loadDefaultBadges();
	void initBadgeImages();

	QMap<uint64_t, ZenBadgeInfo> _badges;
	mutable QMap<ZenBadgeType, QImage> _badgeImages;
};

} // namespace Relay
