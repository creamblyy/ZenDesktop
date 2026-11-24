/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_lyrics_box.h"

#include "media/player/media_player_instance.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/rp_widget.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_media_player.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QMouseEvent>
#include <chrono>
#include <functional>
#include <memory>

namespace Relay {
namespace {

constexpr auto kBoxWidth = 760;
constexpr auto kBoxHeight = 460;
constexpr auto kOuterPad = 24;
constexpr auto kGap = 20;
constexpr auto kLeftWidth = 280;
constexpr auto kCoverSize = 240;
constexpr auto kContentPad = (kLeftWidth - kCoverSize) / 2;
constexpr auto kContentWidth = kCoverSize;

QString FormatTime(float64 sec) {
	if (sec < 0) sec = 0;
	const auto totalSec = int(sec);
	const auto m = totalSec / 60;
	const auto s = totalSec % 60;
	return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

} // namespace

ZenLyricsBox::ZenLyricsBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BoxContent()
, _controller(controller) {
}

ZenLyricsBox::~ZenLyricsBox() {
	if (_reply) {
		_reply->abort();
	}
}

void ZenLyricsBox::prepare() {
	setDimensions(kBoxWidth, kBoxHeight);
	setNoContentMargin(true);

	setupContent();

	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		if (state.id.type() == AudioMsgId::Type::Song) {
			_isPlaying = (state.state == Media::Player::State::Playing);
			if (state.frequency > 0) {
				_currentPosition = float64(state.position) / state.frequency;
				if (state.length > 0) {
					_duration = float64(state.length) / state.frequency;
				}
			}
			updatePlaybackState();
		}
	}, lifetime());

	Media::Player::instance()->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == AudioMsgId::Type::Song);
	}) | rpl::on_next([=] {
		handleTrackChange();
	}, lifetime());

	handleTrackChange();
}

void ZenLyricsBox::handleTrackChange() {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	_currentAudioId = current;
	_currentTitle = QString();
	_currentPerformer = QString();
	_duration = 0.;
	_coverMedia = nullptr;

	if (const auto doc = current.audio()) {
		if (const auto song = doc->song()) {
			_currentTitle = song->title;
			_currentPerformer = song->performer;
		}
		if (_currentTitle.isEmpty()) {
			_currentTitle = doc->filename();
		}
		_duration = doc->duration();
		_coverMedia = doc->createMediaView();
		if (_coverMedia) {
			_coverMedia->thumbnailWanted(current.contextId());
		}
	}

	if (_currentTitle.isEmpty()) {
		_currentTitle = "ZenDesktop Audio";
	}

	if (_titleLabel) {
		_titleLabel->setText(_currentTitle);
	}
	if (_artistLabel) {
		_artistLabel->setText(_currentPerformer.isEmpty() ? "—" : _currentPerformer);
	}
	if (_cover) {
		_cover->update();
	}

	fetchLyrics(_currentTitle, _currentPerformer);
	updatePlaybackState();
}

void ZenLyricsBox::setupContent() {
	// Root horizontal containers
	_leftCol = Ui::CreateChild<QWidget>(this);
	_rightCol = Ui::CreateChild<Ui::RpWidget>(this);

	const auto contentHeight = kBoxHeight - kOuterPad * 2;
	const auto rightX = kOuterPad + kLeftWidth + kGap;
	const auto rightWidth = kBoxWidth - kLeftWidth - kOuterPad * 2 - kGap;
	_leftCol->setGeometry(kOuterPad, kOuterPad, kLeftWidth, contentHeight);
	_rightCol->setGeometry(rightX, kOuterPad, rightWidth, contentHeight);
	_rightCol->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_rightCol);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(32, 38, 50));
		p.drawRoundedRect(QRectF(_rightCol->rect()), 18., 18.);
	}, _rightCol->lifetime());

	// 1. Cover Art
	_cover = Ui::CreateChild<Ui::RpWidget>(_leftCol);
	_cover->setGeometry(kContentPad, 0, kCoverSize, kCoverSize);
	_cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_cover);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(_cover->rect());

		QPainterPath path;
		path.addRoundedRect(r, 18., 18.);
		p.setClipPath(path);

		auto drawnCover = false;
		if (_coverMedia) {
			if (const auto thumb = _coverMedia->thumbnail()) {
				const auto pix = thumb->pix(
					QSize(kCoverSize, kCoverSize) * style::DevicePixelRatio());
				if (!pix.isNull()) {
					p.drawPixmap(_cover->rect(), pix);
					drawnCover = true;
				}
			}
		}

		if (!drawnCover) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(36, 42, 54));
			p.drawRoundedRect(r, 18., 18.);

			p.setPen(QColor(140, 163, 188, 120));
			auto font = p.font();
			font.setPixelSize(48);
			p.setFont(font);
			p.drawText(_cover->rect(), Qt::AlignCenter, QString::fromUtf8("🎵"));
		}
	}, _cover->lifetime());

	// 2. Track Title & Artist
	_titleLabel = Ui::CreateChild<Ui::FlatLabel>(_leftCol, _currentTitle.isEmpty() ? "..." : _currentTitle, st::defaultFlatLabel);
	_titleLabel->setGeometry(kContentPad, kCoverSize + 14, kContentWidth, 24);
	_titleLabel->setFont(st::semiboldFont);
	_titleLabel->setTextColorOverride(QColor(245, 247, 250));

	_artistLabel = Ui::CreateChild<Ui::FlatLabel>(_leftCol, _currentPerformer.isEmpty() ? "..." : _currentPerformer, st::defaultFlatLabel);
	_artistLabel->setGeometry(kContentPad, kCoverSize + 38, kContentWidth, 20);
	_artistLabel->setFont(st::normalFont);
	_artistLabel->setTextColorOverride(QColor(127, 139, 155));

	// 3. Progress Scrubber
	_progressBar = Ui::CreateChild<Ui::RpWidget>(_leftCol);
	_progressBar->setGeometry(kContentPad, kCoverSize + 66, kContentWidth, 16);
	_progressBar->setCursor(style::cur_pointer);

	_progressBar->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_progressBar);
		auto hq = PainterHighQualityEnabler(p);
		const auto h = 4;
		const auto top = (_progressBar->height() - h) / 2;
		p.setPen(Qt::NoPen);
		// Background track
		p.setBrush(QColor(42, 51, 65));
		p.drawRoundedRect(QRectF(0, top, _progressBar->width(), h), 2., 2.);

		// Active progress fill
		const auto progress = (_duration > 0.)
			? std::clamp(_currentPosition / _duration, 0., 1.)
			: 0.;
		const auto fillWidth = int(_progressBar->width() * progress);
		if (fillWidth > 0) {
			p.setBrush(QColor(154, 176, 199));
			p.drawRoundedRect(QRectF(0, top, fillWidth, h), 2., 2.);
			// Thumb
			p.setBrush(QColor(255, 255, 255));
			p.drawRoundedRect(QRectF(fillWidth - 3, top - 4, 6, 12), 3., 3.);
		}
	}, _progressBar->lifetime());

	_progressBar->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseMove) {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->buttons() & Qt::LeftButton) {
				const auto ratio = std::clamp(float64(me->pos().x()) / _progressBar->width(), 0., 1.);
				Media::Player::instance()->finishSeeking(AudioMsgId::Type::Song, ratio);
				_currentPosition = ratio * _duration;
				_progressBar->update();
			}
		}
	}, _progressBar->lifetime());

	// 4. Timestamps
	_elapsedLabel = Ui::CreateChild<Ui::FlatLabel>(_leftCol, "0:00", st::defaultFlatLabel);
	_elapsedLabel->resizeToNaturalWidth(120);
	_elapsedLabel->move(kContentPad, kCoverSize + 84);
	_elapsedLabel->setTextColorOverride(QColor(127, 139, 155));

	_remainingLabel = Ui::CreateChild<Ui::FlatLabel>(_leftCol, "-0:00", st::defaultFlatLabel);
	_remainingLabel->resizeToNaturalWidth(120);
	_remainingLabel->move(kContentPad + kContentWidth - _remainingLabel->width(), kCoverSize + 84);
	_remainingLabel->setTextColorOverride(QColor(127, 139, 155));

	// 5. Primary Controls: Prev, Play/Pause, Next
	const auto controlsCenterY = kCoverSize + 136;
	_playBtn = Ui::CreateChild<Ui::RpWidget>(_leftCol);
	_playBtn->setGeometry((kLeftWidth - 52) / 2, controlsCenterY - 26, 52, 52);
	_playBtn->setCursor(style::cur_pointer);

	_playBtn->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_playBtn);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		// Material 3 Circular Play container
		p.setBrush(QColor(52, 63, 79));
		p.drawEllipse(_playBtn->rect());

		p.setBrush(QColor(255, 255, 255));
		const auto cx = _playBtn->width() / 2;
		const auto cy = _playBtn->height() / 2;

		if (_isPlaying) {
			// Pause bars
			p.drawRoundedRect(QRectF(cx - 7, cy - 8, 4, 16), 1.5, 1.5);
			p.drawRoundedRect(QRectF(cx + 3, cy - 8, 4, 16), 1.5, 1.5);
		} else {
			// Play triangle
			QPainterPath tri;
			tri.moveTo(cx - 5, cy - 9);
			tri.lineTo(cx + 8, cy);
			tri.lineTo(cx - 5, cy + 9);
			tri.closeSubpath();
			p.drawPath(tri);
		}
	}, _playBtn->lifetime());

	_playBtn->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonRelease) {
			Media::Player::instance()->playPause();
			_isPlaying = !_isPlaying;
			_playBtn->update();
		}
	}, _playBtn->lifetime());

	const auto prevBtn = Ui::CreateChild<Ui::IconButton>(_leftCol, st::mediaPlayerPreviousButton);
	prevBtn->move(_playBtn->x() - 16 - prevBtn->width(), controlsCenterY - prevBtn->height() / 2);
	prevBtn->setClickedCallback([=] {
		Media::Player::instance()->previous();
	});

	const auto nextBtn = Ui::CreateChild<Ui::IconButton>(_leftCol, st::mediaPlayerNextButton);
	nextBtn->move(_playBtn->x() + _playBtn->width() + 16, controlsCenterY - nextBtn->height() / 2);
	nextBtn->setClickedCallback([=] {
		Media::Player::instance()->next();
	});

	// 6. Right Column: Lyrics container & scroll
	_scroll = Ui::CreateChild<Ui::ScrollArea>(_rightCol, st::defaultScrollArea);
	_scroll->setGeometry(16, 16, _rightCol->width() - 32, _rightCol->height() - 32);
	_scroll->viewport()->setAutoFillBackground(false);

	_lyricsContainer = Ui::CreateChild<QWidget>(_scroll);
	_lyricsContainer->setAutoFillBackground(false);
	_scroll->setWidget(_lyricsContainer);
	_lyricsContainer->show();
}

void ZenLyricsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	if (_leftCol && _rightCol) {
		const auto contentHeight = std::max(100, height() - kOuterPad * 2);
		const auto rightX = kOuterPad + kLeftWidth + kGap;
		const auto rightWidth = std::max(160, width() - kLeftWidth - kOuterPad * 2 - kGap);
		_leftCol->setGeometry(kOuterPad, kOuterPad, kLeftWidth, contentHeight);
		_rightCol->setGeometry(rightX, kOuterPad, rightWidth, contentHeight);
		if (_scroll) {
			_scroll->setGeometry(16, 16, _rightCol->width() - 32, _rightCol->height() - 32);
		}
	}
}

void ZenLyricsBox::showStatusMessage(const QString &text) {
	_lyrics.clear();
	_activeLineIdx = -1;
	qDeleteAll(_lineWidgets);
	_lineWidgets.clear();

	if (!_scroll || !_lyricsContainer) {
		return;
	}

	const auto containerWidth = std::max(140, _scroll->width());
	const auto label = Ui::CreateChild<Ui::FlatLabel>(_lyricsContainer, text, st::defaultFlatLabel);
	label->setTextColorOverride(QColor(150, 165, 180));
	label->resizeToWidth(std::max(100, containerWidth - 28));
	label->move(14, 16);
	label->show();

	_lineWidgets.push_back(label);
	_lyricsContainer->resize(containerWidth, std::max(label->height() + 32, _scroll->height()));
	_lyricsContainer->show();
}

void ZenLyricsBox::fetchLyrics(const QString &title, const QString &performer) {
	if (const auto prev = _reply.data()) {
		_reply = nullptr;
		prev->abort();
		prev->deleteLater();
	}

	_triedTitleFallback = false;
	showStatusMessage(QString::fromUtf8("Поиск текста..."));

	const auto cleanedTitle = [&] {
		auto s = title;
		s.remove(QRegularExpression(R"(\s*[\(\[][^\)\]]*[\)\]])"));
		s = s.trimmed();
		return s.isEmpty() ? title.trimmed() : s;
	}();
	const auto hasPerformer = !performer.trimmed().isEmpty();
	const auto initialQuery = (performer.trimmed() + " " + title.trimmed()).trimmed();

	const auto runQuery = std::make_shared<std::function<void(const QString&)>>();
	lifetime().add([runQuery] {});
	const auto weakRun = std::weak_ptr<std::function<void(const QString&)>>(runQuery);
	*runQuery = [this, weakRun, cleanedTitle, hasPerformer](const QString &query) {
		if (query.isEmpty()) {
			showStatusMessage(QString::fromUtf8("Текст песни не найден в базе."));
			return;
		}
		const auto url = QUrl(QString("https://lrclib.net/api/search?q=%1").arg(
			QString::fromUtf8(QUrl::toPercentEncoding(query))));

		auto req = QNetworkRequest(url);
		req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
		req.setTransferTimeout(std::chrono::milliseconds(15000));
		req.setHeader(QNetworkRequest::UserAgentHeader, "ZenDesktop/1.0.0");

		const auto reply = _net.get(req);
		_reply = reply;
		connect(reply, &QNetworkReply::finished, this, [this, reply, weakRun, cleanedTitle, hasPerformer] {
			reply->deleteLater();
			if (_reply.data() != reply) {
				return;
			}
			_reply = nullptr;

			const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (reply->error() != QNetworkReply::NoError || status >= 400) {
				showStatusMessage(QString::fromUtf8("Не удалось загрузить текст песни из сети.\nПроверьте подключение к интернету."));
				return;
			}

			const auto data = reply->readAll();
			const auto doc = QJsonDocument::fromJson(data);
			if (!doc.isArray() || doc.array().isEmpty()) {
				if (!_triedTitleFallback && hasPerformer && !cleanedTitle.isEmpty()) {
					_triedTitleFallback = true;
					if (const auto fn = weakRun.lock(); fn && *fn) {
						(*fn)(cleanedTitle);
					}
					return;
				}
				showStatusMessage(QString::fromUtf8("Текст песни не найден в базе."));
				return;
			}

			QJsonObject best;
			for (const auto &entry : doc.array()) {
				const auto obj = entry.toObject();
				if (!obj["syncedLyrics"].toString().isEmpty()) {
					best = obj;
					break;
				}
				if (best.isEmpty() && !obj["plainLyrics"].toString().isEmpty()) {
					best = obj;
				}
			}
			if (best.isEmpty()) {
				best = doc.array().first().toObject();
			}

			const auto synced = best["syncedLyrics"].toString();
			const auto plain = best["plainLyrics"].toString();
			const auto dur = best["duration"].toDouble();
			if (dur > 0.) {
				_duration = dur;
			}

			if (!synced.isEmpty()) {
				parseLrc(synced);
			} else if (!plain.isEmpty()) {
				parsePlain(plain, _duration);
			} else {
				showStatusMessage(QString::fromUtf8("Инструментальный трек (без слов)."));
			}
		});
	};

	(*runQuery)(initialQuery);
}

void ZenLyricsBox::parseLrc(const QString &lrc) {
	_lyrics.clear();
	const auto lines = lrc.split('\n');
	const auto regex = QRegularExpression(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))");

	for (const auto &line : lines) {
		const auto match = regex.match(line);
		if (match.hasMatch()) {
			const auto m = match.captured(1).toInt();
			const auto s = match.captured(2).toInt();
			const auto ms = ("0." + match.captured(3)).toDouble();
			const auto t = m * 60. + s + ms;
			const auto text = match.captured(4).trimmed();
			if (!text.isEmpty()) {
				_lyrics.push_back({ t, text });
			}
		}
	}
	renderLyrics();
}

void ZenLyricsBox::parsePlain(const QString &plain, float64 duration) {
	_lyrics.clear();
	const auto lines = plain.split('\n');
	QStringList nonEmpty;
	for (const auto &l : lines) {
		const auto trimmed = l.trimmed();
		if (!trimmed.isEmpty()) {
			nonEmpty.append(trimmed);
		}
	}
	if (nonEmpty.isEmpty()) {
		showStatusMessage(QString::fromUtf8("Текст песни не найден в базе."));
		return;
	}

	const auto step = (duration > 0.) ? (duration / nonEmpty.size()) : 4.;
	for (auto i = 0; i < nonEmpty.size(); ++i) {
		_lyrics.push_back({ i * step, nonEmpty[i] });
	}
	renderLyrics();
}

void ZenLyricsBox::renderLyrics() {
	_activeLineIdx = -1;
	qDeleteAll(_lineWidgets);
	_lineWidgets.clear();

	if (!_scroll || !_lyricsContainer) {
		return;
	}
	if (_lyrics.empty()) {
		showStatusMessage(QString::fromUtf8("Инструментальный трек (без слов)."));
		return;
	}

	const auto containerWidth = std::max(140, _scroll->width());
	const auto labelWidth = std::max(100, containerWidth - 28);
	auto y = 12;

	for (auto i = 0; i < int(_lyrics.size()); ++i) {
		const auto &item = _lyrics[i];
		const auto label = Ui::CreateChild<Ui::FlatLabel>(_lyricsContainer, item.text, st::defaultFlatLabel);
		label->setTextColorOverride(QColor(100, 115, 130));
		label->setFont(st::normalFont);
		label->setCursor(style::cur_pointer);
		label->resizeToWidth(labelWidth);
		label->move(14, y);
		label->show();

		const auto targetTime = item.time;
		label->events() | rpl::on_next([=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonRelease) {
				if (_duration > 0) {
					const auto ratio = std::clamp(targetTime / _duration, 0.0, 1.0);
					Media::Player::instance()->finishSeeking(AudioMsgId::Type::Song, ratio);
				}
				_currentPosition = targetTime;
				updateActiveLine(targetTime);
			}
		}, label->lifetime());

		_lineWidgets.push_back(label);
		y += label->height() + 10;
	}

	_lyricsContainer->resize(containerWidth, std::max(y + 16, _scroll->height()));
	_lyricsContainer->show();
	updateActiveLine(_currentPosition);
}

void ZenLyricsBox::updatePlaybackState() {
	if (_elapsedLabel) {
		_elapsedLabel->setText(FormatTime(_currentPosition));
		_elapsedLabel->resizeToNaturalWidth(120);
		_elapsedLabel->move(kContentPad, kCoverSize + 84);
	}
	if (_remainingLabel) {
		const auto rem = std::max(0., _duration - _currentPosition);
		_remainingLabel->setText("-" + FormatTime(rem));
		_remainingLabel->resizeToNaturalWidth(120);
		_remainingLabel->move(kContentPad + kContentWidth - _remainingLabel->width(), kCoverSize + 84);
	}
	if (_progressBar) {
		_progressBar->update();
	}
	if (_playBtn) {
		_playBtn->update();
	}

	updateActiveLine(_currentPosition);
}

void ZenLyricsBox::updateActiveLine(float64 seconds) {
	if (_lyrics.empty()) return;

	int activeIdx = -1;
	for (auto i = 0; i < int(_lyrics.size()); ++i) {
		if (seconds >= _lyrics[i].time) {
			activeIdx = i;
		} else {
			break;
		}
	}

	if (activeIdx != _activeLineIdx) {
		if (_activeLineIdx >= 0 && _activeLineIdx < int(_lineWidgets.size())) {
			if (const auto oldLabel = _lineWidgets[_activeLineIdx]) {
				oldLabel->setTextColorOverride(QColor(100, 115, 130));
				oldLabel->setFont(st::normalFont);
			}
		}

		_activeLineIdx = activeIdx;

		if (_activeLineIdx >= 0 && _activeLineIdx < int(_lineWidgets.size())) {
			if (const auto newLabel = _lineWidgets[_activeLineIdx]) {
				newLabel->setTextColorOverride(QColor(255, 255, 255));
				newLabel->setFont(st::semiboldFont);

				if (_autoScroll && _scroll) {
					const auto lineY = newLabel->y();
					const auto targetY = lineY - (_scroll->height() / 2) + (newLabel->height() / 2);
					_scroll->scrollToY(std::max(0, targetY));
				}
			}
		}
	}
}

} // namespace Relay
