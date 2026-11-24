/*
This file is part of Telegram Desktop fork - ZenDesktop (Power Tools).
*/
#pragma once

#include "base/basic_types.h"
#include <QtCore/QString>
#include <memory>

class HistoryItem;
class PhotoData;
class QWidget;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class InputField;
class PopupMenu;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Relay {

// Persisted toggles for ZenDesktop Power Tools (stored in QSettings("ZenDesktop", "PowerTools")).
[[nodiscard]] bool IsInlineCalcEnabled();
void SetInlineCalcEnabled(bool enabled);

[[nodiscard]] bool IsTranslateBeforeSendEnabled();
void SetTranslateBeforeSendEnabled(bool enabled);

[[nodiscard]] bool IsZalgoFilterEnabled();
void SetZalgoFilterEnabled(bool enabled);

// Replaces excessive combining diacritical marks with U+2060 so entity offsets stay intact.
void FilterZalgoInPlace(QString &text);

// Opens a modal box with full message, peer, media, and JPEG header details.
void ShowMessageDetailsBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// Adds a "Найти похожее" submenu to photo context menus (Yandex, Google Lens, Bing, TinEye).
void AddReverseImageSearchMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<PhotoData*> photo,
	not_null<Window::SessionController*> controller);

// Attaches inline math / currency / unit conversion ghost pill with Tab accept to compose fields.
void AttachComposeInlineCalc(
	not_null<Ui::RpWidget*> host,
	not_null<Ui::InputField*> field);

// Registers and appends "Translate before send" actions into the compose send context menu.
void AttachComposeTranslateMenu(
	not_null<QWidget*> sendButton,
	not_null<Ui::InputField*> field,
	std::shared_ptr<ChatHelpers::Show> show);

void AppendSendMenuExtra(
	not_null<QWidget*> sendButton,
	not_null<Ui::PopupMenu*> menu);

} // namespace Relay
