/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "settings/sections/settings_zen.h"

#include "settings/settings_common_session.h"
#include "settings/settings_common.h"
#include "relay_hooks/relay_hooks.h"
#include "relay_hooks/zen_updater.h"
#include "relay_hooks/zen_badges.h"
#include "core/version.h"
#include "core/file_utilities.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace Settings {
namespace {

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

	const auto addCheckbox = [&](rpl::producer<QString> &&label, bool checked) {
		return content->add(
			object_ptr<Ui::Checkbox>(
				content,
				std::move(label),
				checked,
				st::settingsCheckbox),
			st::settingsCheckboxPadding);
	};

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Стелс-режим (Ghost Mode)")));

	const auto ghostToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Ghost Mode (Невидимка)")),
		hooks.isGhostModeEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Скрывает факт прочтения входящих сообщений и предотвращает отправку статуса набора текста собеседникам.")));

	const auto typingToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Скрывать статус набора текста")),
		hooks.isTypingHidden());

	const auto readToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Скрывать отчет о прочтении")),
		hooks.isReadHistoryHidden());

	const auto storiesToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Скрывать просмотр историй (Stories)")),
		hooks.isStoriesStealthEnabled());

	ghostToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setGhostModeEnabled(checked);
		typingToggle->setChecked(Relay::RelayHooks::Instance().isTypingHidden());
		readToggle->setChecked(Relay::RelayHooks::Instance().isReadHistoryHidden());
		storiesToggle->setChecked(Relay::RelayHooks::Instance().isStoriesStealthEnabled());
	}, lifetime);

	typingToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setTypingHidden(checked);
	}, lifetime);

	readToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setReadHistoryHidden(checked);
	}, lifetime);

	storiesToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setStoriesStealthEnabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Защита сообщений и приватность")));

	const auto antiRecallToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Anti-Recall (Сохранение удаленных)")),
		hooks.isAntiRecallEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Сохраняет удаленные собеседником сообщения локально с визуальной пометкой [удалено].")));

	antiRecallToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setAntiRecallEnabled(checked);
	}, lifetime);

	const auto noSponsoredToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Отключение рекламы (No Sponsored)")),
		hooks.isNoSponsoredEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Блокирует показ нативных спонсированных рекламных постов Telegram в каналах и видео.")));

	noSponsoredToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setNoSponsoredEnabled(checked);
	}, lifetime);

	const auto hidePhoneToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Скрывать свой номер телефона")),
		hooks.isHidePhoneNumberEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Заменяет отображение номера телефона на маску (+* *** ***-**-**) в настройках и профиле для стримов и скриншотов.")));

	hidePhoneToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setHidePhoneNumberEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
}

// ==========================================
// 2. Appearance Section (Внешний вид)
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
	return rpl::single(QString::fromUtf8("Внешний вид"));
}

void ZenAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();
	auto &hooks = Relay::RelayHooks::Instance();

	const auto addCheckbox = [&](rpl::producer<QString> &&label, bool checked) {
		return content->add(
			object_ptr<Ui::Checkbox>(
				content,
				std::move(label),
				checked,
				st::settingsCheckbox),
			st::settingsCheckboxPadding);
	};

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Отображение сообщений")));

	const auto secondsToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Время с секундами (HH:MM:SS)")),
		hooks.isTimeWithSecondsEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Отображает точное время отправки и редактирования сообщений с секундами в уголках баблов сообщений.")));

	secondsToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setTimeWithSecondsEnabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Стикеры и подсказки")));

	const auto greetingToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Отключить приветственные стикеры")),
		hooks.isGreetingStickerDisabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Скрывает большой анимированный приветственный стикер в пустых диалогах.")));

	greetingToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setGreetingStickerDisabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Профиль и информация")));

	const auto peerIdToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Показывать ID в профилях")),
		hooks.isShowPeerIdEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Отображает числовой User ID, Chat ID или Channel ID в блоке сведений профиля с возможностью быстрого копирования.")));

	peerIdToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setShowPeerIdEnabled(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Стиль Material 3 & Анимации")));

	const auto m3BubblesToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Material 3 скругления баблов (24px)")),
		hooks.isMaterial3BubblesEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Применяет мягкие скругления 24px для сообщений в стиле Material 3 Expressive.")));

	m3BubblesToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setMaterial3BubblesEnabled(checked);
	}, lifetime);

	const auto dividersToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Разделители строк в списке чатов")),
		hooks.isChatListDividersEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Отображает тонкие адаптивные линии между диалогами в списке чатов для улучшения структуры.")));

	dividersToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setChatListDividersEnabled(checked);
	}, lifetime);

	const auto m3IconsToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Контурные иконки Material 3")),
		hooks.isMaterial3IconsEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Включает современный контурный стиль иконок Material Symbols в системных меню.")));

	m3IconsToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setMaterial3IconsEnabled(checked);
	}, lifetime);

	const auto smoothScrollToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Плавный скроллинг списков (Smooth Scroll)")),
		hooks.isSmoothScrollEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Мягкая кинетическая интерполяция при прокрутке колесиком мыши в списке чатов и сообщений.")));

	smoothScrollToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setSmoothScrollEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
}

// ==========================================
// 3. Network Section (Сеть)
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

	const auto addCheckbox = [&](rpl::producer<QString> &&label, bool checked) {
		return content->add(
			object_ptr<Ui::Checkbox>(
				content,
				std::move(label),
				checked,
				st::settingsCheckbox),
			st::settingsCheckboxPadding);
	};

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Скорость и загрузка")));

	const auto boostToggle = addCheckbox(
		rpl::single(QString::fromUtf8("Ускоренная многопоточная загрузка (Download Boost)")),
		hooks.isDownloadBoostEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("Многопоточная параллельная загрузка файлов и медиа до 16 MTProto сессий одновременно, снимающая ограничение одиночного коннекта.")));

	boostToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setDownloadBoostEnabled(checked);
	}, lifetime);

	Ui::ResizeFitChild(this, content);
}

// ==========================================
// 4. Updates & Backup Section (Обновления)
// ==========================================
class ZenUpdates final : public Section<ZenUpdates> {
public:
	ZenUpdates(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

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

base::weak_qptr<Ui::RpWidget> ZenUpdates::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto search = CreateSectionSearchRow(parent);
	const auto row = search.row;
	return base::make_weak(row);
}

void ZenUpdates::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto ctrl = controller();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Обновления")));

	// 1. Проверить обновления
	const auto checkUpdateBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Проверить обновления")),
		st::settingsButton,
		{ .icon = &st::menuIconRestore, .type = IconType::Simple });

	checkUpdateBtn->setClickedCallback([=] {
		Relay::ZenUpdater::Instance().setController(ctrl);
		Relay::ZenUpdater::Instance().checkForUpdates(true);
	});

	// 2. Обновить бейджи
	const auto updateBadgesBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Обновить бейджи")),
		st::settingsButton,
		{ .icon = &st::menuIconRestore, .type = IconType::Simple });

	updateBadgesBtn->setClickedCallback([=] {
		Relay::ZenBadges::Instance().syncFromRemote(ctrl);
	});

	// 3. Список изменений
	const auto changelogBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Список изменений")),
		st::settingsButton,
		{ .icon = &st::menuIconInfo, .type = IconType::Simple });

	changelogBtn->setClickedCallback([=] {
		Relay::ZenUpdater::Instance().showChangelog(ctrl);
	});

	// 4. Автообновление (cycle mode on click)
	auto modeStr = rpl::single(Relay::RelayHooks::Instance().autoUpdateModeString());
	const auto autoUpdateBtn = AddButtonWithLabel(
		content,
		rpl::single(QString::fromUtf8("Автообновление")),
		rpl::duplicate(modeStr),
		st::settingsButton);

	autoUpdateBtn->setClickedCallback([=] {
		auto &hooks = Relay::RelayHooks::Instance();
		const auto current = hooks.autoUpdateMode();
		const auto next = (current == Relay::AutoUpdateMode::DownloadOnly)
			? Relay::AutoUpdateMode::AutoInstall
			: (current == Relay::AutoUpdateMode::AutoInstall)
			? Relay::AutoUpdateMode::Disabled
			: Relay::AutoUpdateMode::DownloadOnly;
		hooks.setAutoUpdateMode(next);
		ctrl->show(Ui::MakeInformBox(QString("Режим автообновления: %1").arg(hooks.autoUpdateModeString())));
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	// Section 2: Резервная копия
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Резервная копия")));

	// Экспорт настроек
	const auto exportBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Экспорт настроек")),
		st::settingsButton,
		{ .icon = &st::menuIconSettings, .type = IconType::Simple });

	exportBtn->setClickedCallback([=] {
		const auto initial = QString("ZenDesktop_config.json");
		FileDialog::GetWritePath(
			this,
			QString::fromUtf8("Экспорт настроек ZenDesktop"),
			QString("JSON (*.json);;All files (*.*)"),
			initial,
			[=](QString &&path) {
				if (!path.isEmpty()) {
					if (Relay::RelayHooks::Instance().exportSettings(path)) {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Настройки успешно экспортированы.")));
					} else {
						ctrl->show(Ui::MakeInformBox(QString::fromUtf8("Не удалось сохранить файл настроек.")));
					}
				}
			});
	});

	// Импорт настроек
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

	// Сброс настроек
	const auto resetBtn = AddButtonWithIcon(
		content,
		rpl::single(QString::fromUtf8("Сброс настроек")),
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
}

// ==========================================
// 5. Main Zen Section (ZenDesktop Main Hub)
// ==========================================
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
	return rpl::single(QString::fromUtf8("ZenDesktop"));
}

void Zen::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto ctrl = controller();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(QString::fromUtf8("Разделы настроек")));

	// 1. Приватность
	const auto privacyBtn = AddButtonWithLabel(
		content,
		rpl::single(QString::fromUtf8("Приватность")),
		rpl::single(QString::fromUtf8("Ghost Mode, Anti-Recall")),
		st::settingsButton,
		{ .icon = &st::menuIconStealth, .type = IconType::Simple });

	privacyBtn->setClickedCallback([=] {
		ctrl->showSettings(ZenPrivacyId());
	});

	// 2. Внешний вид
	const auto appearanceBtn = AddButtonWithLabel(
		content,
		rpl::single(QString::fromUtf8("Внешний вид")),
		rpl::single(QString::fromUtf8("Секунды, стикеры")),
		st::settingsButton,
		{ .icon = &st::menuIconPalette, .type = IconType::Simple });

	appearanceBtn->setClickedCallback([=] {
		ctrl->showSettings(ZenAppearanceId());
	});

	// 3. Сеть
	const auto networkBtn = AddButtonWithLabel(
		content,
		rpl::single(QString::fromUtf8("Сеть")),
		rpl::single(QString::fromUtf8("Download Boost")),
		st::settingsButton,
		{ .icon = &st::menuIconNetwork, .type = IconType::Simple });

	networkBtn->setClickedCallback([=] {
		ctrl->showSettings(ZenNetworkId());
	});

	// 4. Обновления
	const auto updatesBtn = AddButtonWithLabel(
		content,
		rpl::single(QString::fromUtf8("Обновления")),
		rpl::single(QString::fromUtf8("Бейджи, OTA, бэкап")),
		st::settingsButton,
		{ .icon = &st::menuIconRestore, .type = IconType::Simple });

	updatesBtn->setClickedCallback([=] {
		ctrl->showSettings(ZenUpdatesId());
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	// RelayCore Stats block
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
	Ui::AddDividerText(
		content,
		rpl::single(QString::fromUtf8("ZenDesktop v1.0.0 (build 2601) • RelayCore 2.4")));

	Ui::ResizeFitChild(this, content);
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

Type ZenNetworkId() {
	return ZenNetwork::Id();
}

Type ZenUpdatesId() {
	return ZenUpdates::Id();
}

} // namespace Settings
