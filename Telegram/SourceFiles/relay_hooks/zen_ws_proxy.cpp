/*
This file is part of Telegram Desktop fork - ZenDesktop with RelayHooks.
*/
#include "relay_hooks/zen_ws_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "logs.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QtEndian>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Relay {
namespace {

constexpr auto kHandshakeLen = 64;
constexpr auto kSkipLen = 8;
constexpr auto kPrekeyLen = 32;
constexpr auto kIvLen = 16;
constexpr auto kProtoTagPos = 56;
constexpr auto kDcIdxPos = 60;
constexpr auto kMaxHandshakeDrain = 64 * 1024;
constexpr auto kMaxUpgradeResp = 16 * 1024;
constexpr auto kWsPort = 443;

constexpr uint32_t kProtoAbridged = 0xEFEFEFEFU;
constexpr uint32_t kProtoIntermediate = 0xEEEEEEEEU;
constexpr uint32_t kProtoSecure = 0xDDDDDDDDU;

[[nodiscard]] uint32_t ReadLE32(const char *p) {
	const auto b = reinterpret_cast<const uint8_t*>(p);
	return uint32_t(b[0])
		| (uint32_t(b[1]) << 8)
		| (uint32_t(b[2]) << 16)
		| (uint32_t(b[3]) << 24);
}

[[nodiscard]] int16_t ReadLE16S(const char *p) {
	const auto b = reinterpret_cast<const uint8_t*>(p);
	return static_cast<int16_t>(uint16_t(b[0]) | (uint16_t(b[1]) << 8));
}

[[nodiscard]] QByteArray ReverseBytes(const QByteArray &in) {
	QByteArray out(in.size(), Qt::Uninitialized);
	for (int i = 0; i < in.size(); ++i) {
		out[i] = in[in.size() - 1 - i];
	}
	return out;
}

[[nodiscard]] QByteArray RandomBytes(int count) {
	QByteArray out(count, Qt::Uninitialized);
	auto *gen = QRandomGenerator::system();
	for (int i = 0; i < count; i += 4) {
		const quint32 v = gen->generate();
		const int chunk = std::min(4, count - i);
		for (int j = 0; j < chunk; ++j) {
			out[i + j] = static_cast<char>((v >> (8 * j)) & 0xFF);
		}
	}
	return out;
}

[[nodiscard]] bool IsValidRelayPrefix(const QByteArray &r) {
	if (r.size() < 8) {
		return false;
	}
	const auto first = static_cast<uint8_t>(r[0]);
	if (first == 0xEF) {
		return false;
	}
	const auto s4 = r.left(4);
	if (s4 == QByteArrayLiteral("HEAD")
		|| s4 == QByteArrayLiteral("POST")
		|| s4 == QByteArrayLiteral("GET ")
		|| s4 == QByteArrayLiteral("OPTI")
		|| s4 == QByteArray("\xee\xee\xee\xee", 4)
		|| s4 == QByteArray("\xdd\xdd\xdd\xdd", 4)
		|| s4 == QByteArray("\x16\x03\x01\x02", 4)) {
		return false;
	}
	if (r.mid(4, 4) == QByteArray(4, '\0')) {
		return false;
	}
	return true;
}

class AesCtr final {
public:
	AesCtr() = default;
	~AesCtr() { reset(); }

	AesCtr(const AesCtr &) = delete;
	AesCtr &operator=(const AesCtr &) = delete;

	bool init(const QByteArray &key, const QByteArray &iv) {
		reset();
		if (key.size() != kPrekeyLen || iv.size() != kIvLen) {
			return false;
		}
		_ctx = EVP_CIPHER_CTX_new();
		if (!_ctx) {
			return false;
		}
		if (EVP_EncryptInit_ex(
				_ctx,
				EVP_aes_256_ctr(),
				nullptr,
				reinterpret_cast<const unsigned char*>(key.constData()),
				reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
			reset();
			return false;
		}
		return true;
	}

	QByteArray process(const QByteArray &data) {
		if (!_ctx || data.isEmpty()) {
			return {};
		}
		QByteArray out(data.size(), Qt::Uninitialized);
		int outLen = 0;
		if (EVP_EncryptUpdate(
				_ctx,
				reinterpret_cast<unsigned char*>(out.data()),
				&outLen,
				reinterpret_cast<const unsigned char*>(data.constData()),
				data.size()) != 1) {
			return {};
		}
		out.resize(outLen);
		return out;
	}

	void reset() {
		if (_ctx) {
			EVP_CIPHER_CTX_free(_ctx);
			_ctx = nullptr;
		}
	}

private:
	EVP_CIPHER_CTX *_ctx = nullptr;
};

// Splits outbound MTProto transport stream into discrete messages so each
// becomes its own WebSocket binary frame (matches Flowseal/tg-ws-proxy).
class MsgSplitter final {
public:
	explicit MsgSplitter(uint32_t proto) : _proto(proto) {}

	std::vector<QByteArray> split(const QByteArray &cipher, const QByteArray &plain) {
		std::vector<QByteArray> parts;
		if (cipher.isEmpty()) {
			return parts;
		}
		if (_disabled) {
			parts.push_back(cipher);
			return parts;
		}
		_cipherBuf.append(cipher);
		_plainBuf.append(plain);

		int offset = 0;
		const int total = _cipherBuf.size();
		while (offset < total) {
			const int pktLen = nextPacketLen(offset, total - offset);
			if (pktLen < 0) {
				break;
			}
			if (pktLen == 0) {
				parts.push_back((offset == 0) ? _cipherBuf : _cipherBuf.mid(offset));
				offset = total;
				_disabled = true;
				break;
			}
			parts.push_back(_cipherBuf.mid(offset, pktLen));
			offset += pktLen;
		}
		if (offset > 0) {
			_cipherBuf.remove(0, offset);
			_plainBuf.remove(0, offset);
		}
		return parts;
	}

private:
	int nextPacketLen(int offset, int avail) const {
		if (avail <= 0) {
			return -1;
		}
		if (_proto == kProtoAbridged) {
			const auto first = static_cast<uint8_t>(_plainBuf[offset]);
			int payloadLen = 0;
			int headerLen = 1;
			if (first == 0x7F || first == 0xFF) {
				if (avail < 4) {
					return -1;
				}
				const auto b1 = static_cast<uint8_t>(_plainBuf[offset + 1]);
				const auto b2 = static_cast<uint8_t>(_plainBuf[offset + 2]);
				const auto b3 = static_cast<uint8_t>(_plainBuf[offset + 3]);
				payloadLen = (int(b1) | (int(b2) << 8) | (int(b3) << 16)) * 4;
				headerLen = 4;
			} else {
				payloadLen = int(first & 0x7F) * 4;
			}
			if (payloadLen <= 0) {
				return 0;
			}
			const int pktLen = headerLen + payloadLen;
			return (avail < pktLen) ? -1 : pktLen;
		}
		if (_proto == kProtoIntermediate || _proto == kProtoSecure) {
			if (avail < 4) {
				return -1;
			}
			const uint32_t rawLen = ReadLE32(_plainBuf.constData() + offset) & 0x7FFFFFFFU;
			if (rawLen == 0 || rawLen > 64 * 1024 * 1024U) {
				return 0;
			}
			const int pktLen = 4 + int(rawLen);
			return (avail < pktLen) ? -1 : pktLen;
		}
		return 0;
	}

	uint32_t _proto = 0;
	QByteArray _cipherBuf;
	QByteArray _plainBuf;
	bool _disabled = false;
};

class WsProxySession final : public QObject {
public:
	WsProxySession(
		QTcpSocket *client,
		const QByteArray &secret,
		QObject *parent)
	: QObject(parent)
	, _client(client)
	, _secret(secret) {
		_client->setParent(this);
		_client->setSocketOption(QAbstractSocket::LowDelayOption, 1);

		connect(_client, &QTcpSocket::readyRead, this, [this] {
			onClientReadyRead();
		});
		connect(_client, &QTcpSocket::disconnected, this, [this] {
			closeSession();
		});
		connect(_client, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
			closeSession();
		});
	}

private:
	enum class State {
		WaitHandshake,
		ConnectingUpstream,
		WaitWsUpgrade,
		Bridging,
		Closed,
	};

	void onClientReadyRead() {
		if (_state == State::Closed) {
			return;
		}
		const auto chunk = _client->readAll();
		if (chunk.isEmpty()) {
			return;
		}
		if (_state == State::WaitHandshake) {
			_clientPending.append(chunk);
			if (_clientPending.size() > kMaxHandshakeDrain) {
				closeSession();
				return;
			}
			if (_clientPending.size() >= kHandshakeLen) {
				const auto handshake = _clientPending.left(kHandshakeLen);
				_clientPending.remove(0, kHandshakeLen);
				if (!processHandshake(handshake)) {
					closeSession();
				}
			}
			return;
		}
		if (_state == State::ConnectingUpstream || _state == State::WaitWsUpgrade) {
			_clientPending.append(chunk);
			return;
		}
		if (_state == State::Bridging) {
			forwardClientData(chunk);
		}
	}

	bool processHandshake(const QByteArray &handshake) {
		const auto decPrekeyIv = handshake.mid(kSkipLen, kPrekeyLen + kIvLen);
		const auto decKey = QCryptographicHash::hash(
			decPrekeyIv.left(kPrekeyLen) + _secret,
			QCryptographicHash::Sha256);
		const auto decIv = decPrekeyIv.mid(kPrekeyLen, kIvLen);

		AesCtr tempDec;
		if (!tempDec.init(decKey, decIv)) {
			return false;
		}
		const auto decrypted = tempDec.process(handshake);
		if (decrypted.size() != kHandshakeLen) {
			return false;
		}

		const auto proto = ReadLE32(decrypted.constData() + kProtoTagPos);
		if (proto != kProtoAbridged
			&& proto != kProtoIntermediate
			&& proto != kProtoSecure) {
			return false;
		}

		const auto dcIdx = ReadLE16S(decrypted.constData() + kDcIdxPos);
		const int absDc = std::abs(int(dcIdx));
		if (absDc < 1 || (absDc > 5 && absDc != 203)) {
			return false;
		}
		const bool isMedia = (dcIdx < 0);
		const int hostDc = (absDc == 203) ? 2 : absDc;

		_splitter = std::make_unique<MsgSplitter>(proto);

		if (!_cltDec.init(decKey, decIv)) {
			return false;
		}
		_cltDec.process(QByteArray(kHandshakeLen, '\0'));

		const auto revClt = ReverseBytes(decPrekeyIv);
		const auto cltEncKey = QCryptographicHash::hash(
			revClt.left(kPrekeyLen) + _secret,
			QCryptographicHash::Sha256);
		if (!_cltEnc.init(cltEncKey, revClt.mid(kPrekeyLen, kIvLen))) {
			return false;
		}

		// Generate relay obfuscated2 init frame (standard, without secret hash).
		QByteArray rnd;
		do {
			rnd = RandomBytes(kHandshakeLen);
		} while (!IsValidRelayPrefix(rnd));

		const auto tgEncKey = rnd.mid(kSkipLen, kPrekeyLen);
		const auto tgEncIv = rnd.mid(kSkipLen + kPrekeyLen, kIvLen);
		if (!_tgEnc.init(tgEncKey, tgEncIv)) {
			return false;
		}
		const auto keystream = _tgEnc.process(QByteArray(kHandshakeLen, '\0'));
		if (keystream.size() != kHandshakeLen) {
			return false;
		}

		QByteArray tailPlain(8, Qt::Uninitialized);
		tailPlain[0] = decrypted[kProtoTagPos];
		tailPlain[1] = decrypted[kProtoTagPos + 1];
		tailPlain[2] = decrypted[kProtoTagPos + 2];
		tailPlain[3] = decrypted[kProtoTagPos + 3];
		tailPlain[4] = decrypted[kDcIdxPos];
		tailPlain[5] = decrypted[kDcIdxPos + 1];
		const auto pad = RandomBytes(2);
		tailPlain[6] = pad[0];
		tailPlain[7] = pad[1];

		_relayInit = rnd;
		for (int i = 0; i < 8; ++i) {
			_relayInit[kProtoTagPos + i] = static_cast<char>(
				static_cast<uint8_t>(tailPlain[i])
				^ static_cast<uint8_t>(keystream[kProtoTagPos + i]));
		}

		const auto revRelay = ReverseBytes(_relayInit.mid(kSkipLen, kPrekeyLen + kIvLen));
		if (!_tgDec.init(revRelay.left(kPrekeyLen), revRelay.mid(kPrekeyLen, kIvLen))) {
			return false;
		}

		if (isMedia) {
			_domains.push_back(QString("kws%1-1.web.telegram.org").arg(hostDc));
		}
		_domains.push_back(QString("kws%1.web.telegram.org").arg(hostDc));

		tryNextDomain();
		return true;
	}

	void tryNextDomain() {
		if (_state == State::Closed) {
			return;
		}
		if (_domainIndex >= int(_domains.size())) {
			closeSession();
			return;
		}

		if (_upstream) {
			_upstream->disconnect(this);
			_upstream->abort();
			_upstream->deleteLater();
			_upstream = nullptr;
		}

		_state = State::ConnectingUpstream;
		_upstreamBuf.clear();
		const auto host = _domains[_domainIndex++];

		_upstream = new QSslSocket(this);
		_upstream->setProxy(QNetworkProxy::NoProxy);
		_upstream->setSocketOption(QAbstractSocket::LowDelayOption, 1);

		connect(_upstream, &QSslSocket::encrypted, this, [this, host] {
			if (_state != State::ConnectingUpstream) {
				return;
			}
			_state = State::WaitWsUpgrade;
			const auto wsKey = RandomBytes(16).toBase64();
			const auto req = QByteArray(
				"GET /apiws HTTP/1.1\r\n"
				"Host: " + host.toUtf8() + "\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Key: " + wsKey + "\r\n"
				"Sec-WebSocket-Version: 13\r\n"
				"Sec-WebSocket-Protocol: binary\r\n"
				"\r\n");
			_upstream->write(req);
		});

		connect(_upstream, &QSslSocket::readyRead, this, [this] {
			onUpstreamReadyRead();
		});

		connect(_upstream, &QSslSocket::disconnected, this, [this] {
			if (_state == State::ConnectingUpstream || _state == State::WaitWsUpgrade) {
				if (_domainIndex < int(_domains.size())) {
					tryNextDomain();
					return;
				}
			}
			closeSession();
		});

		connect(_upstream, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
			if (_state == State::ConnectingUpstream || _state == State::WaitWsUpgrade) {
				if (_domainIndex < int(_domains.size())) {
					tryNextDomain();
					return;
				}
			}
			closeSession();
		});

		_upstream->connectToHostEncrypted(host, kWsPort);
	}

	void onUpstreamReadyRead() {
		if (_state == State::Closed || !_upstream) {
			return;
		}
		_upstreamBuf.append(_upstream->readAll());

		if (_state == State::WaitWsUpgrade) {
			const int hdrEnd = _upstreamBuf.indexOf("\r\n\r\n");
			if (hdrEnd < 0) {
				if (_upstreamBuf.size() > kMaxUpgradeResp) {
					if (_domainIndex < int(_domains.size())) {
						tryNextDomain();
					} else {
						closeSession();
					}
				}
				return;
			}
			const auto header = _upstreamBuf.left(hdrEnd);
			_upstreamBuf.remove(0, hdrEnd + 4);
			const int firstCrLf = header.indexOf("\r\n");
			const auto statusLine = (firstCrLf >= 0) ? header.left(firstCrLf) : header;
			if (!statusLine.contains(" 101 ")) {
				if (_domainIndex < int(_domains.size())) {
					tryNextDomain();
				} else {
					closeSession();
				}
				return;
			}

			_state = State::Bridging;
			sendWsFrame(0x02, _relayInit);
			if (!_clientPending.isEmpty()) {
				const auto pending = _clientPending;
				_clientPending.clear();
				forwardClientData(pending);
			}
		}

		if (_state == State::Bridging) {
			drainUpstreamWsFrames();
		}
	}

	void forwardClientData(const QByteArray &data) {
		if (_state != State::Bridging || data.isEmpty() || !_splitter) {
			return;
		}
		const auto plain = _cltDec.process(data);
		const auto cipher = _tgEnc.process(plain);
		const auto parts = _splitter->split(cipher, plain);
		for (const auto &part : parts) {
			sendWsFrame(0x02, part);
		}
	}

	void drainUpstreamWsFrames() {
		while (_state == State::Bridging && _upstreamBuf.size() >= 2) {
			const auto b0 = static_cast<uint8_t>(_upstreamBuf[0]);
			const auto b1 = static_cast<uint8_t>(_upstreamBuf[1]);
			const uint8_t opcode = b0 & 0x0F;
			const bool masked = (b1 & 0x80) != 0;
			uint64_t payloadLen = b1 & 0x7F;
			int hdrLen = 2;

			if (payloadLen == 126) {
				if (_upstreamBuf.size() < 4) {
					return;
				}
				payloadLen = qFromBigEndian<quint16>(
					reinterpret_cast<const uchar*>(_upstreamBuf.constData() + 2));
				hdrLen = 4;
			} else if (payloadLen == 127) {
				if (_upstreamBuf.size() < 10) {
					return;
				}
				payloadLen = qFromBigEndian<quint64>(
					reinterpret_cast<const uchar*>(_upstreamBuf.constData() + 2));
				hdrLen = 10;
			}
			if (masked) {
				hdrLen += 4;
			}
			if (payloadLen > 16ULL * 1024 * 1024) {
				closeSession();
				return;
			}
			const int totalLen = hdrLen + int(payloadLen);
			if (_upstreamBuf.size() < totalLen) {
				return;
			}

			QByteArray payload = _upstreamBuf.mid(hdrLen, int(payloadLen));
			if (masked) {
				const char *mask = _upstreamBuf.constData() + (hdrLen - 4);
				for (int i = 0; i < payload.size(); ++i) {
					payload[i] = static_cast<char>(payload[i] ^ mask[i & 3]);
				}
			}
			_upstreamBuf.remove(0, totalLen);

			if (opcode == 0x00 || opcode == 0x01 || opcode == 0x02) {
				if (!payload.isEmpty() && _client) {
					const auto plain = _tgDec.process(payload);
					const auto out = _cltEnc.process(plain);
					_client->write(out);
				}
			} else if (opcode == 0x09) {
				sendWsFrame(0x0A, payload);
			} else if (opcode == 0x08) {
				closeSession();
				return;
			}
		}
	}

	void sendWsFrame(uint8_t opcode, const QByteArray &payload) {
		if (!_upstream || _state == State::Closed) {
			return;
		}
		QByteArray frame;
		const int len = payload.size();
		frame.reserve(len + 14);
		frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));
		if (len < 126) {
			frame.append(static_cast<char>(0x80 | len));
		} else if (len < 65536) {
			frame.append(static_cast<char>(0x80 | 126));
			frame.append(static_cast<char>((len >> 8) & 0xFF));
			frame.append(static_cast<char>(len & 0xFF));
		} else {
			frame.append(static_cast<char>(0x80 | 127));
			const uint64_t len64 = static_cast<uint64_t>(len);
			for (int shift = 56; shift >= 0; shift -= 8) {
				frame.append(static_cast<char>((len64 >> shift) & 0xFF));
			}
		}
		const auto mask = RandomBytes(4);
		frame.append(mask);
		for (int i = 0; i < len; ++i) {
			frame.append(static_cast<char>(payload[i] ^ mask[i & 3]));
		}
		_upstream->write(frame);
	}

	void closeSession() {
		if (_state == State::Closed) {
			return;
		}
		_state = State::Closed;
		if (_client) {
			_client->disconnect(this);
			_client->close();
		}
		if (_upstream) {
			_upstream->disconnect(this);
			_upstream->abort();
		}
		deleteLater();
	}

	QTcpSocket *_client = nullptr;
	QSslSocket *_upstream = nullptr;
	QByteArray _secret;
	QByteArray _clientPending;
	QByteArray _upstreamBuf;
	QByteArray _relayInit;
	std::vector<QString> _domains;
	int _domainIndex = 0;
	State _state = State::WaitHandshake;

	AesCtr _cltDec;
	AesCtr _cltEnc;
	AesCtr _tgEnc;
	AesCtr _tgDec;
	std::unique_ptr<MsgSplitter> _splitter;
};

} // namespace

ZenWsProxy &ZenWsProxy::Instance() {
	static ZenWsProxy instance;
	return instance;
}

ZenWsProxy::~ZenWsProxy() {
	stop();
}

bool ZenWsProxy::start(uint16_t preferredPort) {
	if (_server && _server->isListening()) {
		return true;
	}

	_secret = RandomBytes(16);
	_secretHex = QString::fromLatin1(_secret.toHex());

	_server = std::make_unique<QTcpServer>();
	QObject::connect(_server.get(), &QTcpServer::newConnection, _server.get(), [this] {
		handleNewConnection();
	});

	if (!_server->listen(QHostAddress::LocalHost, preferredPort)) {
		if (!_server->listen(QHostAddress::LocalHost, 0)) {
			LOG(("ZenWsProxy: Failed to listen on localhost: %1").arg(_server->errorString()));
			_server.reset();
			_port = 0;
			return false;
		}
	}

	_port = _server->serverPort();
	LOG(("ZenWsProxy: Listening on 127.0.0.1:%1").arg(_port));
	return true;
}

void ZenWsProxy::stop() {
	if (_server) {
		_server->close();
		_server.reset();
	}
	_port = 0;
}

bool ZenWsProxy::isRunning() const {
	return _server && _server->isListening();
}

uint16_t ZenWsProxy::port() const {
	return _port;
}

QString ZenWsProxy::secretHex() const {
	return _secretHex;
}

void ZenWsProxy::handleNewConnection() {
	if (!_server) {
		return;
	}
	while (_server->hasPendingConnections()) {
		if (const auto client = _server->nextPendingConnection()) {
			new WsProxySession(client, _secret, _server.get());
		}
	}
}

void ZenWsProxy::applyEnabled(bool enabled) {
	if (enabled) {
		if (!start(1443)) {
			return;
		}
		MTP::ProxyData proxy;
		proxy.type = MTP::ProxyData::Type::Mtproto;
		proxy.host = u"127.0.0.1"_q;
		proxy.port = _port;
		proxy.password = _secretHex;
		Core::App().setCurrentProxy(proxy, MTP::ProxyData::Settings::Enabled);
	} else {
		const auto prevPort = _port;
		stop();
		const auto &proxySettings = Core::App().settings().proxy();
		const auto selected = proxySettings.selected();
		if (proxySettings.settings() == MTP::ProxyData::Settings::Enabled
			&& selected.type == MTP::ProxyData::Type::Mtproto
			&& selected.host == u"127.0.0.1"_q
			&& (prevPort == 0 || selected.port == prevPort || selected.port == 1443)) {
			Core::App().setCurrentProxy(
				MTP::ProxyData(),
				MTP::ProxyData::Settings::Disabled);
		}
	}
}

} // namespace Relay
