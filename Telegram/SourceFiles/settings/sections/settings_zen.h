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
[[nodiscard]] Type ZenNetworkId();
[[nodiscard]] Type ZenUpdatesId();

} // namespace Settings
