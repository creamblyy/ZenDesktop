/*
This file is part of Telegram Desktop fork - ZenDesktop (Power Tools).
*/
#include "relay_hooks/zen_power_tools.h"
#include "relay_hooks/zen_m3_palette.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/weak_ptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QVariant>
#include <QtGui/QDesktopServices>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QHttpPart>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Relay {
namespace {

struct PowerToolsConfig {
	bool loaded = false;
	bool inlineCalc = true;
	bool translateBeforeSend = true;
	bool zalgoFilter = true;
	QString sendTranslateLang = u"en"_q;
};

PowerToolsConfig &Config() {
	static PowerToolsConfig cfg;
	if (!cfg.loaded) {
		cfg.loaded = true;
		QSettings s(u"ZenDesktop"_q, u"PowerTools"_q);
		cfg.inlineCalc = s.value(u"inline_calc"_q, true).toBool();
		cfg.translateBeforeSend = s.value(u"translate_before_send"_q, true).toBool();
		cfg.zalgoFilter = s.value(u"zalgo_filter"_q, true).toBool();
		const auto savedLang = s.value(u"send_translate_lang"_q, u"en"_q).toString().trimmed();
		if (!savedLang.isEmpty()) {
			cfg.sendTranslateLang = savedLang;
		}
	}
	return cfg;
}

void SaveConfig() {
	const auto &cfg = Config();
	QSettings s(u"ZenDesktop"_q, u"PowerTools"_q);
	s.setValue(u"inline_calc"_q, cfg.inlineCalc);
	s.setValue(u"translate_before_send"_q, cfg.translateBeforeSend);
	s.setValue(u"zalgo_filter"_q, cfg.zalgoFilter);
	s.setValue(u"send_translate_lang"_q, cfg.sendTranslateLang);
	s.sync();
}

QNetworkAccessManager *SharedNetwork() {
	static QNetworkAccessManager manager;
	return &manager;
}

[[nodiscard]] uint64 BarePeerId(PeerId id) {
	if (const auto u = peerToUser(id); u.bare) {
		return u.bare;
	}
	if (const auto c = peerToChannel(id); c.bare) {
		return c.bare;
	}
	if (const auto g = peerToChat(id); g.bare) {
		return g.bare;
	}
	return id.value;
}

[[nodiscard]] QString PeerSummary(PeerData *peer) {
	if (!peer) {
		return QString();
	}
	auto out = QString::fromUtf8("%1 (ID: %2)")
		.arg(peer->name(), QString::number(BarePeerId(peer->id)));
	if (const auto uname = peer->username(); !uname.isEmpty()) {
		out += u" (@"_q + uname + u")"_q;
	}
	return out;
}

// ==========================================
// 1. Zalgo diacritics limiter
// ==========================================
[[nodiscard]] int MaxMarksForBase(ushort ch) {
	if ((ch >= 0x0590 && ch <= 0x05FF) // Hebrew
		|| (ch >= 0x0600 && ch <= 0x06FF) // Arabic
		|| (ch >= 0x0750 && ch <= 0x077F)
		|| (ch >= 0x08A0 && ch <= 0x08FF)
		|| (ch >= 0x0900 && ch <= 0x0D7F) // Indic scripts
		|| (ch >= 0x0E00 && ch <= 0x0E7F) // Thai
		|| (ch >= 0x1000 && ch <= 0x109F) // Myanmar
		|| (ch >= 0x1780 && ch <= 0x17FF)) { // Khmer
		return 4;
	}
	return 2;
}

// ==========================================
// 2. JPEG Header & Quality Fingerprint
// ==========================================
[[nodiscard]] QString InspectJpegBytes(const QByteArray &bytes) {
	if (bytes.size() < 4
		|| static_cast<uchar>(bytes[0]) != 0xFF
		|| static_cast<uchar>(bytes[1]) != 0xD8) {
		return QString();
	}

	static constexpr int kZigzag[64] = {
		0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
		12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
		35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
		58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
	};
	static constexpr int kStdLuma[64] = {
		16, 11, 10, 16, 24, 40, 51, 61,
		12, 12, 14, 19, 26, 58, 60, 55,
		14, 13, 16, 24, 40, 57, 69, 56,
		14, 17, 22, 29, 51, 87, 80, 62,
		18, 22, 37, 56, 68, 109, 103, 77,
		24, 35, 55, 64, 81, 104, 113, 92,
		49, 64, 78, 87, 103, 121, 120, 101,
		72, 92, 95, 98, 112, 100, 103, 99
	};

	const int limit = std::min<int>(bytes.size(), 256 * 1024);
	int pos = 2;
	bool hasExif = false;
	bool hasXmp = false;
	bool hasIcc = false;
	bool hasSof = false;
	bool progressive = false;
	int quality = -1;

	while (pos + 1 < limit) {
		while (pos < limit && static_cast<uchar>(bytes[pos]) != 0xFF) {
			++pos;
		}
		while (pos < limit && static_cast<uchar>(bytes[pos]) == 0xFF) {
			++pos;
		}
		if (pos >= limit) {
			break;
		}
		const uchar marker = static_cast<uchar>(bytes[pos++]);
		if (marker == 0x00 || marker == 0xD8 || marker == 0x01
			|| (marker >= 0xD0 && marker <= 0xD7)) {
			continue;
		}
		if (marker == 0xD9 || marker == 0xDA) {
			break;
		}
		if (pos + 2 > limit) {
			break;
		}
		const int segLen = (static_cast<uchar>(bytes[pos]) << 8)
			| static_cast<uchar>(bytes[pos + 1]);
		pos += 2;
		if (segLen < 2 || pos + (segLen - 2) > limit) {
			break;
		}
		const int payloadStart = pos;
		const int payloadLen = segLen - 2;
		const char *payload = bytes.constData() + payloadStart;

		if (marker == 0xC0) {
			hasSof = true;
			progressive = false;
		} else if (marker == 0xC2) {
			hasSof = true;
			progressive = true;
		} else if (marker == 0xE1) {
			if (payloadLen >= 6 && memcmp(payload, "Exif\0\0", 6) == 0) {
				hasExif = true;
			} else if (payloadLen >= 29
				&& memcmp(payload, "http://ns.adobe.com/xap/1.0/\0", 29) == 0) {
				hasXmp = true;
			}
		} else if (marker == 0xE2) {
			if (payloadLen >= 12 && memcmp(payload, "ICC_PROFILE\0", 12) == 0) {
				hasIcc = true;
			}
		} else if (marker == 0xDB && quality < 0) {
			int qPos = payloadStart;
			const int qEnd = payloadStart + payloadLen;
			while (qPos < qEnd) {
				const uchar info = static_cast<uchar>(bytes[qPos++]);
				const int pq = (info >> 4) & 0x0F;
				const int tq = info & 0x0F;
				const int elemSize = (pq == 0) ? 1 : 2;
				if (qPos + 64 * elemSize > qEnd) {
					break;
				}
				if (tq == 0 && pq == 0) {
					int table[64]{};
					double scaleSum = 0.0;
					for (int k = 0; k < 64; ++k) {
						table[kZigzag[k]] = static_cast<uchar>(bytes[qPos + k]);
					}
					for (int k = 0; k < 64; ++k) {
						scaleSum += (100.0 * double(table[k])) / double(kStdLuma[k]);
					}
					const double avgScale = scaleSum / 64.0;
					const int estQ = (avgScale <= 100.0)
						? int(std::round((200.0 - avgScale) / 2.0))
						: int(std::round(5000.0 / avgScale));
					quality = std::clamp(estQ, 1, 100);
					break;
				}
				qPos += 64 * elemSize;
			}
		}

		pos = payloadStart + payloadLen;
	}

	QStringList parts;
	if (quality > 0) {
		parts.append(QString::fromUtf8("JPEG ~%1%").arg(quality));
	} else {
		parts.append(u"JPEG"_q);
	}
	if (hasSof) {
		parts.append(progressive ? u"Progressive"_q : u"Baseline"_q);
	}
	parts.append(hasExif ? u"EXIF"_q : u"без EXIF"_q);
	if (hasXmp) {
		parts.append(u"XMP"_q);
	}
	if (hasIcc) {
		parts.append(u"ICC"_q);
	}
	return parts.join(QString::fromUtf8(" · "));
}

// ==========================================
// 3. Message Details Row Widget
// ==========================================
void AddDetailCopyRow(
		not_null<Ui::VerticalLayout*> container,
		base::weak_ptr<Window::SessionController> weakCtrl,
		const QString &label,
		const QString &value) {
	if (value.trimmed().isEmpty()) {
		return;
	}
	const auto btn = container->add(object_ptr<Ui::AbstractButton>(container));
	btn->setPointerCursor(true);

	const auto titleLbl = Ui::CreateChild<Ui::FlatLabel>(
		btn,
		rpl::single(label),
		st::boxDividerLabel);
	titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto valueLbl = Ui::CreateChild<Ui::FlatLabel>(
		btn,
		rpl::single(value),
		st::boxLabel);
	valueLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
	valueLbl->setSelectable(false);

	const auto layoutRow = [=](int width) {
		const int w = std::max(width, 160);
		const int textW = std::max(60, w - 44);
		titleLbl->resizeToWidth(textW);
		valueLbl->resizeToWidth(textW);
		titleLbl->moveToLeft(22, 8, w);
		valueLbl->moveToLeft(22, 8 + titleLbl->height() + 2, w);
		const int totalH = 8 + titleLbl->height() + 2 + valueLbl->height() + 9;
		if (btn->height() != totalH) {
			btn->setMinimumHeight(totalH);
			btn->setMaximumHeight(totalH);
			btn->resize(w, totalH);
		}
	};

	layoutRow(st::boxWideWidth);
	btn->widthValue() | rpl::on_next([=](int w) {
		if (w > 0) {
			layoutRow(w);
		}
	}, btn->lifetime());

	btn->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(btn);
		if (btn->isOver()) {
			p.fillRect(btn->rect(), st::windowBgOver->c);
		}
		auto lineColor = st::windowFg->c;
		lineColor.setAlphaF(0.08);
		const int w = std::max(0, btn->width() - 44);
		if (w > 0 && btn->height() > 0) {
			p.fillRect(22, btn->height() - 1, w, 1, lineColor);
		}
	}, btn->lifetime());

	btn->setClickedCallback([=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(value));
		if (const auto strong = weakCtrl.get()) {
			const auto shortVal = (value.size() > 54)
				? (value.left(51) + u"..."_q)
				: value;
			strong->showToast(QString::fromUtf8("Скопировано: %1").arg(shortVal));
		}
	});
}

// ==========================================
// 4. Reverse Image Search
// ==========================================
enum class ImageSearchProvider {
	Yandex,
	Google,
	Bing,
	TinEye,
};

[[nodiscard]] QString BuildReverseSearchUrl(
		ImageSearchProvider provider,
		const QString &uploadHost,
		const QString &cbirId,
		const QString &imageUrl) {
	const auto encodedImage = QString::fromUtf8(QUrl::toPercentEncoding(imageUrl));
	const auto encodedCbir = QString::fromUtf8(QUrl::toPercentEncoding(cbirId));
	const auto host = uploadHost.isEmpty() ? u"yandex.com"_q : uploadHost;
	switch (provider) {
	case ImageSearchProvider::Yandex:
		return QString::fromUtf8("https://%1/images/search?rpt=imageview&cbir_id=%2&url=%3")
			.arg(host, encodedCbir, encodedImage);
	case ImageSearchProvider::Google:
		return QString::fromUtf8("https://lens.google.com/uploadbyurl?url=%1")
			.arg(encodedImage);
	case ImageSearchProvider::Bing:
		return QString::fromUtf8("https://www.bing.com/images/search?view=detailv2&iss=sbi&form=SBIVSP&sbisrc=UrlPaste&q=imgurl:%1")
			.arg(encodedImage);
	case ImageSearchProvider::TinEye:
		return QString::fromUtf8("https://tineye.com/search?url=%1")
			.arg(encodedImage);
	}
	return QString();
}

void StartReverseImageSearch(
		base::weak_ptr<Window::SessionController> weakCtrl,
		PhotoId photoId,
		ImageSearchProvider provider) {
	const auto strong = weakCtrl.get();
	if (!strong) {
		return;
	}
	const auto photo = strong->session().data().photo(photoId);
	const auto media = photo->activeMediaView();
	QImage img;
	if (media) {
		if (const auto large = media->image(Data::PhotoSize::Large)) {
			img = large->original();
		} else if (const auto thumb = media->image(Data::PhotoSize::Thumbnail)) {
			img = thumb->original();
		} else {
			const auto rawBytes = media->imageBytes(Data::PhotoSize::Large);
			if (!rawBytes.isEmpty()) {
				img = QImage::fromData(rawBytes);
			}
		}
	}
	if (img.isNull()) {
		strong->showToast(QString::fromUtf8("Сначала откройте фото для загрузки"));
		return;
	}

	constexpr int kMaxSide = 1280;
	if (std::max(img.width(), img.height()) > kMaxSide) {
		img = img.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}

	QByteArray jpegBytes;
	{
		QBuffer buf(&jpegBytes);
		buf.open(QIODevice::WriteOnly);
		img.save(&buf, "JPEG", 90);
	}
	if (jpegBytes.isEmpty()) {
		strong->showToast(QString::fromUtf8("Не удалось подготовить изображение"));
		return;
	}

	strong->showToast(QString::fromUtf8("Загрузка фото для поиска..."));

	auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	QHttpPart filePart;
	filePart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		QVariant(u"form-data; name=\"upfile\"; filename=\"image.jpg\""_q));
	filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(u"image/jpeg"_q));
	filePart.setBody(jpegBytes);
	multi->append(filePart);

	QNetworkRequest req(QUrl(
		u"https://yandex.com/images/touch/search?rpt=imageview&format=json&request=%7B%22blocks%22%3A%5B%7B%22block%22%3A%22cbir-uploader__get-cbir-id%22%7D%5D%7D"_q));
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(std::chrono::milliseconds(15000));
	req.setRawHeader("Accept", "application/json");
	req.setRawHeader("X-Requested-With", "XMLHttpRequest");
	req.setRawHeader("Cookie", "gdpr=0");
	req.setRawHeader(
		"User-Agent",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");

	auto *reply = SharedNetwork()->post(req, multi);
	multi->setParent(reply);

	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, weakCtrl, provider] {
		reply->deleteLater();
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = reply->readAll();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
			if (const auto s = weakCtrl.get()) {
				s->showToast(QString::fromUtf8("Не удалось выполнить поиск по картинке"));
			}
			return;
		}
		QJsonParseError err;
		const auto doc = QJsonDocument::fromJson(body, &err);
		if (err.error != QJsonParseError::NoError || !doc.isObject()) {
			if (const auto s = weakCtrl.get()) {
				s->showToast(QString::fromUtf8("Ошибка ответа сервера поиска"));
			}
			return;
		}
		const auto blocks = doc.object().value(u"blocks"_q).toArray();
		if (blocks.isEmpty()) {
			if (const auto s = weakCtrl.get()) {
				s->showToast(QString::fromUtf8("Сервер не вернул результат поиска"));
			}
			return;
		}
		const auto params = blocks.at(0).toObject().value(u"params"_q).toObject();
		const auto cbirId = params.value(u"cbirId"_q).toString();
		const auto imageUrl = params.value(u"originalImageUrl"_q).toString();
		const auto finalUrl = BuildReverseSearchUrl(
			provider,
			reply->url().host(),
			cbirId,
			imageUrl);
		const QUrl target(finalUrl);
		if (cbirId.isEmpty() || imageUrl.isEmpty() || !target.isValid() || target.scheme() != u"https"_q) {
			if (const auto s = weakCtrl.get()) {
				s->showToast(QString::fromUtf8("Не удалось сформировать ссылку поиска"));
			}
			return;
		}
		QDesktopServices::openUrl(target);
	});
}

// ==========================================
// 5. Inline Math, Currency & Unit Evaluator
// ==========================================
class MathParser final {
public:
	explicit MathParser(const QString &expr) : _s(expr) {}

	[[nodiscard]] bool parse(double &out) {
		_pos = 0;
		_hadOpOrCall = false;
		if (!parseExpr(out, 0)) {
			return false;
		}
		skipWs();
		return (_pos == _s.size()) && _hadOpOrCall && std::isfinite(out);
	}

private:
	void skipWs() {
		while (_pos < _s.size() && _s[_pos].isSpace()) {
			++_pos;
		}
	}

	bool parseExpr(double &out, int depth) {
		if (depth > 64 || !parseTerm(out, depth + 1)) {
			return false;
		}
		while (true) {
			skipWs();
			if (_pos >= _s.size()) {
				break;
			}
			const QChar op = _s[_pos];
			if (op != u'+' && op != u'-') {
				break;
			}
			++_pos;
			_hadOpOrCall = true;
			double rhs = 0.0;
			if (!parseTerm(rhs, depth + 1)) {
				return false;
			}
			out = (op == u'+') ? (out + rhs) : (out - rhs);
			if (!std::isfinite(out)) {
				return false;
			}
		}
		return true;
	}

	bool parseTerm(double &out, int depth) {
		if (depth > 64 || !parsePower(out, depth + 1)) {
			return false;
		}
		while (true) {
			skipWs();
			if (_pos >= _s.size()) {
				break;
			}
			const QChar op = _s[_pos];
			if (op != u'*' && op != u'/' && op != u'%') {
				break;
			}
			++_pos;
			_hadOpOrCall = true;
			double rhs = 0.0;
			if (!parsePower(rhs, depth + 1)) {
				return false;
			}
			if (op == u'*') {
				out *= rhs;
			} else if (op == u'/') {
				if (rhs == 0.0) {
					return false;
				}
				out /= rhs;
			} else {
				if (rhs == 0.0) {
					return false;
				}
				out = std::fmod(out, rhs);
			}
			if (!std::isfinite(out)) {
				return false;
			}
		}
		return true;
	}

	bool parsePower(double &out, int depth) {
		if (depth > 64 || !parseUnary(out, depth + 1)) {
			return false;
		}
		skipWs();
		if (_pos < _s.size() && _s[_pos] == u'^') {
			++_pos;
			_hadOpOrCall = true;
			double expVal = 0.0;
			if (!parsePower(expVal, depth + 1)) {
				return false;
			}
			out = std::pow(out, expVal);
			if (!std::isfinite(out)) {
				return false;
			}
		}
		return true;
	}

	bool parseUnary(double &out, int depth) {
		if (depth > 64) {
			return false;
		}
		skipWs();
		if (_pos < _s.size() && (_s[_pos] == u'+' || _s[_pos] == u'-')) {
			const bool neg = (_s[_pos] == u'-');
			++_pos;
			if (!parseUnary(out, depth + 1)) {
				return false;
			}
			if (neg) {
				out = -out;
			}
			return std::isfinite(out);
		}
		return parsePrimary(out, depth + 1);
	}

	bool parsePrimary(double &out, int depth) {
		if (depth > 64) {
			return false;
		}
		skipWs();
		if (_pos >= _s.size()) {
			return false;
		}
		if (_s[_pos] == u'(') {
			++_pos;
			if (!parseExpr(out, depth + 1)) {
				return false;
			}
			skipWs();
			if (_pos >= _s.size() || _s[_pos] != u')') {
				return false;
			}
			++_pos;
			return true;
		}
		if (_s[_pos].isDigit() || _s[_pos] == u'.' || _s[_pos] == u',') {
			const int start = _pos;
			bool hasDigit = false;
			bool hasDot = false;
			while (_pos < _s.size()) {
				const QChar c = _s[_pos];
				if (c.isDigit()) {
					hasDigit = true;
					++_pos;
				} else if ((c == u'.' || c == u',') && !hasDot) {
					hasDot = true;
					++_pos;
				} else {
					break;
				}
			}
			if (!hasDigit) {
				return false;
			}
			auto numStr = _s.mid(start, _pos - start).replace(u',', u'.');
			bool ok = false;
			out = numStr.toDouble(&ok);
			if (!ok || !std::isfinite(out)) {
				return false;
			}
			// Suffix % (e.g. 50% -> 0.5) when not followed by binary digit/paren after ws
			return true;
		}
		if (_s[_pos].isLetter()) {
			const int start = _pos;
			while (_pos < _s.size() && (_s[_pos].isLetterOrNumber() || _s[_pos] == u'_')) {
				++_pos;
			}
			const auto ident = _s.mid(start, _pos - start).toLower();
			skipWs();
			if (_pos < _s.size() && _s[_pos] == u'(') {
				++_pos;
				_hadOpOrCall = true;
				double arg = 0.0;
				if (!parseExpr(arg, depth + 1)) {
					return false;
				}
				skipWs();
				if (_pos >= _s.size() || _s[_pos] != u')') {
					return false;
				}
				++_pos;
				if (ident == u"sqrt"_q) {
					if (arg < 0.0) return false;
					out = std::sqrt(arg);
				} else if (ident == u"abs"_q) {
					out = std::abs(arg);
				} else if (ident == u"sin"_q) {
					out = std::sin(arg);
				} else if (ident == u"cos"_q) {
					out = std::cos(arg);
				} else if (ident == u"tan"_q) {
					out = std::tan(arg);
				} else if (ident == u"log"_q || ident == u"log10"_q) {
					if (arg <= 0.0) return false;
					out = std::log10(arg);
				} else if (ident == u"ln"_q) {
					if (arg <= 0.0) return false;
					out = std::log(arg);
				} else if (ident == u"round"_q) {
					out = std::round(arg);
				} else if (ident == u"floor"_q) {
					out = std::floor(arg);
				} else if (ident == u"ceil"_q) {
					out = std::ceil(arg);
				} else {
					return false;
				}
				return std::isfinite(out);
			}
			if (ident == u"pi"_q) {
				out = 3.14159265358979323846;
				return true;
			}
			if (ident == u"e"_q) {
				out = 2.71828182845904523536;
				return true;
			}
			return false;
		}
		return false;
	}

	const QString &_s;
	int _pos = 0;
	bool _hadOpOrCall = false;
};

[[nodiscard]] QString FormatCalcNumber(double v, int maxDecimals = 6) {
	if (std::abs(v) < 1e-12) {
		return u"0"_q;
	}
	if (std::abs(v) >= 1e12 || (std::abs(v) > 0.0 && std::abs(v) < 1e-6)) {
		return QString::number(v, 'g', 8);
	}
	auto s = QString::number(v, 'f', maxDecimals);
	if (s.contains(u'.')) {
		while (s.endsWith(u'0')) {
			s.chop(1);
		}
		if (s.endsWith(u'.')) {
			s.chop(1);
		}
	}
	return s;
}

[[nodiscard]] QString NormalizeCurrencyCode(const QString &raw) {
	const auto t = raw.trimmed().toLower();
	if (t == u"$"_q || t == u"usd"_q || t == u"usdt"_q || t == u"usdc"_q || t == QString::fromUtf8("доллар") || t == QString::fromUtf8("долларов")) return u"USD"_q;
	if (t == u"€"_q || t == u"eur"_q || t == QString::fromUtf8("евро")) return u"EUR"_q;
	if (t == u"₽"_q || t == u"rub"_q || t == QString::fromUtf8("руб") || t == QString::fromUtf8("рубль") || t == QString::fromUtf8("рублей")) return u"RUB"_q;
	if (t == u"₴"_q || t == u"uah"_q || t == QString::fromUtf8("грн") || t == QString::fromUtf8("гривен")) return u"UAH"_q;
	if (t == u"₸"_q || t == u"kzt"_q || t == QString::fromUtf8("тенге")) return u"KZT"_q;
	if (t == u"byn"_q) return u"BYN"_q;
	if (t == u"£"_q || t == u"gbp"_q) return u"GBP"_q;
	if (t == u"¥"_q || t == u"cny"_q || t == QString::fromUtf8("юань") || t == QString::fromUtf8("юаней")) return u"CNY"_q;
	if (t == u"jpy"_q || t == QString::fromUtf8("иен") || t == QString::fromUtf8("йен")) return u"JPY"_q;
	if (t == u"₺"_q || t == u"try"_q || t == QString::fromUtf8("лир")) return u"TRY"_q;
	const auto up = t.toUpper();
	static const QSet<QString> kSupported = {
		u"AED"_q, u"PLN"_q, u"CHF"_q, u"CAD"_q, u"AUD"_q, u"INR"_q,
		u"KRW"_q, u"BRL"_q, u"GEL"_q, u"UZS"_q, u"AMD"_q, u"AZN"_q,
		u"MDL"_q, u"RSD"_q, u"CZK"_q, u"SEK"_q, u"NOK"_q,
	};
	return kSupported.contains(up) ? up : QString();
}

enum class UnitCategory {
	Length,
	Mass,
	Temp,
	Data,
	Speed,
	Time,
};

struct UnitSpec {
	UnitCategory category = UnitCategory::Length;
	QString label;
	double toBase = 1.0;
};

[[nodiscard]] std::optional<UnitSpec> LookupUnit(const QString &raw) {
	const auto u = raw.trimmed().toLower();
	// Length (base: meter)
	if (u == u"mm"_q || u == QString::fromUtf8("мм")) return UnitSpec{ UnitCategory::Length, QString::fromUtf8("мм"), 0.001 };
	if (u == u"cm"_q || u == QString::fromUtf8("см")) return UnitSpec{ UnitCategory::Length, QString::fromUtf8("см"), 0.01 };
	if (u == u"m"_q || u == QString::fromUtf8("м")) return UnitSpec{ UnitCategory::Length, QString::fromUtf8("м"), 1.0 };
	if (u == u"km"_q || u == QString::fromUtf8("км")) return UnitSpec{ UnitCategory::Length, QString::fromUtf8("км"), 1000.0 };
	if (u == u"in"_q || u == u"inch"_q || u == QString::fromUtf8("дюйм") || u == QString::fromUtf8("дюймов")) return UnitSpec{ UnitCategory::Length, u"in"_q, 0.0254 };
	if (u == u"ft"_q || u == QString::fromUtf8("фут") || u == QString::fromUtf8("футов")) return UnitSpec{ UnitCategory::Length, u"ft"_q, 0.3048 };
	if (u == u"yd"_q) return UnitSpec{ UnitCategory::Length, u"yd"_q, 0.9144 };
	if (u == u"mi"_q || u == QString::fromUtf8("ми") || u == QString::fromUtf8("миля") || u == QString::fromUtf8("мили") || u == QString::fromUtf8("миль")) return UnitSpec{ UnitCategory::Length, u"mi"_q, 1609.344 };
	// Mass (base: gram)
	if (u == u"mg"_q || u == QString::fromUtf8("мг")) return UnitSpec{ UnitCategory::Mass, QString::fromUtf8("мг"), 0.001 };
	if (u == u"g"_q || u == QString::fromUtf8("г")) return UnitSpec{ UnitCategory::Mass, QString::fromUtf8("г"), 1.0 };
	if (u == u"kg"_q || u == QString::fromUtf8("кг")) return UnitSpec{ UnitCategory::Mass, QString::fromUtf8("кг"), 1000.0 };
	if (u == u"t"_q || u == QString::fromUtf8("т")) return UnitSpec{ UnitCategory::Mass, QString::fromUtf8("т"), 1000000.0 };
	if (u == u"oz"_q) return UnitSpec{ UnitCategory::Mass, u"oz"_q, 28.349523125 };
	if (u == u"lb"_q || u == u"lbs"_q || u == QString::fromUtf8("фунт") || u == QString::fromUtf8("фунтов")) return UnitSpec{ UnitCategory::Mass, u"lb"_q, 453.59237 };
	// Temperature (special handling)
	if (u == u"c"_q || u == QString::fromUtf8("°c") || u == QString::fromUtf8("°с")) return UnitSpec{ UnitCategory::Temp, QString::fromUtf8("°C"), 0.0 };
	if (u == u"f"_q || u == QString::fromUtf8("°f")) return UnitSpec{ UnitCategory::Temp, QString::fromUtf8("°F"), 1.0 };
	if (u == u"k"_q || u == QString::fromUtf8("к")) return UnitSpec{ UnitCategory::Temp, u"K"_q, 2.0 };
	// Data (base: bytes, binary 1024)
	if (u == u"b"_q || u == QString::fromUtf8("б")) return UnitSpec{ UnitCategory::Data, QString::fromUtf8("Б"), 1.0 };
	if (u == u"kb"_q || u == QString::fromUtf8("кб")) return UnitSpec{ UnitCategory::Data, QString::fromUtf8("КБ"), 1024.0 };
	if (u == u"mb"_q || u == QString::fromUtf8("мб")) return UnitSpec{ UnitCategory::Data, QString::fromUtf8("МБ"), 1024.0 * 1024.0 };
	if (u == u"gb"_q || u == QString::fromUtf8("гб")) return UnitSpec{ UnitCategory::Data, QString::fromUtf8("ГБ"), 1024.0 * 1024.0 * 1024.0 };
	if (u == u"tb"_q || u == QString::fromUtf8("тб")) return UnitSpec{ UnitCategory::Data, QString::fromUtf8("ТБ"), 1024.0 * 1024.0 * 1024.0 * 1024.0 };
	// Speed (base: m/s)
	if (u == u"m/s"_q || u == QString::fromUtf8("м/с")) return UnitSpec{ UnitCategory::Speed, QString::fromUtf8("м/с"), 1.0 };
	if (u == u"kmh"_q || u == u"km/h"_q || u == QString::fromUtf8("км/ч")) return UnitSpec{ UnitCategory::Speed, QString::fromUtf8("км/ч"), 1.0 / 3.6 };
	if (u == u"mph"_q) return UnitSpec{ UnitCategory::Speed, u"mph"_q, 0.44704 };
	if (u == u"kn"_q) return UnitSpec{ UnitCategory::Speed, u"kn"_q, 0.514444 };
	// Time (base: second)
	if (u == u"ms"_q || u == QString::fromUtf8("мс")) return UnitSpec{ UnitCategory::Time, QString::fromUtf8("мс"), 0.001 };
	if (u == u"s"_q || u == u"sec"_q || u == QString::fromUtf8("с") || u == QString::fromUtf8("сек")) return UnitSpec{ UnitCategory::Time, QString::fromUtf8("с"), 1.0 };
	if (u == u"min"_q || u == QString::fromUtf8("мин")) return UnitSpec{ UnitCategory::Time, QString::fromUtf8("мин"), 60.0 };
	if (u == u"h"_q || u == u"hr"_q || u == QString::fromUtf8("ч") || u == QString::fromUtf8("часов")) return UnitSpec{ UnitCategory::Time, QString::fromUtf8("ч"), 3600.0 };
	if (u == u"d"_q || u == u"day"_q || u == QString::fromUtf8("д") || u == QString::fromUtf8("дней")) return UnitSpec{ UnitCategory::Time, QString::fromUtf8("д"), 86400.0 };
	return std::nullopt;
}

class CurrencyStore final : public QObject {
public:
	static CurrencyStore &Instance() {
		static CurrencyStore s;
		return s;
	}

	[[nodiscard]] bool hasRates() const {
		return !_ratesFromUsd.isEmpty()
			&& _fetchedMs > 0
			&& (QDateTime::currentMSecsSinceEpoch() - _fetchedMs < 6 * 3600 * 1000);
	}

	[[nodiscard]] std::optional<double> convert(double amount, const QString &from, const QString &to) {
		if (!hasRates()) {
			ensureFetching();
			return std::nullopt;
		}
		if (!_ratesFromUsd.contains(from) || !_ratesFromUsd.contains(to)) {
			return std::nullopt;
		}
		const double fromRate = _ratesFromUsd.value(from);
		const double toRate = _ratesFromUsd.value(to);
		if (fromRate <= 0.0 || toRate <= 0.0) {
			return std::nullopt;
		}
		return (amount / fromRate) * toRate;
	}

	void ensureFetching(QPointer<QObject> notifyTarget = {}, std::function<void()> onReady = {}) {
		if (notifyTarget && onReady) {
			_waiters.push_back({ notifyTarget, std::move(onReady) });
		}
		if (_inFlight || hasRates()) {
			return;
		}
		_inFlight = true;
		QNetworkRequest req(QUrl(u"https://open.er-api.com/v6/latest/USD"_q));
		req.setAttribute(
			QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::NoLessSafeRedirectPolicy);
		req.setTransferTimeout(std::chrono::milliseconds(5000));
		auto *reply = SharedNetwork()->get(req);
		QObject::connect(reply, &QNetworkReply::finished, this, [this, reply] {
			_inFlight = false;
			reply->deleteLater();
			const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
				QJsonParseError err;
				const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
				if (err.error == QJsonParseError::NoError && doc.isObject()) {
					const auto ratesObj = doc.object().value(u"rates"_q).toObject();
					if (!ratesObj.isEmpty()) {
						QHash<QString, double> next;
						next.insert(u"USD"_q, 1.0);
						for (auto it = ratesObj.constBegin(); it != ratesObj.constEnd(); ++it) {
							const double val = it.value().toDouble();
							if (val > 0.0 && std::isfinite(val)) {
								next.insert(it.key().toUpper(), val);
							}
						}
						_ratesFromUsd = std::move(next);
						_fetchedMs = QDateTime::currentMSecsSinceEpoch();
					}
				}
			}
			const auto pending = std::move(_waiters);
			_waiters.clear();
			for (const auto &w : pending) {
				if (w.target && w.cb) {
					w.cb();
				}
			}
		});
	}

private:
	struct Waiter {
		QPointer<QObject> target;
		std::function<void()> cb;
	};
	QHash<QString, double> _ratesFromUsd;
	qint64 _fetchedMs = 0;
	bool _inFlight = false;
	std::vector<Waiter> _waiters;
};

struct InlineSuggestion {
	bool ready = false;
	bool loading = false;
	int matchLocalStart = 0;
	int matchLocalEnd = 0;
	QString resultText;
	QString labelText;
};

[[nodiscard]] InlineSuggestion EvaluateLinePrefix(
		const QString &prefix,
		QPointer<QObject> notifyTarget,
		std::function<void()> onRatesReady) {
	InlineSuggestion out;
	if (!prefix.endsWith(u'=') || prefix.size() < 2) {
		return out;
	}
	const int endIdx = prefix.size();
	// Limit scan window to last 120 chars.
	const int minIdx = std::max(0, endIdx - 120);
	const auto window = prefix.mid(minIdx);

	// 1. Check currency or unit conversion suffix ending in '='.
	static const QRegularExpression kConvRe(
		QString::fromUtf8("(?:^|[\\s(\\[])([0-9]+(?:[.,][0-9]+)?)\\s*([A-Za-zА-Яа-яёЁ$€£₽₴₸¥₺°/]+)\\s+(?:to|in|в|->)\\s+([A-Za-zА-Яа-яёЁ$€£₽₴₸¥₺°/]+)=$"),
		QRegularExpression::CaseInsensitiveOption);
	const auto m = kConvRe.match(window);
	if (m.hasMatch()) {
		int capStart = m.capturedStart(1);
		auto numStr = m.captured(1).replace(u',', u'.');
		bool ok = false;
		const double amount = numStr.toDouble(&ok);
		const auto fromRaw = m.captured(2);
		const auto toRaw = m.captured(3);
		if (ok && std::isfinite(amount)) {
			const auto fromCurr = NormalizeCurrencyCode(fromRaw);
			const auto toCurr = NormalizeCurrencyCode(toRaw);
			if (!fromCurr.isEmpty() && !toCurr.isEmpty()) {
				out.matchLocalStart = minIdx + capStart;
				out.matchLocalEnd = endIdx;
				if (!CurrencyStore::Instance().hasRates()) {
					CurrencyStore::Instance().ensureFetching(notifyTarget, std::move(onRatesReady));
					out.loading = true;
					out.labelText = QString::fromUtf8("= загрузка курса...");
					return out;
				}
				if (const auto res = CurrencyStore::Instance().convert(amount, fromCurr, toCurr)) {
					const auto formatted = FormatCalcNumber(*res, (*res >= 100.0) ? 2 : 4);
					out.ready = true;
					out.resultText = QString("%1 %2").arg(formatted, toCurr);
					out.labelText = QString::fromUtf8("= %1  ·  Tab").arg(out.resultText);
					return out;
				}
			}

			const auto fromUnit = LookupUnit(fromRaw);
			const auto toUnit = LookupUnit(toRaw);
			if (fromUnit && toUnit && fromUnit->category == toUnit->category) {
				double converted = 0.0;
				if (fromUnit->category == UnitCategory::Temp) {
					double c = amount;
					if (int(fromUnit->toBase) == 1) {
						c = (amount - 32.0) * (5.0 / 9.0);
					} else if (int(fromUnit->toBase) == 2) {
						c = amount - 273.15;
					}
					if (int(toUnit->toBase) == 0) {
						converted = c;
					} else if (int(toUnit->toBase) == 1) {
						converted = c * (9.0 / 5.0) + 32.0;
					} else {
						converted = c + 273.15;
					}
				} else {
					converted = (amount * fromUnit->toBase) / toUnit->toBase;
				}
				if (std::isfinite(converted)) {
					out.ready = true;
					out.matchLocalStart = minIdx + capStart;
					out.matchLocalEnd = endIdx;
					out.resultText = QString("%1 %2").arg(FormatCalcNumber(converted, 4), toUnit->label);
					out.labelText = QString::fromUtf8("= %1  ·  Tab").arg(out.resultText);
					return out;
				}
			}
		}
	}

	// 2. Check math expression ending with '='.
	int scan = endIdx - 2;
	auto isMathChar = [](QChar c) {
		return c.isDigit()
			|| c == u'.' || c == u','
			|| c == u'+' || c == u'-' || c == u'*' || c == u'/' || c == u'%' || c == u'^'
			|| c == u'(' || c == u')'
			|| c.isSpace()
			|| (c.unicode() < 128 && c.isLetter());
	};
	while (scan >= minIdx && isMathChar(prefix[scan])) {
		--scan;
	}
	int exprStart = scan + 1;
	while (exprStart < endIdx - 1 && prefix[exprStart].isSpace()) {
		++exprStart;
	}
	const auto expr = prefix.mid(exprStart, (endIdx - 1) - exprStart).trimmed();
	if (expr.size() >= 2) {
		double val = 0.0;
		MathParser parser(expr);
		if (parser.parse(val)) {
			out.ready = true;
			out.matchLocalStart = exprStart;
			out.matchLocalEnd = endIdx;
			out.resultText = FormatCalcNumber(val, 6);
			out.labelText = QString::fromUtf8("= %1  ·  Tab").arg(out.resultText);
			return out;
		}
	}
	return out;
}

class CalcPillWidget final : public Ui::RpWidget {
public:
	CalcPillWidget(QWidget *parent, std::function<void()> onClick)
	: Ui::RpWidget(parent)
	, _onClick(std::move(onClick)) {
		setAttribute(Qt::WA_ShowWithoutActivating);
		setFocusPolicy(Qt::NoFocus);
		setCursor(style::cur_pointer);
		setMouseTracking(true);
		hide();
	}

	void setTextAndResize(const QString &text, bool interactive) {
		_text = text;
		_interactive = interactive;
		const int w = st::semiboldFont->width(_text) + 24;
		const int h = std::max(28, st::semiboldFont->height + 12);
		resize(w, h);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing, true);

		const auto bg = M3::Blend(
			st::windowBg->c,
			st::windowBgActive->c,
			_hovered && _interactive ? 0.24f : 0.16f);
		auto border = st::windowBgActive->c;
		border.setAlphaF(_interactive ? 0.55 : 0.28);

		const QRectF r = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
		const qreal rad = r.height() * 0.5;
		p.setPen(QPen(border, 1.2));
		p.setBrush(bg);
		p.drawRoundedRect(r, rad, rad);

		p.setFont(st::semiboldFont);
		p.setPen(_interactive ? st::windowFg->c : st::windowSubTextFg->c);
		p.drawText(rect(), Qt::AlignCenter, _text);
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && _interactive && _onClick) {
			_onClick();
			e->accept();
			return;
		}
		Ui::RpWidget::mousePressEvent(e);
	}

	void enterEventHook(QEnterEvent *e) override {
		_hovered = true;
		update();
		Ui::RpWidget::enterEventHook(e);
	}

	void leaveEventHook(QEvent *e) override {
		_hovered = false;
		update();
		Ui::RpWidget::leaveEventHook(e);
	}

private:
	std::function<void()> _onClick;
	QString _text;
	bool _hovered = false;
	bool _interactive = false;
};

struct CalcState {
	QPointer<CalcPillWidget> pill;
	bool hasResult = false;
	bool suppressed = false;
	int matchAbsStart = 0;
	int matchAbsEnd = 0;
	QString resultText;
	QString lastEvaluatedPrefix;
};

// ==========================================
// 6. Translate Before Send registry & helper
// ==========================================
QHash<const QWidget*, std::function<void(not_null<Ui::PopupMenu*>)>> &TranslateMenuMap() {
	static QHash<const QWidget*, std::function<void(not_null<Ui::PopupMenu*>)>> map;
	return map;
}

void RunFieldTranslation(
		QPointer<Ui::InputField> fieldPtr,
		std::shared_ptr<ChatHelpers::Show> show,
		const QString &langCode,
		const QString &langTitle,
		std::shared_ptr<QPointer<QNetworkReply>> activeReply) {
	if (!fieldPtr) {
		return;
	}
	const auto original = fieldPtr->getLastText();
	if (original.trimmed().isEmpty()) {
		return;
	}
	Config().sendTranslateLang = langCode;
	SaveConfig();

	if (*activeReply) {
		(*activeReply)->abort();
		*activeReply = nullptr;
	}

	if (show) {
		show->showToast(QString::fromUtf8("Перевод на %1...").arg(langTitle));
	}

	QUrl url(u"https://translate.googleapis.com/translate_a/single"_q);
	QUrlQuery query;
	query.addQueryItem(u"client"_q, u"gtx"_q);
	query.addQueryItem(u"sl"_q, u"auto"_q);
	query.addQueryItem(u"tl"_q, langCode);
	query.addQueryItem(u"dt"_q, u"t"_q);
	query.addQueryItem(u"q"_q, original);
	url.setQuery(query);

	QNetworkRequest req(url);
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(std::chrono::milliseconds(8000));
	req.setRawHeader(
		"User-Agent",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");

	auto *reply = SharedNetwork()->get(req);
	*activeReply = reply;

	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, fieldPtr, show, original, langTitle] {
		reply->deleteLater();
		if (reply->error() == QNetworkReply::OperationCanceledError) {
			return;
		}
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
			if (show) {
				show->showToast(QString::fromUtf8("Ошибка перевода (HTTP %1)").arg(status));
			}
			return;
		}
		QJsonParseError err;
		const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
		if (err.error != QJsonParseError::NoError || !doc.isArray()) {
			if (show) {
				show->showToast(QString::fromUtf8("Не удалось разобрать ответ переводчика"));
			}
			return;
		}
		const auto outer = doc.array();
		if (outer.isEmpty() || !outer.at(0).isArray()) {
			if (show) {
				show->showToast(QString::fromUtf8("Пустой перевод"));
			}
			return;
		}
		QString translated;
		for (const auto &segVal : outer.at(0).toArray()) {
			const auto seg = segVal.toArray();
			if (!seg.isEmpty() && seg.at(0).isString()) {
				translated.append(seg.at(0).toString());
			}
		}
		if (translated.isEmpty()) {
			if (show) {
				show->showToast(QString::fromUtf8("Переводчик вернул пустой текст"));
			}
			return;
		}
		if (!fieldPtr || fieldPtr->getLastText() != original) {
			return;
		}
		fieldPtr->setText(translated);
		fieldPtr->setCursorPosition(translated.size());
		if (show) {
			show->showToast(QString::fromUtf8("Переведено (%1)").arg(langTitle));
		}
	});
}

} // namespace

// ==========================================
// Persisted PowerTools Settings
// ==========================================
bool IsInlineCalcEnabled() {
	return Config().inlineCalc;
}

void SetInlineCalcEnabled(bool enabled) {
	Config().inlineCalc = enabled;
	SaveConfig();
}

bool IsTranslateBeforeSendEnabled() {
	return Config().translateBeforeSend;
}

void SetTranslateBeforeSendEnabled(bool enabled) {
	Config().translateBeforeSend = enabled;
	SaveConfig();
}

bool IsZalgoFilterEnabled() {
	return Config().zalgoFilter;
}

void SetZalgoFilterEnabled(bool enabled) {
	Config().zalgoFilter = enabled;
	SaveConfig();
}

// ==========================================
// Public Features Implementation
// ==========================================
void FilterZalgoInPlace(QString &text) {
	if (text.size() < 4) {
		return;
	}
	int run = 0;
	ushort baseChar = 0;
	for (int i = 0; i < text.size(); ++i) {
		const QChar ch = text.at(i);
		const auto cat = ch.category();
		if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing) {
			++run;
			if (run > MaxMarksForBase(baseChar)) {
				text[i] = QChar(0x2060);
			}
		} else {
			run = 0;
			baseChar = ch.unicode();
		}
	}
}

void ShowMessageDetailsBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto weakCtrl = base::make_weak(controller);
	std::vector<std::pair<QString, QString>> rows;
	const auto push = [&](const QString &k, const QString &v) {
		if (!v.trimmed().isEmpty()) {
			rows.emplace_back(k, v);
		}
	};

	const auto msgId = item->fullId().msg.bare;
	push(QString::fromUtf8("ID сообщения"), QString::number(msgId));
	if (const auto gid = item->groupId().value; gid != 0) {
		push(QString::fromUtf8("ID альбома / группы"), QString::number(gid));
	}

	const auto sentDt = QDateTime::fromSecsSinceEpoch(qint64(item->date()));
	push(QString::fromUtf8("Отправлено"), sentDt.toString(u"dd.MM.yyyy HH:mm:ss"_q));

	if (const auto edited = item->Get<HistoryMessageEdited>(); edited && edited->date > 0) {
		const auto edDt = QDateTime::fromSecsSinceEpoch(qint64(edited->date));
		push(QString::fromUtf8("Отредактировано"), edDt.toString(u"dd.MM.yyyy HH:mm:ss"_q));
	}

	push(QString::fromUtf8("Отправитель"), PeerSummary(item->from()));
	push(QString::fromUtf8("Чат / Диалог"), PeerSummary(item->history()->peer));

	if (const auto fwd = item->Get<HistoryMessageForwarded>()) {
		QString fwdText;
		if (fwd->originalSender) {
			fwdText = PeerSummary(fwd->originalSender);
		} else if (fwd->originalHiddenSenderInfo) {
			fwdText = fwd->originalHiddenSenderInfo->name;
		} else if (!fwd->originalPostAuthor.isEmpty()) {
			fwdText = fwd->originalPostAuthor;
		}
		if (fwd->originalDate > 0) {
			const auto fwdDt = QDateTime::fromSecsSinceEpoch(qint64(fwd->originalDate));
			if (!fwdText.isEmpty()) fwdText += u" · "_q;
			fwdText += fwdDt.toString(u"dd.MM.yyyy HH:mm:ss"_q);
		}
		if (fwd->originalId.bare > 0) {
			if (!fwdText.isEmpty()) fwdText += u" · "_q;
			fwdText += u"msgId: "_q + QString::number(fwd->originalId.bare);
		}
		push(QString::fromUtf8("Переслано из"), fwdText);
	}

	const auto &origText = item->originalText();
	if (!origText.text.isEmpty()) {
		const auto words = origText.text.split(
			QRegularExpression(u"\\s+"_q),
			Qt::SkipEmptyParts).size();
		QSet<DocumentId> customEmojiIds;
		for (const auto &ent : origText.entities) {
			if (ent.type() == EntityType::CustomEmoji) {
				customEmojiIds.insert(Data::ParseCustomEmojiData(ent.data()));
			}
		}
		auto textStats = QString::fromUtf8("%1 симв. · %2 слов · %3 сущн.")
			.arg(origText.text.size())
			.arg(words)
			.arg(origText.entities.size());
		if (!customEmojiIds.isEmpty()) {
			textStats += QString::fromUtf8(" · %1 кастом. эмодзи").arg(customEmojiIds.size());
		}
		push(QString::fromUtf8("Текст"), textStats);
	}

	if (const auto media = item->media()) {
		if (const auto photo = media->photo()) {
			QStringList info;
			info.append(QString::fromUtf8("ID: %1").arg(photo->id));
			const auto dc = int32(photo->mediaKey().first & 0xFFFFFFFFULL);
			if (dc > 0) {
				info.append(QString::fromUtf8("DC %1").arg(dc));
			}
			if (photo->width() > 0 && photo->height() > 0) {
				info.append(QString("%1x%2").arg(photo->width()).arg(photo->height()));
			}
			if (const auto byteSz = photo->imageByteSize(Data::PhotoSize::Large); byteSz > 0) {
				info.append(Ui::FormatSizeText(byteSz));
			}
			if (photo->date() > 0) {
				info.append(QDateTime::fromSecsSinceEpoch(qint64(photo->date()))
					.toString(u"dd.MM.yyyy HH:mm:ss"_q));
			}
			if (const auto pv = photo->activeMediaView()) {
				const auto fp = InspectJpegBytes(pv->imageBytes(Data::PhotoSize::Large));
				if (!fp.isEmpty()) {
					info.append(fp);
				}
			}
			push(QString::fromUtf8("Медиа (Фото)"), info.join(QString::fromUtf8(" · ")));
		} else if (const auto doc = media->document()) {
			QStringList info;
			info.append(QString::fromUtf8("ID: %1").arg(doc->id));
			const auto dc = int32(doc->mediaKey().first & 0xFFFFFFFFULL);
			if (dc > 0) {
				info.append(QString::fromUtf8("DC %1").arg(dc));
			}
			if (const auto fname = doc->filename(); !fname.isEmpty()) {
				info.append(fname);
			}
			if (const auto mime = doc->mimeString(); !mime.isEmpty()) {
				info.append(mime);
			}
			if (doc->size > 0) {
				info.append(Ui::FormatSizeText(doc->size));
			}
			if (doc->dimensions.isValid() && !doc->dimensions.isEmpty()) {
				info.append(QString("%1x%2").arg(doc->dimensions.width()).arg(doc->dimensions.height()));
			}
			if (const auto dur = doc->duration(); dur > 0) {
				info.append(Ui::FormatDurationText(dur / 1000));
			}
			push(QString::fromUtf8("Медиа (Файл)"), info.join(QString::fromUtf8(" · ")));

			if (const auto fp = doc->filepath(); !fp.isEmpty()) {
				push(QString::fromUtf8("Локальный путь"), fp);
				if (fp.endsWith(u".jpg"_q, Qt::CaseInsensitive)
					|| fp.endsWith(u".jpeg"_q, Qt::CaseInsensitive)
					|| doc->mimeString().contains(u"jpeg"_q, Qt::CaseInsensitive)) {
					QFile f(fp);
					if (f.open(QIODevice::ReadOnly)) {
						const auto head = f.read(256 * 1024);
						const auto jpegInfo = InspectJpegBytes(head);
						if (!jpegInfo.isEmpty()) {
							push(QString::fromUtf8("JPEG метаданные"), jpegInfo);
						}
					}
				}
			}
		}
	}

	QString fullDump;
	for (const auto &[k, v] : rows) {
		fullDump += k + u": "_q + v + u"\n"_q;
	}

	controller->show(Box([rows = std::move(rows), fullDump, weakCtrl](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Детали сообщения")));
		box->setWidth(st::boxWideWidth);
		const auto container = box->verticalLayout();
		for (const auto &[k, v] : rows) {
			AddDetailCopyRow(container, weakCtrl, k, v);
		}
		box->addButton(rpl::single(QString::fromUtf8("Копировать всё")), [=] {
			TextUtilities::SetClipboardText(TextForMimeData::Simple(fullDump.trimmed()));
			if (const auto s = weakCtrl.get()) {
				s->showToast(QString::fromUtf8("Все детали скопированы"));
			}
		});
		box->addButton(rpl::single(QString::fromUtf8("Закрыть")), [=] {
			box->closeBox();
		});
	}));
}

void AddReverseImageSearchMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo,
		not_null<Window::SessionController*> controller) {
	const auto weakCtrl = base::make_weak(controller);
	const auto photoId = photo->id;
	const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
	addAction({
		.text = QString::fromUtf8("Найти похожее"),
		.handler = nullptr,
		.icon = &st::menuIconSearch,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> sub) {
			sub->addAction(QString::fromUtf8("Google Lens"), [=] {
				StartReverseImageSearch(weakCtrl, photoId, ImageSearchProvider::Google);
			}, &st::menuIconSearch);
			sub->addAction(QString::fromUtf8("Яндекс Картинки"), [=] {
				StartReverseImageSearch(weakCtrl, photoId, ImageSearchProvider::Yandex);
			}, &st::menuIconSearch);
			sub->addAction(QString::fromUtf8("Bing Visual Search"), [=] {
				StartReverseImageSearch(weakCtrl, photoId, ImageSearchProvider::Bing);
			}, &st::menuIconSearch);
			sub->addAction(QString::fromUtf8("TinEye"), [=] {
				StartReverseImageSearch(weakCtrl, photoId, ImageSearchProvider::TinEye);
			}, &st::menuIconSearch);
		},
	});
}

void AttachComposeInlineCalc(
		not_null<Ui::RpWidget*> host,
		not_null<Ui::InputField*> field) {
	if (host->property("zen_inline_calc_attached").toBool()) {
		return;
	}
	host->setProperty("zen_inline_calc_attached", true);

	const auto state = host->lifetime().make_state<CalcState>();
	const auto hostPtr = QPointer<Ui::RpWidget>(host.get());
	const auto fieldPtr = QPointer<Ui::InputField>(field.get());

	const auto hidePill = [=] {
		state->hasResult = false;
		if (state->pill) {
			state->pill->hide();
		}
	};

	const auto applySuggestion = [=](bool keepExpr) {
		if (!fieldPtr || !state->hasResult) {
			return;
		}
		auto cursor = fieldPtr->textCursor();
		if (keepExpr) {
			cursor.setPosition(state->matchAbsEnd);
			cursor.insertText(u" "_q + state->resultText);
		} else {
			cursor.setPosition(state->matchAbsStart);
			cursor.setPosition(state->matchAbsEnd, QTextCursor::KeepAnchor);
			cursor.insertText(state->resultText);
		}
		fieldPtr->setTextCursor(cursor);
		hidePill();
	};

	const auto ensurePill = [=]() -> CalcPillWidget* {
		if (!hostPtr) {
			return nullptr;
		}
		if (!state->pill) {
			QWidget *parent = hostPtr->parentWidget() ? hostPtr->parentWidget() : hostPtr.data();
			state->pill = new CalcPillWidget(parent, [=] {
				applySuggestion(false);
			});
		}
		return state->pill.data();
	};

	host->lifetime().add([state] {
		if (state->pill) {
			state->pill->deleteLater();
		}
	});

	const auto positionPill = [=] {
		if (!hostPtr || !state->pill || !state->pill->isVisible()) {
			return;
		}
		QWidget *parent = state->pill->parentWidget();
		if (!parent) {
			return;
		}
		const QPoint anchor = hostPtr->mapTo(parent, QPoint(16, 0));
		const int maxX = std::max(8, parent->width() - state->pill->width() - 12);
		const int x = std::clamp(anchor.x(), 8, maxX);
		const int y = std::max(8, anchor.y() - state->pill->height() - 6);
		state->pill->move(x, y);
		state->pill->raise();
	};

	const auto evaluateSelf = std::make_shared<std::function<void()>>();
	const auto weakEval = std::weak_ptr<std::function<void()>>(evaluateSelf);
	host->lifetime().add([evaluateSelf] {});

	*evaluateSelf = [=] {
		if (!hostPtr || !fieldPtr) {
			hidePill();
			return;
		}
		if (!IsInlineCalcEnabled() || !fieldPtr->hasFocus()) {
			hidePill();
			return;
		}
		const auto cursor = fieldPtr->textCursor();
		if (cursor.hasSelection()) {
			hidePill();
			return;
		}
		const auto block = cursor.block();
		const int blockStart = block.position();
		const int posInBlock = cursor.positionInBlock();
		const auto prefix = block.text().left(posInBlock);

		if (prefix != state->lastEvaluatedPrefix) {
			state->lastEvaluatedPrefix = prefix;
			state->suppressed = false;
		}
		if (state->suppressed) {
			hidePill();
			return;
		}

		const auto res = EvaluateLinePrefix(
			prefix,
			QPointer<QObject>(hostPtr.data()),
			[weakEval, hostPtr] {
				if (const auto fn = weakEval.lock(); hostPtr && fn && *fn) {
					(*fn)();
				}
			});
		if (!res.ready && !res.loading) {
			hidePill();
			return;
		}

		state->hasResult = res.ready;
		state->matchAbsStart = blockStart + res.matchLocalStart;
		state->matchAbsEnd = blockStart + res.matchLocalEnd;
		state->resultText = res.resultText;

		if (auto *pill = ensurePill()) {
			pill->setTextAndResize(res.labelText, res.ready);
			pill->show();
			positionPill();
		}
	};

	field->changes() | rpl::on_next([=] {
		if (const auto fn = weakEval.lock(); fn && *fn) {
			(*fn)();
		}
	}, host->lifetime());

	field->focusedChanges() | rpl::on_next([=](bool focused) {
		if (!focused) {
			hidePill();
		} else if (const auto fn = weakEval.lock(); fn && *fn) {
			(*fn)();
		}
	}, host->lifetime());

	QObject::connect(
		field->rawTextEdit(),
		&QTextEdit::cursorPositionChanged,
		host.get(),
		[=] {
			if (const auto fn = weakEval.lock(); fn && *fn) {
				(*fn)();
			}
		});

	host->geometryValue() | rpl::on_next([=](QRect) {
		positionPill();
	}, host->lifetime());

	base::install_event_filter(host.get(), field->rawTextEdit(), [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto *k = static_cast<QKeyEvent*>(e.get());
		if (state->hasResult && k->key() == Qt::Key_Tab
			&& (k->modifiers() == Qt::NoModifier || k->modifiers() == Qt::ShiftModifier)) {
			applySuggestion(k->modifiers() == Qt::ShiftModifier);
			return base::EventFilterResult::Cancel;
		}
		if (state->pill && state->pill->isVisible() && k->key() == Qt::Key_Escape) {
			state->suppressed = true;
			hidePill();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void AttachComposeTranslateMenu(
		not_null<QWidget*> sendButton,
		not_null<Ui::InputField*> field,
		std::shared_ptr<ChatHelpers::Show> show) {
	const auto key = sendButton.get();
	const auto fieldPtr = QPointer<Ui::InputField>(field.get());
	const auto activeReply = std::make_shared<QPointer<QNetworkReply>>();

	QObject::connect(key, &QObject::destroyed, [key, activeReply] {
		TranslateMenuMap().remove(key);
		if (*activeReply) {
			(*activeReply)->abort();
		}
	});

	TranslateMenuMap().insert(key, [fieldPtr, show, activeReply](not_null<Ui::PopupMenu*> menu) {
		if (!IsTranslateBeforeSendEnabled()
			|| !fieldPtr
			|| fieldPtr->getLastText().trimmed().isEmpty()) {
			return;
		}

		struct LangItem {
			const char *code;
			const char *title;
		};
		static constexpr LangItem kLangs[] = {
			{ "en", "Английский (EN)" },
			{ "ru", "Русский (RU)" },
			{ "uk", "Украинский (UK)" },
			{ "de", "Немецкий (DE)" },
			{ "fr", "Французский (FR)" },
			{ "es", "Испанский (ES)" },
			{ "it", "Итальянский (IT)" },
			{ "pl", "Польский (PL)" },
			{ "tr", "Турецкий (TR)" },
			{ "zh-CN", "Китайский (ZH)" },
			{ "ja", "Японский (JA)" },
		};

		const auto savedCode = Config().sendTranslateLang;
		QString quickTitle = QString::fromUtf8("Английский (EN)");
		for (const auto &l : kLangs) {
			if (savedCode == QLatin1String(l.code)) {
				quickTitle = QString::fromUtf8(l.title);
				break;
			}
		}

		menu->addSeparator();
		menu->addAction(
			QString::fromUtf8("Перевести: %1").arg(quickTitle),
			[fieldPtr, show, savedCode, quickTitle, activeReply] {
				RunFieldTranslation(fieldPtr, show, savedCode, quickTitle, activeReply);
			},
			&st::menuIconTranslate);

		const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
		addAction({
			.text = QString::fromUtf8("Перевести на..."),
			.handler = nullptr,
			.icon = &st::menuIconTranslate,
			.fillSubmenu = [=](not_null<Ui::PopupMenu*> sub) {
				for (const auto &l : kLangs) {
					const auto code = QString::fromLatin1(l.code);
					const auto title = QString::fromUtf8(l.title);
					sub->addAction(title, [fieldPtr, show, code, title, activeReply] {
						RunFieldTranslation(fieldPtr, show, code, title, activeReply);
					});
				}
			},
		});
	});
}

void AppendSendMenuExtra(
		not_null<QWidget*> sendButton,
		not_null<Ui::PopupMenu*> menu) {
	if (!IsTranslateBeforeSendEnabled()) {
		return;
	}
	const auto it = TranslateMenuMap().constFind(sendButton.get());
	if (it != TranslateMenuMap().constEnd() && *it) {
		(*it)(menu);
	}
}

} // namespace Relay
