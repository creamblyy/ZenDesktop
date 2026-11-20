/*
This file is part of Telegram Desktop fork with RelayHooks.
*/
#include "relay_hooks/relay_hooks.h"
#include "logs.h"

namespace Relay {

RelayHooks &RelayHooks::Instance() {
	static RelayHooks instance;
	return instance;
}

RelayHooks::RelayHooks() {
	LOG(("RelayHooks initialized for Windows build."));
}

bool RelayHooks::isGhostModeEnabled() const {
	return _ghostModeEnabled;
}

void RelayHooks::setGhostModeEnabled(bool enabled) {
	_ghostModeEnabled = enabled;
	_hideTyping = enabled;
	_hideReadHistory = enabled;
	LOG(("RelayHooks: Ghost mode set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isAntiRecallEnabled() const {
	return _antiRecallEnabled;
}

void RelayHooks::setAntiRecallEnabled(bool enabled) {
	_antiRecallEnabled = enabled;
	LOG(("RelayHooks: Anti-recall set to %1").arg(enabled ? "true" : "false"));
}

bool RelayHooks::isTypingHidden() const {
	return _hideTyping || _ghostModeEnabled;
}

void RelayHooks::setTypingHidden(bool hidden) {
	_hideTyping = hidden;
}

bool RelayHooks::isReadHistoryHidden() const {
	return _hideReadHistory || _ghostModeEnabled;
}

void RelayHooks::setReadHistoryHidden(bool hidden) {
	_hideReadHistory = hidden;
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
	_stats = HookStats{};
}

} // namespace Relay
