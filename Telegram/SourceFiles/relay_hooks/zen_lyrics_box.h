/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#pragma once

#include "ui/layers/box_content.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Window {
class SessionController;
} // namespace Window

namespace Relay {

struct LyricLine {
	float64 time = 0.;
	QString text;
};

class ZenLyricsBox final : public Ui::BoxContent {
public:
	ZenLyricsBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~ZenLyricsBox();

protected:
	void prepare() override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupContent();
	void fetchLyrics(const QString &title, const QString &performer);
	void parseLrc(const QString &lrc);
	void parsePlain(const QString &plain, float64 duration);
	void updatePlaybackState();
	void updateActiveLine(float64 seconds);
	void renderLyrics();

	const not_null<Window::SessionController*> _controller;
	QNetworkAccessManager _net;
	QPointer<QNetworkReply> _reply;

	std::vector<LyricLine> _lyrics;
	int _activeLineIdx = -1;
	bool _autoScroll = true;

	QString _currentTitle;
	QString _currentPerformer;
	float64 _duration = 0.;
	float64 _currentPosition = 0.;
	bool _isPlaying = false;

	QWidget *_leftCol = nullptr;
	QWidget *_rightCol = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	QWidget *_lyricsContainer = nullptr;
	std::vector<QWidget*> _lineWidgets;

	Ui::RpWidget *_cover = nullptr;
	Ui::FlatLabel *_titleLabel = nullptr;
	Ui::FlatLabel *_artistLabel = nullptr;
	Ui::RpWidget *_progressBar = nullptr;
	Ui::FlatLabel *_elapsedLabel = nullptr;
	Ui::FlatLabel *_remainingLabel = nullptr;
	Ui::IconButton *_playBtn = nullptr;
};

} // namespace Relay
