/*
This file is part of Telegram Desktop fork with RelayHooks.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Relay {

enum class AutoUpdateMode : int {
	DownloadOnly = 0,
	Disabled = 1,
	AutoInstall = 2,
};

struct HookStats {
	uint64_t deletedMessagesIntercepted = 0;
	uint64_t typingBlocked = 0;
	uint64_t readHistoryBlocked = 0;
	uint64_t storiesBlocked = 0;
	uint64_t messagesModified = 0;
};

class RelayHooks {
public:
	using DeleteHookFn = std::function<bool(uint64_t peerId, const std::vector<int32_t> &messageIds)>;
	using SendHookFn = std::function<bool(QString &text)>;
	using TypingHookFn = std::function<bool(uint64_t peerId)>;
	using ReadHistoryHookFn = std::function<bool(uint64_t peerId, int32_t maxId)>;

	static RelayHooks &Instance();

	// Privacy & Protection
	[[nodiscard]] bool isGhostModeEnabled() const;
	void setGhostModeEnabled(bool enabled);

	[[nodiscard]] bool isAntiRecallEnabled() const;
	void setAntiRecallEnabled(bool enabled);

	[[nodiscard]] bool isTypingHidden() const;
	void setTypingHidden(bool hidden);

	[[nodiscard]] bool isReadHistoryHidden() const;
	void setReadHistoryHidden(bool hidden);

	[[nodiscard]] bool isStoriesStealthEnabled() const;
	void setStoriesStealthEnabled(bool enabled);
	[[nodiscard]] bool isReadStoriesHidden() const;

	[[nodiscard]] bool isNoSponsoredEnabled() const;
	void setNoSponsoredEnabled(bool enabled);

	[[nodiscard]] bool isHidePhoneNumberEnabled() const;
	void setHidePhoneNumberEnabled(bool enabled);

	// Appearance & Material 3
	[[nodiscard]] bool isTimeWithSecondsEnabled() const;
	void setTimeWithSecondsEnabled(bool enabled);

	[[nodiscard]] bool isGreetingStickerDisabled() const;
	void setGreetingStickerDisabled(bool disabled);

	[[nodiscard]] bool isShowPeerIdEnabled() const;
	void setShowPeerIdEnabled(bool enabled);

	[[nodiscard]] bool isMaterial3BubblesEnabled() const;
	void setMaterial3BubblesEnabled(bool enabled);

	[[nodiscard]] bool isChatListDividersEnabled() const;
	void setChatListDividersEnabled(bool enabled);

	[[nodiscard]] bool isSmoothScrollEnabled() const;
	void setSmoothScrollEnabled(bool enabled);

	[[nodiscard]] bool isMaterial3IconsEnabled() const;
	void setMaterial3IconsEnabled(bool enabled);

	// Anti-Recall Visual State
	[[nodiscard]] bool isMessageDeleted(uint64_t peerId, int32_t messageId) const;
	void markMessageDeleted(uint64_t peerId, int32_t messageId);

	// Network
	[[nodiscard]] bool isDownloadBoostEnabled() const;
	void setDownloadBoostEnabled(bool enabled);

	// Updates & Backup
	[[nodiscard]] AutoUpdateMode autoUpdateMode() const;
	void setAutoUpdateMode(AutoUpdateMode mode);
	[[nodiscard]] QString autoUpdateModeString() const;

	void saveSettings();
	void loadSettings();
	[[nodiscard]] bool exportSettings(const QString &filePath) const;
	bool importSettings(const QString &filePath);
	void resetSettings();

	// Core Hook Interceptors
	bool handleMessagesDeleted(uint64_t peerId, const std::vector<int32_t> &messageIds);
	bool handleNonChannelMessagesDeleted(const std::vector<int32_t> &messageIds);
	bool handleBeforeSendMessage(QString &text);
	bool handleSendTyping(uint64_t peerId);
	bool handleReadHistory(uint64_t peerId, int32_t maxId);

	// Hook Registration API
	void registerDeleteHook(const std::string &id, DeleteHookFn fn);
	void registerSendHook(const std::string &id, SendHookFn fn);
	void registerTypingHook(const std::string &id, TypingHookFn fn);
	void registerReadHistoryHook(const std::string &id, ReadHistoryHookFn fn);
	void unregisterHook(const std::string &id);

	// Diagnostics & Stats
	[[nodiscard]] const HookStats &stats() const;
	void resetStats();

private:
	RelayHooks();
	~RelayHooks() = default;

	RelayHooks(const RelayHooks &) = delete;
	RelayHooks &operator=(const RelayHooks &) = delete;

	bool _ghostModeEnabled = false;
	bool _antiRecallEnabled = true; // Anti-recall on by default
	bool _hideTyping = false;
	bool _hideReadHistory = false;
	bool _storiesStealth = false;
	bool _noSponsored = true; // Block ads by default
	bool _hidePhoneNumber = false;
	bool _timeWithSeconds = false;
	bool _greetingStickerDisabled = false;
	bool _showPeerId = true;
	bool _material3Bubbles = true;
	bool _chatListDividers = true;
	bool _smoothScroll = true;
	bool _material3Icons = true;
	bool _downloadBoost = true; // Boost enabled by default
	AutoUpdateMode _autoUpdateMode = AutoUpdateMode::DownloadOnly;

	std::unordered_set<uint64_t> _deletedMessages;
	HookStats _stats;

	std::unordered_map<std::string, DeleteHookFn> _deleteHooks;
	std::unordered_map<std::string, SendHookFn> _sendHooks;
	std::unordered_map<std::string, TypingHookFn> _typingHooks;
	std::unordered_map<std::string, ReadHistoryHookFn> _readHistoryHooks;
};

} // namespace Relay
