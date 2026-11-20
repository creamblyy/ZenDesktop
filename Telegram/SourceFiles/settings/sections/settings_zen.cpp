/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "settings/sections/settings_zen.h"

#include "settings/settings_common_session.h"
#include "settings/settings_common.h"
#include "relay_hooks/relay_hooks.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

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
	return rpl::single(u"ZenDesktop"_q);
}

void Zen::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &lifetime = content->lifetime();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"RelayHooks & Стелс"_q));

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

	// Ghost mode toggle
	const auto ghostToggle = addCheckbox(
		rpl::single(u"Ghost Mode (Стелс-режим)"_q),
		hooks.isGhostModeEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(u"Скрывает факт прочтения входящих сообщений и предотвращает отправку статуса набора текста."_q));

	// Anti-recall toggle
	const auto antiRecallToggle = addCheckbox(
		rpl::single(u"Anti-Recall (Сохранение удаленных сообщений)"_q),
		hooks.isAntiRecallEnabled());

	Ui::AddDividerText(
		content,
		rpl::single(u"Сохраняет сообщения локально, даже если собеседник удалил их для всех."_q));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"Детальные настройки"_q));

	const auto typingToggle = addCheckbox(
		rpl::single(u"Скрывать статус набора текста"_q),
		hooks.isTypingHidden());

	const auto readToggle = addCheckbox(
		rpl::single(u"Скрывать отчет о прочтении"_q),
		hooks.isReadHistoryHidden());

	ghostToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setGhostModeEnabled(checked);
		typingToggle->setChecked(Relay::RelayHooks::Instance().isTypingHidden());
		readToggle->setChecked(Relay::RelayHooks::Instance().isReadHistoryHidden());
	}, lifetime);

	antiRecallToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setAntiRecallEnabled(checked);
	}, lifetime);

	typingToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setTypingHidden(checked);
	}, lifetime);

	readToggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		Relay::RelayHooks::Instance().setReadHistoryHidden(checked);
	}, lifetime);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"Статистика RelayHooks"_q));

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

	const auto statsLabel = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(formatStats()),
			st::settingsDividerLabel),
		st::settingsCheckboxPadding);

	const auto resetButton = AddButtonWithIcon(
		content,
		rpl::single(u"Сбросить счетчики перехватов"_q),
		st::settingsButton);

	resetButton->setClickedCallback([=] {
		Relay::RelayHooks::Instance().resetStats();
		statsLabel->setText(formatStats());
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddDividerText(
		content,
		rpl::single(u"ZenDesktop (Windows) — клиент на базе RelayHooks."_q));

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type ZenId() {
	return Zen::Id();
}

} // namespace Settings
