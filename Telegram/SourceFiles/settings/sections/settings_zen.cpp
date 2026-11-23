/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "settings/sections/settings_zen.h"

#include "settings/settings_common_session.h"
#include "settings/settings_common.h"
#include "relay_hooks/relay_hooks.h"
#include "relay_hooks/zen_updater.h"
#include "relay_hooks/zen_badges.h"
#include "relay_hooks/zen_ws_proxy.h"
#include "base/call_delayed.h"
#include "base/weak_ptr.h"
#include "core/version.h"
#include "core/file_utilities.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/effects/animations.h"
#include "ui/search_field_controller.h"
#include "window/main_window.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QMouseEvent>
#include <QtSvg/QSvgRenderer>
#include <QGraphicsOpacityEffect>
#include <algorithm>
#include <cmath>

namespace Settings {
namespace {

// Helper for Material 3 list item with switch toggle
not_null<Ui::SettingsButton*> AddToggle(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&label,
		rpl::producer<bool> &&checked,
		const style::icon *icon = nullptr) {
	const auto btn = AddButtonWithIcon(
		content,
		std::move(label),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		icon ? IconDescriptor{ .icon = icon, .type = IconType::Simple } : IconDescriptor{});
	btn->toggleOn(std::move(checked));
	return btn;
}

// Inset divider (M3 style: 1px line inset aligned with text)
void AddInsetDivider(not_null<Ui::VerticalLayout*> content, int leftMargin = 60) {
	const auto divider = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, 1),
		QMargins(leftMargin, 0, 20, 0));
	divider->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(divider);
		auto c = st::windowFg->c;
		c.setAlphaF(0.08); // M3 outline-variant tone
		p.fillRect(divider->rect(), c);
	}, divider->lifetime());
}

constexpr auto kZenRevealShiftPx = 12;
constexpr auto kZenRevealStaggerMs = 30;
constexpr auto kZenRevealOpacityMs = 220;
constexpr auto kZenRevealMoveMs = 320;
constexpr auto kZenExitDurationMs = 110;

[[nodiscard]] float64 ZenCubicBezierEase(float64 t) {
	if (t <= 0.0) {
		return 0.0;
	}
	if (t >= 1.0) {
		return 1.0;
	}
	auto lo = 0.0;
	auto hi = 1.0;
	for (int i = 0; i < 20; ++i) {
		const auto mid = 0.5 * (lo + hi);
		const auto inv = 1.0 - mid;
		const auto x = 0.6 * mid * inv * inv + mid * mid * mid;
		if (x < t) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	const auto u = 0.5 * (lo + hi);
	return u * u * (3.0 - 2.0 * u);
}

struct ZenTransitionItem {
	QPointer<QWidget> widget;
	QPointer<QGraphicsOpacityEffect> effect;
	int baseY = 0;
	int delayMs = 0;
	bool hasShift = false;
};

struct ZenTransitionState {
	std::vector<ZenTransitionItem> items;
	Ui::Animations::Simple revealAnimation;
	Ui::Animations::Simple exitAnimation;
	int totalMs = 0;
	int lastWidth = -1;
	bool navigating = false;
};

void FinishZenReveal(ZenTransitionState *state) {
	if (!state) {
		return;
	}
	state->revealAnimation.stop();
	for (const auto &item : state->items) {
		if (item.widget) {
			if (item.hasShift) {
				item.widget->move(item.widget->x(), item.baseY);
			}
			item.widget->setGraphicsEffect(nullptr);
		}
	}
	state->items.clear();
}

void AttachZenSectionTransition(
		not_null<AbstractSection*> section,
		not_null<Ui::VerticalLayout*> content) {
	const auto state = content->lifetime().make_state<ZenTransitionState>();

	if (Relay::RelayHooks::Instance().isSwissMotionEnabled()) {
		for (auto *child : content->children()) {
			if (auto *w = qobject_cast<QWidget*>(child)) {
				if (!w->isHidden()) {
					auto *effect = new QGraphicsOpacityEffect(w);
					effect->setOpacity(0.0);
					w->setGraphicsEffect(effect);
					state->items.push_back(ZenTransitionItem{
						.widget = w,
						.effect = effect,
						.baseY = w->y(),
						.delayMs = 0,
						.hasShift = false,
					});
				}
			}
		}
	}

	content->widthValue() | rpl::on_next([=](int width) {
		if (state->lastWidth != -1
			&& state->lastWidth != width
			&& state->revealAnimation.animating()) {
			FinishZenReveal(state);
		}
		state->lastWidth = width;
	}, content->lifetime());

	section->showFinishes() | rpl::on_next([=] {
		state->exitAnimation.stop();
		state->navigating = false;
		content->setProperty("zen_navigating", false);
		content->setGraphicsEffect(nullptr);

		if (!Relay::RelayHooks::Instance().isSwissMotionEnabled()) {
			FinishZenReveal(state);
			return;
		}

		state->revealAnimation.stop();
		std::vector<QWidget*> targets;
		for (auto *child : content->children()) {
			if (auto *w = qobject_cast<QWidget*>(child)) {
				if (!w->isHidden() && w->height() > 0) {
					targets.push_back(w);
				}
			}
		}
		std::sort(targets.begin(), targets.end(), [](const QWidget *a, const QWidget *b) {
			return a->y() < b->y();
		});

		for (const auto &prev : state->items) {
			if (prev.widget
				&& std::find(targets.begin(), targets.end(), prev.widget.data()) == targets.end()) {
				if (prev.hasShift) {
					prev.widget->move(prev.widget->x(), prev.baseY);
				}
				prev.widget->setGraphicsEffect(nullptr);
			}
		}
		state->items.clear();

		if (targets.empty()) {
			return;
		}

		state->items.reserve(targets.size());
		for (size_t i = 0; i < targets.size(); ++i) {
			auto *w = targets[i];
			auto *effect = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
			if (!effect) {
				effect = new QGraphicsOpacityEffect(w);
				w->setGraphicsEffect(effect);
			}
			effect->setOpacity(0.0);

			const auto baseY = w->y();
			w->move(w->x(), baseY + kZenRevealShiftPx);
			state->items.push_back(ZenTransitionItem{
				.widget = w,
				.effect = effect,
				.baseY = baseY,
				.delayMs = static_cast<int>(i) * kZenRevealStaggerMs,
				.hasShift = true,
			});
		}

		state->totalMs = static_cast<int>(state->items.size() - 1) * kZenRevealStaggerMs
			+ kZenRevealMoveMs;

		const auto weakContent = QPointer<QWidget>(content.get());
		state->revealAnimation.start(
			[=] {
				if (!weakContent) {
					return;
				}
				const auto progress = state->revealAnimation.value(1.);
				const auto elapsedMs = progress * state->totalMs;
				for (const auto &item : state->items) {
					if (!item.widget) {
						continue;
					}
					const auto localMs = elapsedMs - item.delayMs;
					const auto op = ZenCubicBezierEase(
						std::clamp(localMs / float64(kZenRevealOpacityMs), 0.0, 1.0));
					const auto mv = ZenCubicBezierEase(
						std::clamp(localMs / float64(kZenRevealMoveMs), 0.0, 1.0));
					if (item.effect) {
						item.effect->setOpacity(op);
					}
					if (item.hasShift) {
						const auto yOffset = static_cast<int>(std::round((1.0 - mv) * kZenRevealShiftPx));
						item.widget->move(item.widget->x(), item.baseY + yOffset);
					}
				}
				if (!state->revealAnimation.animating()) {
					FinishZenReveal(state);
				}
			},
			0.,
			1.,
			state->totalMs,
			anim::linear);
	}, section->lifetime());
}

void NavigateZenSectionWithExit(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> ctrl,
		Type target) {
	if (!Relay::RelayHooks::Instance().isSwissMotionEnabled()) {
		ctrl->showSettings(target);
		return;
	}
	if (content->property("zen_navigating").toBool()) {
		return;
	}
	content->setProperty("zen_navigating", true);

	auto *effect = new QGraphicsOpacityEffect(content);
	effect->setOpacity(1.0);
	content->setGraphicsEffect(effect);

	const auto anim = content->lifetime().make_state<Ui::Animations::Simple>();
	const auto guard = QPointer<QWidget>(content.get());
	const auto weakCtrl = base::make_weak(ctrl);
	anim->start(
		[=] {
			if (!guard) {
				return;
			}
			const auto v = anim->value(0.0);
			if (auto *eff = qobject_cast<QGraphicsOpacityEffect*>(guard->graphicsEffect())) {
				eff->setOpacity(v);
			}
			if (!anim->animating()) {
				if (const auto strong = weakCtrl.get()) {
					strong->showSettings(target);
				}
				base::call_delayed(250, guard.data(), [=] {
					if (guard) {
						guard->setGraphicsEffect(nullptr);
						guard->setProperty("zen_navigating", false);
					}
				});
			}
		},
		1.0,
		0.0,
		kZenExitDurationMs,
		anim::easeOutCubic);
}

// ==========================================
// 1. Privacy & Stealth Section (Приватность)
// ==========================================
class ZenPrivacy final : public Section<ZenPrivacy> {
public:
	ZenPrivacy(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenPrivacy::ZenPrivacy(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenPrivacy::title() {
	return rpl::single(QString::fromUtf8("Приватность"));
}

void ZenPrivacy::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();
	auto &hooks = Relay::RelayHooks::Instance();

	const auto ghostStream = lifetime.make_state<rpl::event_stream<bool>>();
	const auto typingStream = lifetime.make_state<rpl::event_stream<bool>>();
	const auto readStream = lifetime.make_state<rpl::event_stream<bool>>();
	const auto storiesStream = lifetime.make_state<rpl::event_stream<bool>>();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Стелс-режим (Ghost Mode)")));

	const auto ghostToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Ghost Mode (Невидимка)")),
		ghostStream->events_starting_with(hooks.isGhostModeEnabled()),
		&st::menuIconStealth);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Скрывает факт прочтения входящих сообщений и предотвращает отправку статуса набора текста собеседникам.")));

	const auto typingToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скрывать статус набора текста")),
		typingStream->events_starting_with(hooks.isTypingHidden()),
		&st::menuIconEdit);

	AddInsetDivider(content, 60);

	const auto readToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скрывать отчет о прочтении")),
		readStream->events_starting_with(hooks.isReadHistoryHidden()),
		&st::menuIconShowInChat);

	AddInsetDivider(content, 60);

	const auto storiesToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скрывать просмотр историй (Stories)")),
		storiesStream->events_starting_with(hooks.isStoriesStealthEnabled()),
		&st::menuIconStealth);

	ghostToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setGhostModeEnabled(checked);
		typingStream->fire(Relay::RelayHooks::Instance().isTypingHidden());
		readStream->fire(Relay::RelayHooks::Instance().isReadHistoryHidden());
		storiesStream->fire(Relay::RelayHooks::Instance().isStoriesStealthEnabled());
	}, lifetime);

	typingToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setTypingHidden(checked);
	}, lifetime);

	readToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setReadHistoryHidden(checked);
	}, lifetime);

	storiesToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setStoriesStealthEnabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Защита сообщений и приватность")));

	const auto antiRecallToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Anti-Recall (Сохранение удаленных)")),
		rpl::single(hooks.isAntiRecallEnabled()),
		&st::menuIconPermissions);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Сохраняет удаленные собеседником сообщения локально с визуальной пометкой [удалено].")));

	const auto noSponsoredToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Отключение рекламы (No Sponsored)")),
		rpl::single(hooks.isNoSponsoredEnabled()),
		&st::menuIconCancel);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Блокирует показ нативных спонсированных рекламных постов Telegram в каналах и видео.")));

	const auto hidePhoneToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скрывать свой номер телефона")),
		rpl::single(hooks.isHidePhoneNumberEnabled()),
		&st::menuIconPermissions);

	AddInsetDivider(content, 60);

	const auto hideSensitiveToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Отключить предупреждения о деликатном контенте")),
		rpl::single(hooks.isHideSensitiveMediaEnabled()),
		&st::menuIconShowInChat);

	AddInsetDivider(content, 60);

	const auto streamerToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Режим стримера (Streamer Mode)")),
		rpl::single(hooks.isStreamerModeEnabled()),
		&st::menuIconProfile);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Скрывает номер телефона, email и юзернеймы на экране во время демонстрации экрана или трансляции.")));

	antiRecallToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setAntiRecallEnabled(checked);
	}, lifetime);

	noSponsoredToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setNoSponsoredEnabled(checked);
	}, lifetime);

	hidePhoneToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setHidePhoneNumberEnabled(checked);
	}, lifetime);

	hideSensitiveToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setHideSensitiveMediaEnabled(checked);
	}, lifetime);

	streamerToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setStreamerModeEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 2. Appearance Section (Оформление)
// ==========================================
class ZenAppearance final : public Section<ZenAppearance> {
public:
	ZenAppearance(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenAppearance::ZenAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenAppearance::title() {
	return rpl::single(QString::fromUtf8("Оформление"));
}

void ZenAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();
	auto &hooks = Relay::RelayHooks::Instance();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Интерфейс и стили")));

	const auto swissToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Анимации в стиле Swiss Motion")),
		rpl::single(hooks.isSwissMotionEnabled()),
		&st::menuIconFave);

	AddInsetDivider(content, 60);

	const auto secondsToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Отображать секунды в сообщениях")),
		rpl::single(hooks.isShowSecondsEnabled()),
		&st::menuIconEdit);

	AddInsetDivider(content, 60);

	const auto hideMuteToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скрывать иконку без звука в списке чатов")),
		rpl::single(hooks.isHideMuteIconEnabled()),
		&st::menuIconArchive);

	AddInsetDivider(content, 60);

	const auto customBubbleToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Цветовые акценты Material 3 / Electric Blue")),
		rpl::single(hooks.isCustomBubbleColorEnabled()),
		&st::menuIconDraw);

	swissToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setSwissMotionEnabled(checked);
	}, lifetime);

	secondsToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setShowSecondsEnabled(checked);
	}, lifetime);

	hideMuteToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setHideMuteIconEnabled(checked);
	}, lifetime);

	customBubbleToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setCustomBubbleColorEnabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Стикеры и медиа")));

	const auto cleanStickersToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Компактный просмотр стикеров")),
		rpl::single(hooks.isCleanStickersEnabled()),
		&st::menuIconStickers);

	cleanStickersToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setCleanStickersEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 3. Messages Section (Сообщения)
// ==========================================
class ZenMessages final : public Section<ZenMessages> {
public:
	ZenMessages(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenMessages::ZenMessages(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenMessages::title() {
	return rpl::single(QString::fromUtf8("Сообщения"));
}

void ZenMessages::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Ввод и форматирование")));

	const auto formatBar = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Всегда показывать панель форматирования")),
		rpl::single(true),
		&st::menuIconEdit);

	AddInsetDivider(content, 60);

	const auto quickReactions = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Быстрые реакции по двойному клику")),
		rpl::single(true),
		&st::menuIconReactions);

	AddInsetDivider(content, 60);

	const auto directDownload = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Скачивать медиа без открытия предпросмотра")),
		rpl::single(false),
		&st::menuIconDownload);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Настройки быстрого взаимодействия с входящими и исходящими сообщениями.")));

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 4. Translate Section (Перевод)
// ==========================================
class ZenTranslate final : public Section<ZenTranslate> {
public:
	ZenTranslate(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenTranslate::ZenTranslate(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenTranslate::title() {
	return rpl::single(QString::fromUtf8("Перевод"));
}

void ZenTranslate::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Сервис перевода")));

	const auto translateFullChat = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Кнопка перевода всего чата в шапке")),
		rpl::single(true),
		&st::menuIconShowInChat);

	AddInsetDivider(content, 60);

	const auto showOriginal = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Показывать исходный текст при наведении")),
		rpl::single(true),
		&st::menuIconCopy);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Встроенный переводчик сообщений с поддержкой Google Translate, DeepL и Yandex.")));

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 5. Network Section (Сеть)
// ==========================================
class ZenNetwork final : public Section<ZenNetwork> {
public:
	ZenNetwork(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenNetwork::ZenNetwork(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenNetwork::title() {
	return rpl::single(QString::fromUtf8("Сеть"));
}

void ZenNetwork::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();
	auto &hooks = Relay::RelayHooks::Instance();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Ускорение и прокси")));

	const auto boostToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Ускорение загрузки файлов (Download Boost)")),
		rpl::single(hooks.isDownloadBoostEnabled()),
		&st::menuIconDownload);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Разбивает большие файлы на параллельные потоки для максимальной скорости скачивания медиа.")));

	const auto wsProxyToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Встроенный WebSocket прокси (tg-ws-proxy)")),
		rpl::single(hooks.isWsProxyEnabled()),
		&st::menuIconLink);

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Локальный MTProto-прокси (127.0.0.1:1443) внутри клиента. Туннелирует трафик через WebSocket на kws*.web.telegram.org без сторонних программ.")));

	boostToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setDownloadBoostEnabled(checked);
	}, lifetime);

	wsProxyToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setWsProxyEnabled(checked);
		Relay::ZenWsProxy::Instance().applyEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 6. Plugins Section (Плагины)
// ==========================================
class ZenPlugins final : public Section<ZenPlugins> {
public:
	ZenPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenPlugins::ZenPlugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenPlugins::title() {
	return rpl::single(QString::fromUtf8("Плагины"));
}

void ZenPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Движок плагинов RelayCore")));

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Плагины позволяют расширять функционал ZenDesktop через JavaScript и нативные хуки. Движок активен и готов к установке расширений.")));

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 7. Updates Section (Обновления)
// ==========================================
class ZenUpdates final : public Section<ZenUpdates> {
public:
	ZenUpdates(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenUpdates::ZenUpdates(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenUpdates::title() {
	return rpl::single(QString::fromUtf8("Обновления"));
}

void ZenUpdates::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();
	auto &updater = Relay::ZenUpdater::Instance();
	const auto ctrl = controller();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Автообновление (OTA)")));

	const auto otaToggle = AddToggle(
		content,
		rpl::single(QString::fromUtf8("Автоматически проверять обновления ZenDesktop")),
		rpl::single(updater.isAutoCheckEnabled()),
		&st::menuIconRestore);

	otaToggle->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::ZenUpdater::Instance().setAutoCheckEnabled(checked);
	}, lifetime);

	const auto checkBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Проверить обновления сейчас")),
		st::settingsButton,
		{ .icon = &st::menuIconRestore, .type = IconType::Simple });

	checkBtn->setClickedCallback([=] {
		Relay::ZenUpdater::Instance().checkForUpdates(true);
	});

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("ZenDesktop v1.0.0 (build 2601) • RelayCore 2.4\nКанал обновлений: Release x64")));

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Резервное копирование")));

	const auto exportBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Экспорт настроек")),
		st::settingsButton,
		{ .icon = &st::menuIconDownload, .type = IconType::Simple });

	exportBtn->setClickedCallback([=] {
		FileDialog::GetWritePath(
			this,
			QString::fromUtf8("Экспорт настроек ZenDesktop"),
			QString("JSON (*.json)"),
			"zendesktop_settings.json",
			[=](const QString &path) {
				if (!path.isEmpty()) {
					if (Relay::RelayHooks::Instance().exportSettings(path)) {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Настройки успешно экспортированы.")));
					} else {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Не удалось сохранить файл настроек.")));
					}
				}
			});
	});

	const auto importBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Импорт настроек")),
		st::settingsButton,
		{ .icon = &st::menuIconDownload, .type = IconType::Simple });

	importBtn->setClickedCallback([=] {
		FileDialog::GetOpenPath(
			this,
			QString::fromUtf8("Импорт настроек ZenDesktop"),
			QString("JSON (*.json);;All files (*.*)"),
			[=](FileDialog::OpenResult &&result) {
				if (!result.paths.isEmpty()) {
					const auto path = result.paths.first();
					if (Relay::RelayHooks::Instance().importSettings(path)) {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Настройки успешно импортированы.")));
					} else {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Ошибка при чтении файла настроек.")));
					}
				}
			});
	});

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 8. Support Section (Поддержка)
// ==========================================
class ZenSupport final : public Section<ZenSupport> {
public:
	ZenSupport(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenSupport::ZenSupport(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenSupport::title() {
	return rpl::single(QString::fromUtf8("Поддержка"));
}

void ZenSupport::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto ctrl = controller();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("ZenDesktop Community")));

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Спасибо за использование форка ZenDesktop. Вы можете получить бейдж сторонника или сообщить об ошибках.")));

	const auto badgeBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Менеджер бейджей (Badge Manager)")),
		st::settingsButton,
		{ .icon = &st::menuIconLike, .type = IconType::Simple });

	badgeBtn->setClickedCallback([=] {
		Relay::ZenBadges::Instance().syncFromRemote(ctrl);
	});

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 9. Other Section (Другое)
// ==========================================
class ZenOther final : public Section<ZenOther> {
public:
	ZenOther(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

ZenOther::ZenOther(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ZenOther::title() {
	return rpl::single(QString::fromUtf8("Другое"));
}

void ZenOther::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto ctrl = controller();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Статистика RelayCore")));

	const auto formatStats = [] {
		const auto &s = Relay::RelayHooks::Instance().stats();
		return QString(
			"Перехвачено удалений: %1\n"
			"Скрыто наборов текста: %2\n"
			"Заблокировано отчетов о прочтении: %3\n"
			"Обработано отправлений: %4")
			.arg(s.deletedMessagesIntercepted)
			.arg(s.typingBlocked)
			.arg(s.readHistoryBlocked)
			.arg(s.messagesModified);
	};

	const auto statsLabel = Ui::AddDividerText(
		content,
		rpl::single(formatStats()));

	const auto resetStatsBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Сбросить счетчики перехватов")),
		st::settingsButton);

	resetStatsBtn->setClickedCallback([=] {
		Relay::RelayHooks::Instance().resetStats();
		statsLabel->setText(formatStats());
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Сброс параметров")));

	const auto resetBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Сбросить все настройки ZenDesktop")),
		st::settingsButton,
		{ .icon = &st::menuIconRestore, .type = IconType::Simple });

	resetBtn->setClickedCallback([=] {
		ctrl->show(Ui::MakeConfirmBox({
			.text = QString::fromUtf8("Сбросить все настройки ZenDesktop к значениям по умолчанию?"),
			.confirmed = [=] {
				Relay::RelayHooks::Instance().resetSettings();
				ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Настройки сброшены к значениям по умолчанию.")));
			},
		}));
	});

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

// ==========================================
// 10. Main Zen Section (ZenDesktop Main Hub)
// ==========================================

const char *kCardSvgs[] = {
	// 0: Palette (Оформление)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M480-60.78q-85.86 0-162.3-33.2-76.44-33.19-133.48-90.24-57.05-57.04-90.24-133.49-33.2-76.45-33.2-162.32 0-86.93 33.63-163.32 33.63-76.39 91.68-133.22 58.04-56.82 135.39-89.74 77.35-32.91 165.16-32.91 83.62 0 158.3 28.91 74.67 28.92 131.28 79.96 56.61 51.05 89.8 120.77 33.2 69.72 33.2 150.45 0 118.39-70.85 188.37t-178.2 69.98h-60.99q-6.74 0-9.96 3.87-3.22 3.87-3.22 9.3 0 10.87 11.61 26.59t11.61 41.89q0 54.52-33.72 86.43-33.72 31.92-85.5 31.92ZM309.78-457q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Zm116.61-154.91q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Zm193.22 0q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17ZM734.52-457q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Z"/></svg>)",
	// 1: Messages (Сообщения)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="m220.78-220.78-69.39 69.39q-25.22 25.22-57.91 11.61-32.7-13.61-32.7-49.09v-604.35q0-44.3 30.85-75.15 30.85-30.85 75.15-30.85h626.44q44.3 0 75.15 30.85 30.85 30.85 30.85 75.15v466.44q0 44.3-30.85 75.15-30.85 30.85-75.15 30.85H220.78ZM280-400h240q17 0 28.5-11.5T560-440q0-17-11.5-28.5T520-480H280q-17 0-28.5 11.5T240-440q0 17 11.5 28.5T280-400Zm0-120h400q17 0 28.5-11.5T720-560q0-17-11.5-28.5T680-600H280q-17 0-28.5 11.5T240-560q0 17 11.5 28.5T280-520Zm0-120h400q17 0 28.5-11.5T720-680q0-17-11.5-28.5T680-720H280q-17 0-28.5 11.5T240-680q0 17 11.5 28.5T280-640Z"/></svg>)",
	// 2: Translate (Перевод)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="m600.74-159.04-22.13 63.65q-5.13 15.52-18.81 25.06-13.67 9.55-30.19 9.55-26.78 0-42.96-21.87-16.17-21.87-6.48-48.09l144.09-398.04q6.7-17.22 21.5-27.61t33.02-10.39h41.87q18.22 0 33.02 10.39 14.81 10.39 21.51 27.61l144.08 397.91q9.7 25.78-6.26 47.94-15.96 22.15-43.3 22.15-16.96 0-30.7-9.55-13.74-9.54-19.87-26.06l-21.56-62.65H600.74Zm-252.31-213.7L188-212.74q-15.52 15.52-36.83 15.46-21.3-.07-37.26-16.02-15.52-15.53-15.52-37.33 0-21.8 15.52-37.33l161-161q-40.65-41.78-70.56-87.06-29.92-45.28-51.66-97.2h116.79q15.48 29.96 34.91 56.13 19.44 26.18 45.17 53.48 31.87-34.13 64.83-88.83 32.96-54.69 49.26-100.78H70.39q-22.09 0-37.54-15.45-15.46-15.46-15.46-37.55 0-22.08 15.46-37.54 15.45-15.46 37.54-15.46h227v-27q0-22.08 15.46-37.54 15.45-15.46 37.54-15.46t37.54 15.46q15.46 15.46 15.46 37.54v27h227q22.09 0 37.54 15.46 15.46 15.46 15.46 37.54 0 22.09-15.46 37.55-15.45 15.45-37.54 15.45H575.3q-22.13 69.18-64.97 144.61-42.85 75.44-87.24 121.09l68.87 70.3-39.61 110.83-103.92-106.35Zm285.22 117.7h130.44l-65.22-189.31-65.22 189.31Z"/></svg>)",
	// 3: Lock (Приватность)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M246.78-62.48q-43.72 0-74.86-31.14-31.14-31.13-31.14-74.86v-386.43q0-43.73 31.14-74.87 31.14-31.13 74.86-31.13h24.74v-63.61q0-87.52 60.76-148.85T480-934.7q86.96 0 147.72 61.33 60.76 61.33 60.76 148.85v63.61h24.74q43.72 0 74.86 31.13 31.14 31.14 31.14 74.87v386.43q0 43.73-31.14 74.86-31.14 31.14-74.86 31.14H246.78ZM536.5-305.2q23.5-23.5 23.5-56.5t-23.5-56.5Q513-441.7 480-441.7t-56.5 23.5Q400-394.7 400-361.7t23.5 56.5q23.5 23.5 56.5 23.5t56.5-23.5ZM377.52-660.91h204.96v-63.61q0-43.41-29.63-73.79Q523.22-828.7 480-828.7t-72.85 30.39q-29.63 30.38-29.63 73.79v63.61Z"/></svg>)",
	// 4: Wifi (Сеть)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M409-149q-29-29-29-71t29-71q29-29 71-29t71 29q29 29 29 71t-29 71q-29 29-71 29t-71-29Zm209.83-388.13q65.52 22.87 119.95 62.61 22.26 16.13 23.05 44.02.78 27.89-19.48 48.15-19.83 19.83-48.22 21.18-28.39 1.34-51.78-13.79-35.18-22.6-75.81-35.06T480-422.48q-45.35 0-85.7 12.18-40.34 12.17-75.52 34.78-23.95 15.13-52.63 13.85-28.67-1.29-48.5-21.11-19.82-20.26-19.26-47.87.57-27.61 22.83-43.74 54.43-39.74 119.95-62.68Q406.7-560 480-560q73.3 0 138.83 22.87Zm92.43-223q108.52 39.87 195.96 112.04 22.82 18.7 24.39 46.81 1.56 28.11-19.26 48.93-19.83 19.83-48.22 20.61-28.39.78-51.78-16.91-69.74-54.48-153.59-84.15-83.85-29.68-178.76-29.68T301.24-632.8q-83.85 29.67-153.59 84.15-23.39 17.69-51.5 16.91-28.11-.78-47.93-20.61-20.83-20.82-19.55-48.93 1.29-28.11 24.11-46.81 87.44-72.17 195.96-112.04Q357.26-800 480-800t231.26 39.87Z"/></svg>)",
	// 5: Puzzle (Плагины)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M193.78-100.78q-39.08 0-66.32-27.24-27.24-27.24-27.24-66.33v-121.61q0-15.52 10.54-26.84 10.54-11.33 26.07-15.03 36.73-9.69 61.47-36.78 24.74-27.09 24.74-64.26 0-37.3-24.45-63.89-24.46-26.59-61.2-36.72-14.96-4.13-25.78-15.74-10.83-11.61-10.83-26.56v-124.44q0-38.65 27.18-65.82 27.17-27.18 66.39-27.18h114.08q10.96-51 53.07-87.19 42.11-36.2 97.37-36.2 58.22 0 100.89 36.2 42.68 36.19 50.11 87.19h115.78q38.65 0 66.11 27.18 27.46 27.17 27.46 65.82v131.74q36 20.26 58.85 56.52 22.84 36.26 22.84 79.09 0 44.96-22.84 82.07-22.85 37.1-58.85 53.54v128.91q0 39.09-27.46 66.33-27.46 27.24-66.11 27.24H193.78Z"/></svg>)",
	// 6: Update/Clock (Обновления)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M480.57-100.78q-78.4 0-147.57-29.91-69.17-29.92-120.5-81.24-51.33-51.33-81.24-120.5-29.91-69.18-29.91-147.57 0-78.39 29.91-147.57 29.91-69.17 81.24-120.5 51.33-51.32 120.5-81.24 69.17-29.91 147.57-29.91 83.13 0 157.19 33.87 74.07 33.87 127.89 96.74v-41.56q0-18.7 12.92-31.33 12.91-12.63 31.6-12.63 18.7 0 31.33 12.63 12.63 12.63 12.63 31.33V-613q0 22.09-15.46 37.54Q823.22-560 801.13-560H643.96q-18.7 0-31.89-13.2-13.2-13.19-13.2-31.89 0-18.69 12.91-31.61 12.92-12.91 31.61-12.91h49.65q-39.3-49.22-94.78-76.41-55.48-27.2-117.69-27.2-114.18 0-193.7 79.52-79.52 79.53-79.52 193.7 0 114.17 79.52 193.7 79.52 79.52 193.7 79.52 89.91 0 160.67-52.2 70.76-52.19 97.59-135.41 6.69-19.96 24.5-30.5 17.8-10.54 38.89-7.41 22.08 3.13 35.48 19.3 13.39 16.17 7.69 35.7-32.95 121.26-133.91 198.89-100.96 77.63-230.91 77.63ZM524.52-496l94.91 94.35q12.7 12.69 12.7 31.39 0 18.69-12.7 31.39-12.69 12.7-31.39 12.7-18.69 0-31.39-12.7L451.43-444.09q-7.69-7.69-11.54-17.17-3.85-9.48-3.85-20.31v-154.47q0-18.7 12.92-31.33Q461.87-680 480.57-680q18.69 0 31.32 12.63 12.63 12.63 12.63 31.33V-496Z"/></svg>)",
	// 7: Heart (Поддержка)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M441.61-137.02q-18.74-6.98-33.7-21.37l-70.13-64.13Q232.91-317.83 146.85-415.3 60.78-512.78 60.78-634q0-102.48 68.09-171.13 68.09-68.65 170.56-68.65 51.87 0 98.31 19.95 46.43 19.96 81.69 56.7 35.27-36.74 81.7-56.7 46.44-19.95 98.3-19.95 102.48 0 171.42 68.65 68.93 68.65 68.93 171.13 0 121.22-86.41 219.2-86.41 97.97-193.28 193.84l-68.57 63.13q-14.95 14.4-33.98 21.09-19.02 6.7-38.11 6.7-19.08 0-37.82-6.98Z"/></svg>)",
	// 8: Star (Другое)
	R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="m354-287 126-76 126 77-33-144 111-96-146-13-58-136-58 135-146 13 111 97-33 143ZM233-120l65-281L80-590l288-25 112-265 112 265 288 25-218 189 65 281-247-149-247 149Z"/></svg>)",
};

struct CategoryCard {
	QString title;
	QString about;
	Type (*target)();
	QColor color;
};

class Zen final : public Section<Zen> {
public:
	Zen(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

Zen::Zen(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Zen::title() {
	return rpl::single(QString::fromUtf8("Настройки ZenDesktop"));
}

void Zen::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto ctrl = controller();

	// 1. Centered Header
	const auto header = content->add(
		object_ptr<Ui::VerticalLayout>(content),
		QMargins(0, 10, 0, 10));

	const auto logoSize = 72;
	const auto logo = header->add(
		object_ptr<Ui::FixedHeightWidget>(header, logoSize),
		QMargins(0, 4, 0, 0));
	logo->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (logo->width() - logoSize) / 2;
		p.setPen(Qt::NoPen);
		// Electric Blue (#2b3cff)
		p.setBrush(QColor(43, 60, 255));
		p.drawEllipse(QRect(left, 0, logoSize, logoSize));

		const auto &appLogo = Window::LogoNoMargin();
		if (!appLogo.isNull()) {
			const auto iconSize = 42;
			p.drawImage(
				QRect(left + (logoSize - iconSize) / 2, (logoSize - iconSize) / 2, iconSize, iconSize),
				appLogo);
		}
	}, logo->lifetime());

	const auto titleLabel = header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			rpl::single(QString::fromUtf8("ZenDesktop")),
			st::defaultFlatLabel),
		QMargins(0, 10, 0, 0),
		style::al_top);
	titleLabel->setFont(st::semiboldFont);

	const auto versionLabel = header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			rpl::single(QString::fromUtf8("v1.0.0 (build 2601) • RelayCore 2.4")),
			st::defaultFlatLabel),
		QMargins(0, 2, 0, 6),
		style::al_top);
	versionLabel->setFont(st::normalFont);
	versionLabel->setTextColorOverride(st::windowSubTextFg->c);

	// 2. Search Box
	auto search = CreateSectionSearchRow(content);
	const auto searchRow = search.row;
	content->add(
		object_ptr<Ui::RpWidget>::fromRaw(searchRow.get()),
		QMargins(14, 0, 14, 10));

	// 3. 2-Column Category Grid + Bottom "Другое" Card
	const auto cards = std::vector<CategoryCard>{
		{ QString::fromUtf8("Оформление"), QString::fromUtf8("Иконка, список чатов, папки, нав..."), ZenAppearanceId, QColor(0x8E, 0x6C, 0xEF) },
		{ QString::fromUtf8("Сообщения"), QString::fromUtf8("Вид, жесты, ввод, медиа"), ZenMessagesId, QColor(0x3A, 0x9B, 0xF0) },
		{ QString::fromUtf8("Перевод"), QString::fromUtf8("Сервис, язык, весь чат"), ZenTranslateId, QColor(0x26, 0xB0, 0xA2) },
		{ QString::fromUtf8("Приватность"), QString::fromUtf8("Телефон, ID, режим стримера"), ZenPrivacyId, QColor(0x5B, 0x6B, 0xE8) },
		{ QString::fromUtf8("Сеть"), QString::fromUtf8("tg-ws-proxy, загрузки, скорость"), ZenNetworkId, QColor(0xF0, 0x8A, 0x3A) },
		{ QString::fromUtf8("Плагины"), QString::fromUtf8("Плагины, движок, проверка"), ZenPluginsId, QColor(0xE2, 0x55, 0x7B) },
		{ QString::fromUtf8("Обновления"), QString::fromUtf8("Автообновление, резервная копия"), ZenUpdatesId, QColor(0x3C, 0xB3, 0x71) },
		{ QString::fromUtf8("Поддержка"), QString::fromUtf8("Бейдж, донат, ссылки"), ZenSupportId, QColor(0xF2, 0x54, 0x5B) },
	};

	const auto count = int(cards.size());
	const auto rows = 4;
	const auto cardHeight = 72;
	const auto skip = 2;
	const auto otherHeight = 52;
	const auto totalHeight = rows * cardHeight + (rows - 1) * skip + 8 + otherHeight;

	auto result = object_ptr<Ui::FixedHeightWidget>(content, totalHeight);
	const auto grid = result.data();

	struct State {
		int over = -1;
		int overWas = -1;
		Ui::Animations::Simple overAnimation;
		int pressed = -1;
		std::vector<QImage> icons;
		QString query;
	};
	const auto state = grid->lifetime().make_state<State>();

	if (search.controller) {
		search.controller->queryChanges(
		) | rpl::on_next([=](const QString &q) {
			state->query = q.trimmed();
			grid->update();
		}, grid->lifetime());
	}

	const auto cardRect = [=](int index) {
		if (index == 8) {
			return QRect(
				0,
				rows * cardHeight + (rows - 1) * skip + 8,
				grid->width(),
				otherHeight);
		}
		const auto width = (grid->width() - skip) / 2;
		return QRect(
			(index % 2) * (width + skip),
			(index / 2) * (cardHeight + skip),
			width,
			cardHeight);
	};

	const auto cardAt = [=](QPoint point) {
		for (auto i = 0; i <= 8; ++i) {
			if (cardRect(i).contains(point)) {
				return i;
			}
		}
		return -1;
	};

	grid->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(grid);
		auto hq = PainterHighQualityEnabler(p);

		const auto mix = [](QColor a, QColor b, float64 k) {
			return QColor(
				int(a.red() + (b.red() - a.red()) * k),
				int(a.green() + (b.green() - a.green()) * k),
				int(a.blue() + (b.blue() - a.blue()) * k));
		};
		const auto md3Card = mix(st::windowBg->c, st::windowFg->c, 0.055);
		const auto md3Tile = mix(md3Card, st::windowFg->c, 0.085);
		const auto iconColor = mix(st::windowSubTextFg->c, st::windowFg->c, 0.25);
		const auto radius = 12.0;
		const auto tileSize = 38;
		const auto iconSize = 23;
		const auto padding = 12;
		const auto ratio = style::DevicePixelRatio();

		if (state->icons.size() <= 8) {
			state->icons.resize(9);
			for (auto i = 0; i <= 8; ++i) {
				auto img = QImage(
					QSize(iconSize, iconSize) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				img.fill(Qt::transparent);
				img.setDevicePixelRatio(ratio);
				{
					auto q = QPainter(&img);
					QSvgRenderer(QByteArray(kCardSvgs[i])).render(
						&q,
						QRectF(0, 0, iconSize, iconSize));
					q.setCompositionMode(QPainter::CompositionMode_SourceIn);
					q.fillRect(QRect(0, 0, iconSize, iconSize), iconColor);
				}
				state->icons[i] = std::move(img);
			}
		}

		const auto overProgress = state->overAnimation.value(1.);

		// Paint 8 grid cards
		for (auto i = 0; i < count; ++i) {
			const auto rect = cardRect(i);
			const auto rtl = (i == 0) ? 14.0 : 4.0;
			const auto rtr = (i == 1) ? 14.0 : 4.0;
			const auto rbl = (i == 6) ? 14.0 : 4.0;
			const auto rbr = (i == 7) ? 14.0 : 4.0;
			const auto x1 = double(rect.x());
			const auto y1 = double(rect.y());
			const auto x2 = double(rect.x() + rect.width());
			const auto y2 = double(rect.y() + rect.height());

			auto path = QPainterPath();
			path.moveTo(x1 + rtl, y1);
			path.lineTo(x2 - rtr, y1);
			path.arcTo(QRectF(x2 - 2.0 * rtr, y1, 2.0 * rtr, 2.0 * rtr), 90.0, -90.0);
			path.lineTo(x2, y2 - rbr);
			path.arcTo(QRectF(x2 - 2.0 * rbr, y2 - 2.0 * rbr, 2.0 * rbr, 2.0 * rbr), 0.0, -90.0);
			path.lineTo(x1 + rbl, y2);
			path.arcTo(QRectF(x1, y2 - 2.0 * rbl, 2.0 * rbl, 2.0 * rbl), 270.0, -90.0);
			path.lineTo(x1, y1 + rtl);
			path.arcTo(QRectF(x1, y1, 2.0 * rtl, 2.0 * rtl), 180.0, -90.0);
			path.closeSubpath();

			p.setPen(Qt::NoPen);
			p.fillPath(path, md3Card);

			const auto overAlpha = (i == state->over)
				? overProgress
				: (i == state->overWas)
				? (1. - overProgress)
				: 0.;
			if (overAlpha > 0.) {
				auto over = st::windowFg->c;
				over.setAlphaF(0.07 * overAlpha);
				p.fillPath(path, over);
			}

			// Squircle Tile
			const auto tileRect = QRect(
				rect.x() + padding,
				rect.y() + (rect.height() - tileSize) / 2,
				tileSize,
				tileSize);
			p.setBrush(md3Tile);
			p.drawRoundedRect(QRectF(tileRect), 11.0, 11.0);

			// SVG Icon inside tile
			if (!state->icons[i].isNull()) {
				p.drawImage(
					QRect(
						tileRect.x() + (tileSize - iconSize) / 2,
						tileRect.y() + (tileSize - iconSize) / 2,
						iconSize,
						iconSize),
					state->icons[i]);
			}

			// Text
			const auto textLeft = tileRect.x() + tileSize + 10;
			const auto textWidth = rect.x() + rect.width() - padding - textLeft;

			p.setPen(st::windowBoldFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				QRect(textLeft, rect.y() + 16, textWidth, st::semiboldFont->height),
				Qt::AlignLeft | Qt::AlignTop,
				st::semiboldFont->elided(cards[i].title, textWidth));

			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				QRect(textLeft, rect.y() + 16 + st::semiboldFont->height + 2, textWidth, st::normalFont->height * 2),
				Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
				cards[i].about);
		}

		// Paint 9th Card: "Другое"
		{
			const auto otherIdx = 8;
			const auto rect = cardRect(otherIdx);
			auto path = QPainterPath();
			path.addRoundedRect(QRectF(rect), radius, radius);

			p.setPen(Qt::NoPen);
			p.fillPath(path, md3Card);

			const auto overAlpha = (otherIdx == state->over)
				? overProgress
				: (otherIdx == state->overWas)
				? (1. - overProgress)
				: 0.;
			if (overAlpha > 0.) {
				auto over = st::windowFg->c;
				over.setAlphaF(0.07 * overAlpha);
				p.fillPath(path, over);
			}

			// Star Icon
			if (!state->icons[otherIdx].isNull()) {
				p.drawImage(
					QRect(
						rect.x() + 16,
						rect.y() + (rect.height() - iconSize) / 2,
						iconSize,
						iconSize),
					state->icons[otherIdx]);
			}

			// Label
			p.setPen(st::windowBoldFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				QRect(rect.x() + 16 + iconSize + 14, rect.y() + (rect.height() - st::semiboldFont->height) / 2, rect.width() - 60, st::semiboldFont->height),
				Qt::AlignLeft | Qt::AlignVCenter,
				QString::fromUtf8("Другое"));
		}
	}, grid->lifetime());

	grid->setMouseTracking(true);
	grid->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto over = cardAt(static_cast<QMouseEvent*>(e.get())->pos());
			if (state->over != over) {
				state->overWas = state->over;
				state->over = over;
				state->overAnimation.stop();
				state->overAnimation.start(
					[=] { grid->update(); },
					0.,
					1.,
					140,
					anim::easeOutCubic);
				grid->setCursor((over >= 0)
					? style::cur_pointer
					: style::cur_default);
				grid->update();
			}
		} else if (type == QEvent::Leave) {
			const auto over = -1;
			if (state->over != over) {
				state->overWas = state->over;
				state->over = over;
				state->overAnimation.stop();
				state->overAnimation.start(
					[=] { grid->update(); },
					0.,
					1.,
					140,
					anim::easeOutCubic);
			}
			grid->update();
		} else if (type == QEvent::MouseButtonPress) {
			state->pressed = cardAt(static_cast<QMouseEvent*>(e.get())->pos());
		} else if (type == QEvent::MouseButtonRelease) {
			const auto index = cardAt(static_cast<QMouseEvent*>(e.get())->pos());
			const auto pressed = std::exchange(state->pressed, -1);
			if (index >= 0 && index == pressed) {
				if (index < count) {
					const auto target = cards[index].target();
					NavigateZenSectionWithExit(content, ctrl, target);
				} else if (index == 8) {
					NavigateZenSectionWithExit(content, ctrl, ZenOtherId());
				}
			}
		}
	}, grid->lifetime());

	content->add(std::move(result), QMargins(14, 0, 14, 16));

	Ui::ResizeFitChild(this, content);
	AttachZenSectionTransition(this, content);
}

} // namespace

Type ZenId() {
	return Zen::Id();
}

Type ZenPrivacyId() {
	return ZenPrivacy::Id();
}

Type ZenAppearanceId() {
	return ZenAppearance::Id();
}

Type ZenMessagesId() {
	return ZenMessages::Id();
}

Type ZenTranslateId() {
	return ZenTranslate::Id();
}

Type ZenNetworkId() {
	return ZenNetwork::Id();
}

Type ZenPluginsId() {
	return ZenPlugins::Id();
}

Type ZenUpdatesId() {
	return ZenUpdates::Id();
}

Type ZenSupportId() {
	return ZenSupport::Id();
}

Type ZenOtherId() {
	return ZenOther::Id();
}

} // namespace Settings
