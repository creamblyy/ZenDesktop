/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type ZenId();
[[nodiscard]] Type ZenPrivacyId();
[[nodiscard]] Type ZenAppearanceId();
[[nodiscard]] Type ZenMessagesId();
[[nodiscard]] Type ZenTranslateId();
[[nodiscard]] Type ZenNetworkId();
[[nodiscard]] Type ZenPluginsId();
[[nodiscard]] Type ZenUpdatesId();
[[nodiscard]] Type ZenSupportId();
[[nodiscard]] Type ZenOtherId();
[[nodiscard]] Type ZenFunId();

} // namespace Settings
