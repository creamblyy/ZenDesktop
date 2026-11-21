/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"_cs;
constexpr auto AppNameOld = "Telegram Win (Unofficial)"_cs;
constexpr auto AppName = "Telegram Desktop"_cs;
constexpr auto AppFile = "Telegram"_cs;
constexpr auto AppVersion = 7004005;
constexpr auto AppVersionStr = "7.4.5";
constexpr auto AppBetaVersion = true;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;

// ZenDesktop Unique Version System
constexpr auto ZenVersionMajor = 1;
constexpr auto ZenVersionMinor = 0;
constexpr auto ZenVersionPatch = 0;
constexpr auto ZenBuildNumber = 2601;
constexpr auto ZenVersionStr = "1.0.0"_cs;
constexpr auto ZenBuildStr = "build 2601"_cs;
constexpr auto ZenCoreVersionStr = "RelayCore 2.4"_cs;
constexpr auto ZenFullVersionStr = "ZenDesktop v1.0.0 (build 2601) • RelayCore 2.4"_cs;

