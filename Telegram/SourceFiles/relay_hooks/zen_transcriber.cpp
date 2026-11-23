/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_transcriber.h"
#include "relay_hooks/relay_hooks.h"
#include "core/application.h"
#include "logs.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
} // extern "C"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHttpMultiPart>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Relay {
namespace {

bool DecodeToWav16kMono(const QString &inputPath, const QString &wavPath) {
	AVFormatContext *fmt = nullptr;
	const auto inUtf8 = inputPath.toUtf8();
	if (avformat_open_input(&fmt, inUtf8.constData(), nullptr, nullptr) < 0) {
		return false;
	}
	if (avformat_find_stream_info(fmt, nullptr) < 0) {
		avformat_close_input(&fmt);
		return false;
	}

	const AVCodec *dec = nullptr;
	const int streamIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	if (streamIdx < 0 || !dec) {
		avformat_close_input(&fmt);
		return false;
	}

	AVCodecContext *ctx = avcodec_alloc_context3(dec);
	if (!ctx) {
		avformat_close_input(&fmt);
		return false;
	}
	if (avcodec_parameters_to_context(ctx, fmt->streams[streamIdx]->codecpar) < 0
		|| avcodec_open2(ctx, dec, nullptr) < 0) {
		avcodec_free_context(&ctx);
		avformat_close_input(&fmt);
		return false;
	}

	AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
	SwrContext *swr = nullptr;
	if (swr_alloc_set_opts2(
			&swr,
			&outLayout,
			AV_SAMPLE_FMT_S16,
			16000,
			&ctx->ch_layout,
			ctx->sample_fmt,
			ctx->sample_rate,
			0,
			nullptr) < 0
		|| swr_init(swr) < 0) {
		if (swr) {
			swr_free(&swr);
		}
		avcodec_free_context(&ctx);
		avformat_close_input(&fmt);
		return false;
	}

	QByteArray pcm;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	auto drainFrame = [&](AVFrame *f) {
		const int maxOut = swr_get_out_samples(swr, f ? f->nb_samples : 0);
		if (maxOut <= 0) {
			return;
		}
		QByteArray buf(maxOut * 2, Qt::Uninitialized);
		uint8_t *outPtr[1] = { reinterpret_cast<uint8_t*>(buf.data()) };
		const int outSamples = swr_convert(
			swr,
			outPtr,
			maxOut,
			f ? const_cast<const uint8_t**>(f->extended_data) : nullptr,
			f ? f->nb_samples : 0);
		if (outSamples > 0) {
			pcm.append(buf.constData(), outSamples * 2);
		}
	};

	while (av_read_frame(fmt, pkt) >= 0) {
		if (pkt->stream_index == streamIdx) {
			if (avcodec_send_packet(ctx, pkt) >= 0) {
				while (avcodec_receive_frame(ctx, frame) >= 0) {
					drainFrame(frame);
					av_frame_unref(frame);
				}
			}
		}
		av_packet_unref(pkt);
	}
	avcodec_send_packet(ctx, nullptr);
	while (avcodec_receive_frame(ctx, frame) >= 0) {
		drainFrame(frame);
		av_frame_unref(frame);
	}
	drainFrame(nullptr);

	av_frame_free(&frame);
	av_packet_free(&pkt);
	swr_free(&swr);
	avcodec_free_context(&ctx);
	avformat_close_input(&fmt);

	if (pcm.isEmpty()) {
		return false;
	}

	QFile outFile(wavPath);
	if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const quint32 dataSize = quint32(pcm.size());
	const quint32 riffSize = 36 + dataSize;
	const quint32 fmtSize = 16;
	const quint16 audioFmt = 1;
	const quint16 channels = 1;
	const quint32 sampleRate = 16000;
	const quint32 byteRate = 32000;
	const quint16 blockAlign = 2;
	const quint16 bitsPerSample = 16;

	QByteArray hdr;
	hdr.reserve(44);
	hdr.append("RIFF", 4);
	hdr.append(reinterpret_cast<const char*>(&riffSize), 4);
	hdr.append("WAVE", 4);
	hdr.append("fmt ", 4);
	hdr.append(reinterpret_cast<const char*>(&fmtSize), 4);
	hdr.append(reinterpret_cast<const char*>(&audioFmt), 2);
	hdr.append(reinterpret_cast<const char*>(&channels), 2);
	hdr.append(reinterpret_cast<const char*>(&sampleRate), 4);
	hdr.append(reinterpret_cast<const char*>(&byteRate), 4);
	hdr.append(reinterpret_cast<const char*>(&blockAlign), 2);
	hdr.append(reinterpret_cast<const char*>(&bitsPerSample), 2);
	hdr.append("data", 4);
	hdr.append(reinterpret_cast<const char*>(&dataSize), 4);
	outFile.write(hdr);
	outFile.write(pcm);
	outFile.close();
	return true;
}

void TranscribeViaApi(const QString &filePath, const QString &apiUrl, const QString &apiKey, ZenTranscriber::Callback done) {
	auto *file = new QFile(filePath);
	if (!file->open(QIODevice::ReadOnly)) {
		delete file;
		done(false, QString::fromUtf8("Не удалось открыть аудиофайл."));
		return;
	}
	auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	file->setParent(multi);

	QHttpPart filePart;
	filePart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		QString("form-data; name=\"file\"; filename=\"%1\"").arg(QFileInfo(filePath).fileName()));
	filePart.setBodyDevice(file);
	multi->append(filePart);

	QHttpPart modelPart;
	modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"model\""));
	modelPart.setBody(apiUrl.contains("groq.com") ? "whisper-large-v3-turbo" : "whisper-1");
	multi->append(modelPart);

	auto *net = new QNetworkAccessManager();
	QNetworkRequest req((QUrl(apiUrl)));
	if (!apiKey.isEmpty()) {
		req.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());
	}
	auto *reply = net->post(req, multi);
	multi->setParent(reply);

	QObject::connect(reply, &QNetworkReply::finished, [reply, net, done = std::move(done)] {
		reply->deleteLater();
		net->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			done(false, QString::fromUtf8("Ошибка API: %1").arg(reply->errorString()));
			return;
		}
		const auto doc = QJsonDocument::fromJson(reply->readAll());
		const auto text = doc.isObject() ? doc.object()["text"].toString().trimmed() : QString();
		if (text.isEmpty()) {
			done(false, QString::fromUtf8("Пустой ответ от сервера распознавания."));
			return;
		}
		done(true, text);
	});
}

} // namespace

QString ZenTranscriber::WhisperDir() {
	return QDir(cWorkingDir()).filePath("tdata/whisper");
}

QString ZenTranscriber::WhisperExePath() {
	const auto inData = QDir(WhisperDir()).filePath("whisper-cli.exe");
	if (QFileInfo::exists(inData)) {
		return inData;
	}
	return QDir(QCoreApplication::applicationDirPath()).filePath("whisper-cli.exe");
}

QString ZenTranscriber::WhisperModelPath() {
	const auto dir = QDir(WhisperDir());
	for (const auto &name : { "ggml-base.bin", "ggml-tiny.bin", "ggml-small.bin" }) {
		const auto p = dir.filePath(name);
		if (QFileInfo::exists(p)) {
			return p;
		}
	}
	return dir.filePath("ggml-tiny.bin");
}

bool ZenTranscriber::IsLocalWhisperReady() {
	return QFileInfo::exists(WhisperExePath()) && QFileInfo::exists(WhisperModelPath());
}

void ZenTranscriber::TranscribeFile(const QString &filePath, Callback done) {
	if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
		done(false, QString::fromUtf8("Файл ещё не загружен. Дождитесь скачивания и повторите."));
		return;
	}

	const auto apiUrl = RelayHooks::Instance().whisperApiUrl();
	if (!apiUrl.isEmpty()) {
		TranscribeViaApi(filePath, apiUrl, RelayHooks::Instance().whisperApiKey(), std::move(done));
		return;
	}

	if (!IsLocalWhisperReady()) {
		done(false, QString::fromUtf8("Локальный Whisper не найден. Скачайте модель в Настройках ZenDesktop (или укажите API URL)."));
		return;
	}

	const auto exePath = WhisperExePath();
	const auto modelPath = WhisperModelPath();
	const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const auto wavPath = QDir(tempDir).filePath(
		QString("zen_transcribe_%1.wav").arg(QCoreApplication::applicationPid()));

	auto *thread = QThread::create([filePath, wavPath, exePath, modelPath, done = std::move(done)]() mutable {
		if (!DecodeToWav16kMono(filePath, wavPath)) {
			QMetaObject::invokeMethod(QCoreApplication::instance(), [done = std::move(done)] {
				done(false, QString::fromUtf8("Не удалось декодировать аудиодорожку."));
			}, Qt::QueuedConnection);
			return;
		}

		QProcess proc;
		proc.start(exePath, { "-m", modelPath, "-l", "auto", "-nt", "-np", "-f", wavPath });
		proc.waitForFinished(60000);
		QFile::remove(wavPath);

		const auto out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
		const auto ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && !out.isEmpty());
		QMetaObject::invokeMethod(QCoreApplication::instance(), [done = std::move(done), ok, out] {
			done(ok, ok ? out : QString::fromUtf8("Не удалось распознать речь."));
		}, Qt::QueuedConnection);
	});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

} // namespace Relay
