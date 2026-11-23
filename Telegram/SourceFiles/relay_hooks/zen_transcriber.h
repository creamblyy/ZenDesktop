/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include <QtCore/QString>
#include <functional>

namespace Relay {

class ZenTranscriber final {
public:
	using Callback = std::function<void(bool ok, const QString &textOrError)>;

	static void TranscribeFile(const QString &filePath, Callback done);
	[[nodiscard]] static QString WhisperDir();
	[[nodiscard]] static QString WhisperExePath();
	[[nodiscard]] static QString WhisperModelPath();
	[[nodiscard]] static bool IsLocalWhisperReady();
};

} // namespace Relay
