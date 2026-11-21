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

bool RelayHooks::isTimeWithSecondsEnabled() const {
	return _timeWithSeconds;
}

void RelayHooks::setTimeWithSecondsEnabled(bool enabled) {
	_timeWithSeconds = enabled;
	saveSettings();
	LOG(("RelayHooks: Time with seconds set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isGreetingStickerDisabled() const {
	return _greetingStickerDisabled;
}

void RelayHooks::setGreetingStickerDisabled(bool disabled) {
	_greetingStickerDisabled = disabled;
	saveSettings();
	LOG(("RelayHooks: Greeting sticker disabled set to %1").arg(disabled ? "true" : "false"));
}

bool RelayHooks::isDownloadBoostEnabled() const {
	return _downloadBoost;
}

void RelayHooks::setDownloadBoostEnabled(bool enabled) {
	_downloadBoost = enabled;
	saveSettings();
	LOG(("RelayHooks: Download boost set to %1").arg(enabled ? "true" : "false"));
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
	_timeWithSeconds = s.value("time_with_seconds", false).toBool();
	_greetingStickerDisabled = s.value("greeting_sticker_disabled", false).toBool();
	_downloadBoost = s.value("download_boost", true).toBool();
	_autoUpdateMode = static_cast<AutoUpdateMode>(s.value("auto_update_mode", 0).toInt());
}

void RelayHooks::saveSettings() {
	QSettings s("ZenDesktop", "ZenDesktop");
	s.setValue("ghost_mode", _ghostModeEnabled);
	s.setValue("anti_recall", _antiRecallEnabled);
	s.setValue("hide_typing", _hideTyping);
	s.setValue("hide_read_history", _hideReadHistory);
	s.setValue("time_with_seconds", _timeWithSeconds);
	s.setValue("greeting_sticker_disabled", _greetingStickerDisabled);
	s.setValue("download_boost", _downloadBoost);
	s.setValue("auto_update_mode", static_cast<int>(_autoUpdateMode));
}

bool RelayHooks::exportSettings(const QString &filePath) const {
	QJsonObject root;
	root["ghost_mode"] = _ghostModeEnabled;
	root["anti_recall"] = _antiRecallEnabled;
	root["hide_typing"] = _hideTyping;
	root["hide_read_history"] = _hideReadHistory;
	root["time_with_seconds"] = _timeWithSeconds;
	root["greeting_sticker_disabled"] = _greetingStickerDisabled;
	root["download_boost"] = _downloadBoost;
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
	if (obj.contains("time_with_seconds")) _timeWithSeconds = obj["time_with_seconds"].toBool();
	if (obj.contains("greeting_sticker_disabled")) _greetingStickerDisabled = obj["greeting_sticker_disabled"].toBool();
	if (obj.contains("download_boost")) _downloadBoost = obj["download_boost"].toBool();
	if (obj.contains("auto_update_mode")) _autoUpdateMode = static_cast<AutoUpdateMode>(obj["auto_update_mode"].toInt());

	saveSettings();
	return true;
}

void RelayHooks::resetSettings() {
	_ghostModeEnabled = false;
	_antiRecallEnabled = true;
	_hideTyping = false;
	_hideReadHistory = false;
	_timeWithSeconds = false;
	_greetingStickerDisabled = false;
	_downloadBoost = true;
	_autoUpdateMode = AutoUpdateMode::DownloadOnly;
	saveSettings();
	LOG(("RelayHooks: Settings reset to defaults."));
}

bool RelayHooks::handleMessagesDeleted(uint64_t peerId, const std::vector<int32_t> &messageIds) {
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
