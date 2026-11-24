/*
This file is part of Telegram Desktop fork with RelayHooks.
*/
#include "relay_hooks/relay_hooks.h"
#include "logs.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>

namespace Relay {
namespace {

inline uint64_t MakeMessageKey(uint64_t peerId, int32_t messageId) {
	return ((uint64_t)peerId << 24) ^ static_cast<uint32_t>(messageId);
}

} // namespace

RelayHooks &RelayHooks::Instance() {
	static RelayHooks instance;
	return instance;
}

RelayHooks::RelayHooks() {
	loadSettings();
	LOG(("RelayHooks initialized for Windows build."));
}

bool RelayHooks::isGhostModeEnabled() const {
	return _ghostModeEnabled;
}

void RelayHooks::setGhostModeEnabled(bool enabled) {
	_ghostModeEnabled = enabled;
	_hideTyping = enabled;
	_hideReadHistory = enabled;
	_storiesStealth = enabled;
	saveSettings();
	LOG(("RelayHooks: Ghost mode set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isAntiRecallEnabled() const {
	return _antiRecallEnabled;
}

void RelayHooks::setAntiRecallEnabled(bool enabled) {
	_antiRecallEnabled = enabled;
	saveSettings();
	LOG(("RelayHooks: Anti-recall set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isTypingHidden() const {
	return _hideTyping || _ghostModeEnabled;
}

void RelayHooks::setTypingHidden(bool hidden) {
	_hideTyping = hidden;
	saveSettings();
}

bool RelayHooks::isReadHistoryHidden() const {
	return _hideReadHistory || _ghostModeEnabled;
}

void RelayHooks::setReadHistoryHidden(bool hidden) {
	_hideReadHistory = hidden;
	saveSettings();
}

bool RelayHooks::isStoriesStealthEnabled() const {
	return _storiesStealth || _ghostModeEnabled;
}

void RelayHooks::setStoriesStealthEnabled(bool enabled) {
	_storiesStealth = enabled;
	saveSettings();
}

bool RelayHooks::isReadStoriesHidden() const {
	return _storiesStealth || _ghostModeEnabled;
}

bool RelayHooks::isNoSponsoredEnabled() const {
	return _noSponsored;
}

void RelayHooks::setNoSponsoredEnabled(bool enabled) {
	_noSponsored = enabled;
	saveSettings();
	LOG(("RelayHooks: No-Sponsored set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isHidePhoneNumberEnabled() const {
	return _hidePhoneNumber;
}

void RelayHooks::setHidePhoneNumberEnabled(bool enabled) {
	_hidePhoneNumber = enabled;
	saveSettings();
}

bool RelayHooks::isHideSensitiveMediaEnabled() const {
	return _hideSensitiveMedia;
}

void RelayHooks::setHideSensitiveMediaEnabled(bool enabled) {
	_hideSensitiveMedia = enabled;
	saveSettings();
}

bool RelayHooks::isStreamerModeEnabled() const {
	return _streamerMode;
}

void RelayHooks::setStreamerModeEnabled(bool enabled) {
	_streamerMode = enabled;
	saveSettings();
}

bool RelayHooks::isTimeWithSecondsEnabled() const {
	return _timeWithSeconds;
}

void RelayHooks::setTimeWithSecondsEnabled(bool enabled) {
	_timeWithSeconds = enabled;
	saveSettings();
	LOG(("RelayHooks: Time with seconds set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isShowSecondsEnabled() const {
	return isTimeWithSecondsEnabled();
}

void RelayHooks::setShowSecondsEnabled(bool enabled) {
	setTimeWithSecondsEnabled(enabled);
}

bool RelayHooks::isSwissMotionEnabled() const {
	return _swissMotion;
}

void RelayHooks::setSwissMotionEnabled(bool enabled) {
	_swissMotion = enabled;
	saveSettings();
}

bool RelayHooks::isHideMuteIconEnabled() const {
	return _hideMuteIcon;
}

void RelayHooks::setHideMuteIconEnabled(bool enabled) {
	_hideMuteIcon = enabled;
	saveSettings();
}

bool RelayHooks::isCustomBubbleColorEnabled() const {
	return _customBubbleColor;
}

void RelayHooks::setCustomBubbleColorEnabled(bool enabled) {
	_customBubbleColor = enabled;
	saveSettings();
}

bool RelayHooks::isGreetingStickerDisabled() const {
	return _greetingStickerDisabled;
}

void RelayHooks::setGreetingStickerDisabled(bool disabled) {
	_greetingStickerDisabled = disabled;
	saveSettings();
	LOG(("RelayHooks: Greeting sticker disabled set to %1").arg(disabled ? "true" : "false"));
}

bool RelayHooks::isCleanStickersEnabled() const {
	return isGreetingStickerDisabled();
}

void RelayHooks::setCleanStickersEnabled(bool enabled) {
	setGreetingStickerDisabled(enabled);
}

bool RelayHooks::isShowPeerIdEnabled() const {
	return _showPeerId;
}

void RelayHooks::setShowPeerIdEnabled(bool enabled) {
	_showPeerId = enabled;
	saveSettings();
}

bool RelayHooks::isMaterial3BubblesEnabled() const {
	return _material3Bubbles;
}

void RelayHooks::setMaterial3BubblesEnabled(bool enabled) {
	_material3Bubbles = enabled;
	saveSettings();
}

bool RelayHooks::isChatListDividersEnabled() const {
	return _chatListDividers;
}

void RelayHooks::setChatListDividersEnabled(bool enabled) {
	_chatListDividers = enabled;
	saveSettings();
}

bool RelayHooks::isSmoothScrollEnabled() const {
	return _smoothScroll;
}

void RelayHooks::setSmoothScrollEnabled(bool enabled) {
	_smoothScroll = enabled;
	saveSettings();
}

bool RelayHooks::isMaterial3IconsEnabled() const {
	return _material3Icons;
}

void RelayHooks::setMaterial3IconsEnabled(bool enabled) {
	_material3Icons = enabled;
	saveSettings();
}

bool RelayHooks::isMessageDeleted(uint64_t peerId, int32_t messageId) const {
	return _deletedMessages.contains(MakeMessageKey(peerId, messageId))
		|| _deletedMessages.contains(MakeMessageKey(0, messageId));
}

void RelayHooks::markMessageDeleted(uint64_t peerId, int32_t messageId) {
	if (_deletedMessages.size() > 50000) {
		_deletedMessages.clear();
	}
	_deletedMessages.insert(MakeMessageKey(peerId, messageId));
	if (peerId != 0) {
		_deletedMessages.insert(MakeMessageKey(0, messageId));
	}
}

bool RelayHooks::isEditHistoryEnabled() const {
	return _editHistoryEnabled;
}

void RelayHooks::setEditHistoryEnabled(bool enabled) {
	_editHistoryEnabled = enabled;
	saveSettings();
}

void RelayHooks::recordMessageEdit(
		uint64_t peerId,
		int32_t messageId,
		const QString &oldText,
		int64_t timestamp) {
	if (!_editHistoryEnabled || oldText.trimmed().isEmpty()) {
		return;
	}
	if (_editHistory.size() > 4000) {
		_editHistory.erase(_editHistory.begin());
	}
	auto &list = _editHistory[MakeMessageKey(peerId, messageId)];
	if (list.size() >= 50 || (!list.empty() && list.back().text == oldText)) {
		return;
	}
	list.push_back(ZenEditEntry{ .text = oldText, .timestamp = timestamp });
}

std::vector<ZenEditEntry> RelayHooks::getMessageEdits(uint64_t peerId, int32_t messageId) const {
	const auto it = _editHistory.find(MakeMessageKey(peerId, messageId));
	if (it != _editHistory.end()) {
		return it->second;
	}
	return {};
}

bool RelayHooks::isStripExifEnabled() const {
	return _stripExifEnabled;
}

void RelayHooks::setStripExifEnabled(bool enabled) {
	_stripExifEnabled = enabled;
	saveSettings();
}

int RelayHooks::bubbleRadiusLarge() const {
	return _bubbleRadiusLarge;
}

void RelayHooks::setBubbleRadiusLarge(int radius) {
	_bubbleRadiusLarge = std::clamp(radius, 4, 24);
	saveSettings();
}

int RelayHooks::bubbleRadiusSmall() const {
	return _bubbleRadiusSmall;
}

void RelayHooks::setBubbleRadiusSmall(int radius) {
	_bubbleRadiusSmall = std::clamp(radius, 2, 16);
	saveSettings();
}

int RelayHooks::messageLineSpacing() const {
	return _messageLineSpacing;
}

void RelayHooks::setMessageLineSpacing(int spacing) {
	_messageLineSpacing = std::clamp(spacing, 0, 8);
	saveSettings();
}

bool RelayHooks::isLocalTranscribeEnabled() const {
	return _localTranscribeEnabled;
}

void RelayHooks::setLocalTranscribeEnabled(bool enabled) {
	_localTranscribeEnabled = enabled;
	saveSettings();
}

QString RelayHooks::whisperApiUrl() const {
	return _whisperApiUrl;
}

void RelayHooks::setWhisperApiUrl(const QString &url) {
	_whisperApiUrl = url.trimmed();
	saveSettings();
}

QString RelayHooks::whisperApiKey() const {
	return _whisperApiKey;
}

void RelayHooks::setWhisperApiKey(const QString &key) {
	_whisperApiKey = key.trimmed();
	saveSettings();
}

bool RelayHooks::isSmartCacheEnabled() const {
	return _smartCacheEnabled;
}

void RelayHooks::setSmartCacheEnabled(bool enabled) {
	_smartCacheEnabled = enabled;
	saveSettings();
}

int RelayHooks::smartCacheMaxDays() const {
	return _smartCacheMaxDays;
}

void RelayHooks::setSmartCacheMaxDays(int days) {
	_smartCacheMaxDays = std::clamp(days, 1, 90);
	saveSettings();
}

bool RelayHooks::smartCacheCleanStickers() const {
	return _smartCacheCleanStickers;
}

void RelayHooks::setSmartCacheCleanStickers(bool enabled) {
	_smartCacheCleanStickers = enabled;
	saveSettings();
}

bool RelayHooks::smartCacheCleanVoiceRounds() const {
	return _smartCacheCleanVoiceRounds;
}

void RelayHooks::setSmartCacheCleanVoiceRounds(bool enabled) {
	_smartCacheCleanVoiceRounds = enabled;
	saveSettings();
}

bool RelayHooks::smartCacheCleanVideo() const {
	return _smartCacheCleanVideo;
}

void RelayHooks::setSmartCacheCleanVideo(bool enabled) {
	_smartCacheCleanVideo = enabled;
	saveSettings();
}

bool RelayHooks::smartCacheCleanMedia() const {
	return _smartCacheCleanMedia;
}

void RelayHooks::setSmartCacheCleanMedia(bool enabled) {
	_smartCacheCleanMedia = enabled;
	saveSettings();
}

int RelayHooks::smartCacheRamLimitMb() const {
	return _smartCacheRamLimitMb;
}

void RelayHooks::setSmartCacheRamLimitMb(int mb) {
	_smartCacheRamLimitMb = std::clamp(mb, 128, 4096);
	saveSettings();
}

bool RelayHooks::isQuickProxyEnabled() const {
	return _quickProxyEnabled;
}

void RelayHooks::setQuickProxyEnabled(bool enabled) {
	_quickProxyEnabled = enabled;
	saveSettings();
}

bool RelayHooks::isDownloadBoostEnabled() const {
	return _downloadBoost;
}

void RelayHooks::setDownloadBoostEnabled(bool enabled) {
	_downloadBoost = enabled;
	saveSettings();
	LOG(("RelayHooks: Download boost set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isWsProxyEnabled() const {
	return _wsProxy;
}

void RelayHooks::setWsProxyEnabled(bool enabled) {
	_wsProxy = enabled;
	saveSettings();
	LOG(("RelayHooks: WS proxy set to %1").arg(enabled ? "true" : "false"));
}

AutoUpdateMode RelayHooks::autoUpdateMode() const {
	return _autoUpdateMode;
}

void RelayHooks::setAutoUpdateMode(AutoUpdateMode mode) {
	_autoUpdateMode = mode;
	saveSettings();
}

QString RelayHooks::autoUpdateModeString() const {
	switch (_autoUpdateMode) {
	case AutoUpdateMode::DownloadOnly:
		return QString::fromUtf8("Только скачивать");
	case AutoUpdateMode::AutoInstall:
		return QString::fromUtf8("Автоматически");
	case AutoUpdateMode::Disabled:
	default:
		return QString::fromUtf8("Выключено");
	}
}

void RelayHooks::loadSettings() {
	QSettings s("ZenDesktop", "ZenDesktop");
	_ghostModeEnabled = s.value("ghost_mode", false).toBool();
	_antiRecallEnabled = s.value("anti_recall", true).toBool();
	_hideTyping = s.value("hide_typing", _ghostModeEnabled).toBool();
	_hideReadHistory = s.value("hide_read_history", _ghostModeEnabled).toBool();
	_storiesStealth = s.value("stories_stealth", _ghostModeEnabled).toBool();
	_noSponsored = s.value("no_sponsored", true).toBool();
	_hidePhoneNumber = s.value("hide_phone_number", false).toBool();
	_hideSensitiveMedia = s.value("hide_sensitive_media", false).toBool();
	_streamerMode = s.value("streamer_mode", false).toBool();
	_timeWithSeconds = s.value("time_with_seconds", false).toBool();
	_swissMotion = s.value("swiss_motion", true).toBool();
	_hideMuteIcon = s.value("hide_mute_icon", false).toBool();
	_customBubbleColor = s.value("custom_bubble_color", false).toBool();
	_greetingStickerDisabled = s.value("greeting_sticker_disabled", false).toBool();
	_showPeerId = s.value("show_peer_id", true).toBool();
	_material3Bubbles = s.value("material3_bubbles", true).toBool();
	_chatListDividers = s.value("chat_list_dividers", true).toBool();
	_smoothScroll = s.value("smooth_scroll", true).toBool();
	_material3Icons = s.value("material3_icons", true).toBool();
	_downloadBoost = s.value("download_boost", true).toBool();
	_wsProxy = s.value("ws_proxy", false).toBool();
	_quickProxyEnabled = s.value("quick_proxy", true).toBool();
	_editHistoryEnabled = s.value("edit_history", true).toBool();
	_stripExifEnabled = s.value("strip_exif", true).toBool();
	_bubbleRadiusLarge = std::clamp(s.value("bubble_radius_large", 15).toInt(), 4, 24);
	_bubbleRadiusSmall = std::clamp(s.value("bubble_radius_small", 6).toInt(), 2, 16);
	_messageLineSpacing = std::clamp(s.value("message_line_spacing", 0).toInt(), 0, 8);
	_localTranscribeEnabled = s.value("local_transcribe", true).toBool();
	_whisperApiUrl = s.value("whisper_api_url", QString()).toString();
	_whisperApiKey = s.value("whisper_api_key", QString()).toString();
	_smartCacheEnabled = s.value("smart_cache_enabled", false).toBool();
	_smartCacheMaxDays = std::clamp(s.value("smart_cache_max_days", 7).toInt(), 1, 90);
	_smartCacheCleanStickers = s.value("smart_cache_stickers", true).toBool();
	_smartCacheCleanVoiceRounds = s.value("smart_cache_voice_rounds", true).toBool();
	_smartCacheCleanVideo = s.value("smart_cache_video", true).toBool();
	_smartCacheCleanMedia = s.value("smart_cache_media", false).toBool();
	_smartCacheRamLimitMb = std::clamp(s.value("smart_cache_ram_mb", 512).toInt(), 128, 4096);
	_autoUpdateMode = static_cast<AutoUpdateMode>(s.value("auto_update_mode", 0).toInt());
}

void RelayHooks::saveSettings() {
	QSettings s("ZenDesktop", "ZenDesktop");
	s.setValue("ghost_mode", _ghostModeEnabled);
	s.setValue("anti_recall", _antiRecallEnabled);
	s.setValue("hide_typing", _hideTyping);
	s.setValue("hide_read_history", _hideReadHistory);
	s.setValue("stories_stealth", _storiesStealth);
	s.setValue("no_sponsored", _noSponsored);
	s.setValue("hide_phone_number", _hidePhoneNumber);
	s.setValue("hide_sensitive_media", _hideSensitiveMedia);
	s.setValue("streamer_mode", _streamerMode);
	s.setValue("time_with_seconds", _timeWithSeconds);
	s.setValue("swiss_motion", _swissMotion);
	s.setValue("hide_mute_icon", _hideMuteIcon);
	s.setValue("custom_bubble_color", _customBubbleColor);
	s.setValue("greeting_sticker_disabled", _greetingStickerDisabled);
	s.setValue("show_peer_id", _showPeerId);
	s.setValue("material3_bubbles", _material3Bubbles);
	s.setValue("chat_list_dividers", _chatListDividers);
	s.setValue("smooth_scroll", _smoothScroll);
	s.setValue("material3_icons", _material3Icons);
	s.setValue("download_boost", _downloadBoost);
	s.setValue("ws_proxy", _wsProxy);
	s.setValue("quick_proxy", _quickProxyEnabled);
	s.setValue("edit_history", _editHistoryEnabled);
	s.setValue("strip_exif", _stripExifEnabled);
	s.setValue("bubble_radius_large", _bubbleRadiusLarge);
	s.setValue("bubble_radius_small", _bubbleRadiusSmall);
	s.setValue("message_line_spacing", _messageLineSpacing);
	s.setValue("local_transcribe", _localTranscribeEnabled);
	s.setValue("whisper_api_url", _whisperApiUrl);
	s.setValue("whisper_api_key", _whisperApiKey);
	s.setValue("smart_cache_enabled", _smartCacheEnabled);
	s.setValue("smart_cache_max_days", _smartCacheMaxDays);
	s.setValue("smart_cache_stickers", _smartCacheCleanStickers);
	s.setValue("smart_cache_voice_rounds", _smartCacheCleanVoiceRounds);
	s.setValue("smart_cache_video", _smartCacheCleanVideo);
	s.setValue("smart_cache_media", _smartCacheCleanMedia);
	s.setValue("smart_cache_ram_mb", _smartCacheRamLimitMb);
	s.setValue("auto_update_mode", static_cast<int>(_autoUpdateMode));
}

bool RelayHooks::exportSettings(const QString &filePath) const {
	QJsonObject root;
	root["ghost_mode"] = _ghostModeEnabled;
	root["anti_recall"] = _antiRecallEnabled;
	root["hide_typing"] = _hideTyping;
	root["hide_read_history"] = _hideReadHistory;
	root["stories_stealth"] = _storiesStealth;
	root["no_sponsored"] = _noSponsored;
	root["hide_phone_number"] = _hidePhoneNumber;
	root["time_with_seconds"] = _timeWithSeconds;
	root["greeting_sticker_disabled"] = _greetingStickerDisabled;
	root["show_peer_id"] = _showPeerId;
	root["material3_bubbles"] = _material3Bubbles;
	root["chat_list_dividers"] = _chatListDividers;
	root["smooth_scroll"] = _smoothScroll;
	root["material3_icons"] = _material3Icons;
	root["download_boost"] = _downloadBoost;
	root["ws_proxy"] = _wsProxy;
	root["auto_update_mode"] = static_cast<int>(_autoUpdateMode);

	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	return true;
}

bool RelayHooks::importSettings(const QString &filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		return false;
	}
	const auto obj = doc.object();
	if (obj.contains("ghost_mode")) _ghostModeEnabled = obj["ghost_mode"].toBool();
	if (obj.contains("anti_recall")) _antiRecallEnabled = obj["anti_recall"].toBool();
	if (obj.contains("hide_typing")) _hideTyping = obj["hide_typing"].toBool();
	if (obj.contains("hide_read_history")) _hideReadHistory = obj["hide_read_history"].toBool();
	if (obj.contains("stories_stealth")) _storiesStealth = obj["stories_stealth"].toBool();
	if (obj.contains("no_sponsored")) _noSponsored = obj["no_sponsored"].toBool();
	if (obj.contains("hide_phone_number")) _hidePhoneNumber = obj["hide_phone_number"].toBool();
	if (obj.contains("time_with_seconds")) _timeWithSeconds = obj["time_with_seconds"].toBool();
	if (obj.contains("greeting_sticker_disabled")) _greetingStickerDisabled = obj["greeting_sticker_disabled"].toBool();
	if (obj.contains("show_peer_id")) _showPeerId = obj["show_peer_id"].toBool();
	if (obj.contains("material3_bubbles")) _material3Bubbles = obj["material3_bubbles"].toBool();
	if (obj.contains("chat_list_dividers")) _chatListDividers = obj["chat_list_dividers"].toBool();
	if (obj.contains("smooth_scroll")) _smoothScroll = obj["smooth_scroll"].toBool();
	if (obj.contains("material3_icons")) _material3Icons = obj["material3_icons"].toBool();
	if (obj.contains("download_boost")) _downloadBoost = obj["download_boost"].toBool();
	if (obj.contains("ws_proxy")) _wsProxy = obj["ws_proxy"].toBool();
	if (obj.contains("auto_update_mode")) _autoUpdateMode = static_cast<AutoUpdateMode>(obj["auto_update_mode"].toInt());

	saveSettings();
	return true;
}

void RelayHooks::resetSettings() {
	_ghostModeEnabled = false;
	_antiRecallEnabled = true;
	_hideTyping = false;
	_hideReadHistory = false;
	_storiesStealth = false;
	_noSponsored = true;
	_hidePhoneNumber = false;
	_timeWithSeconds = false;
	_greetingStickerDisabled = false;
	_showPeerId = true;
	_material3Bubbles = true;
	_chatListDividers = true;
	_smoothScroll = true;
	_material3Icons = true;
	_downloadBoost = true;
	_wsProxy = false;
	_autoUpdateMode = AutoUpdateMode::DownloadOnly;
	saveSettings();
	LOG(("RelayHooks: Settings reset to defaults."));
}

bool RelayHooks::handleMessagesDeleted(uint64_t peerId, const std::vector<int32_t> &messageIds) {
	for (const auto id : messageIds) {
		markMessageDeleted(peerId, id);
	}

	// Execute custom hooks first
	for (const auto &[id, hook] : _deleteHooks) {
		if (hook && !hook(peerId, messageIds)) {
			_stats.deletedMessagesIntercepted += messageIds.size();
			return false; // Intercepted by custom hook
		}
	}

	// Built-in Anti-Recall
	if (_antiRecallEnabled) {
		_stats.deletedMessagesIntercepted += messageIds.size();
		LOG(("RelayHooks: Anti-recall intercepted %1 deleted messages for peer %2")
			.arg(messageIds.size())
			.arg(peerId));
		return false; // Intercept: do not delete messages locally
	}

	return true; // Allow deletion
}

bool RelayHooks::handleNonChannelMessagesDeleted(const std::vector<int32_t> &messageIds) {
	return handleMessagesDeleted(0, messageIds);
}

bool RelayHooks::handleBeforeSendMessage(QString &text) {
	for (const auto &[id, hook] : _sendHooks) {
		if (hook && !hook(text)) {
			LOG(("RelayHooks: Send message cancelled by hook '%1'").arg(QString::fromStdString(id)));
			return false;
		}
	}

	_stats.messagesModified++;
	return true;
}

bool RelayHooks::handleSendTyping(uint64_t peerId) {
	if (isTypingHidden()) {
		_stats.typingBlocked++;
		LOG(("RelayHooks: Typing status suppressed for peer %1").arg(peerId));
		return false;
	}

	for (const auto &[id, hook] : _typingHooks) {
		if (hook && !hook(peerId)) {
			_stats.typingBlocked++;
			return false;
		}
	}

	return true;
}

bool RelayHooks::handleReadHistory(uint64_t peerId, int32_t maxId) {
	if (isReadHistoryHidden()) {
		_stats.readHistoryBlocked++;
		LOG(("RelayHooks: Read receipt suppressed for peer %1 up to %2").arg(peerId).arg(maxId));
		return false;
	}

	for (const auto &[id, hook] : _readHistoryHooks) {
		if (hook && !hook(peerId, maxId)) {
			_stats.readHistoryBlocked++;
			return false;
		}
	}

	return true;
}

void RelayHooks::registerDeleteHook(const std::string &id, DeleteHookFn fn) {
	_deleteHooks[id] = std::move(fn);
}

void RelayHooks::registerSendHook(const std::string &id, SendHookFn fn) {
	_sendHooks[id] = std::move(fn);
}

void RelayHooks::registerTypingHook(const std::string &id, TypingHookFn fn) {
	_typingHooks[id] = std::move(fn);
}

void RelayHooks::registerReadHistoryHook(const std::string &id, ReadHistoryHookFn fn) {
	_readHistoryHooks[id] = std::move(fn);
}

void RelayHooks::unregisterHook(const std::string &id) {
	_deleteHooks.erase(id);
	_sendHooks.erase(id);
	_typingHooks.erase(id);
	_readHistoryHooks.erase(id);
}

const HookStats &RelayHooks::stats() const {
	return _stats;
}

void RelayHooks::resetStats() {
	_stats = HookStats();
	LOG(("RelayHooks: Stats reset."));
}

} // namespace Relay
