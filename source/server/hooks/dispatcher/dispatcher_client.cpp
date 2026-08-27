/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "dispatcher_client.h"
#include "server/api/t3d_console.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace
{
	constexpr std::uint32_t kMaxFrameBytes  = 64 * 1024;
	constexpr int           kRecvTimeoutMs  = 5000;
	constexpr int           kSendTimeoutMs  = 5000;
	constexpr int           kPingIntervalMs = 30000;
	constexpr int           kBackoffMinMs   = 1000;
	constexpr int           kBackoffMaxMs   = 30000;
	constexpr std::size_t   kSendQueueMax   = 256;
	constexpr int           kSelectPollMs   = 100;

	std::atomic<bool> g_wsaInitialized{false};

	// Send queue: pre-framed JSON bodies (without length prefix). The
	// session thread drains and writes. Producers are any thread calling
	// SendTo. The session thread also pushes its own pings here so the
	// drain path is the single writer to the socket.
	std::mutex              g_qMu;
	std::deque<std::string> g_queue;
	std::atomic<bool>       g_sessionUp{false};

	// Delivery callback. Default is a Con::Echo stub. Replaced by
	// SetDeliveryCallback. Read by session thread only; writes are guarded
	// by the same mutex.
	std::mutex                              g_cbMu;
	Hooks::Dispatcher::DeliveryCallback     g_cb;

	// Pending resolve callbacks keyed by world_sector_id. When the
	// daemon replies with sector_resolved/sector_unknown for that id,
	// we pop the callback and invoke it. The daemon orders replies per
	// connection, but we don't require strict FIFO — keying by id lets
	// multiple in-flight resolves coexist.
	std::mutex                                                                     g_resolveMu;
	std::unordered_map<std::uint32_t, Hooks::Dispatcher::ResolveCallback>          g_resolveCbs;

	bool ensureWsa()
	{
		if (g_wsaInitialized.load(std::memory_order_acquire)) return true;
		WSADATA wsa;
		const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (rc != 0)
		{
			Con::Warning("[dispatcher] WSAStartup failed (rc=%d)", rc);
			return false;
		}
		g_wsaInitialized.store(true, std::memory_order_release);
		return true;
	}

	std::string deterministicUuid(std::uint32_t world_id)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "00000000-0000-0000-0000-%012X",
		              static_cast<unsigned>(world_id));
		return buf;
	}

	bool sendAll(SOCKET s, const void* data, std::size_t len)
	{
		auto* p = static_cast<const char*>(data);
		while (len > 0)
		{
			const int n = ::send(s, p, static_cast<int>(len), 0);
			if (n <= 0) return false;
			p   += n;
			len -= static_cast<std::size_t>(n);
		}
		return true;
	}

	bool recvAll(SOCKET s, void* data, std::size_t len)
	{
		auto* p = static_cast<char*>(data);
		while (len > 0)
		{
			const int n = ::recv(s, p, static_cast<int>(len), 0);
			if (n <= 0) return false;
			p   += n;
			len -= static_cast<std::size_t>(n);
		}
		return true;
	}

	bool sendFrame(SOCKET s, const std::string& body)
	{
		const std::uint32_t lenLE = static_cast<std::uint32_t>(body.size());
		if (!sendAll(s, &lenLE, sizeof(lenLE))) return false;
		return sendAll(s, body.data(), body.size());
	}

	bool recvFrame(SOCKET s, std::string& out)
	{
		std::uint32_t len = 0;
		if (!recvAll(s, &len, sizeof(len))) return false;
		if (len == 0 || len > kMaxFrameBytes) return false;
		out.resize(len);
		return recvAll(s, out.data(), len);
	}

	// Crude flat-JSON string field extractor: finds `"key":"value"` and
	// returns value with simple \" and \\ unescaping. Returns false if
	// the key isn't present. Sufficient for the daemon's flat frames;
	// not a general JSON parser.
	// Extract a non-negative integer field `"key": <digits>`. Returns
	// false if the key isn't present or the value isn't an unsigned
	// decimal. Sufficient for our flat numeric fields (world_sector_id).
	bool extractU32Field(const std::string& body, const char* key, std::uint32_t& out)
	{
		std::string needle = std::string("\"") + key + "\":";
		const auto k = body.find(needle);
		if (k == std::string::npos) return false;
		std::size_t i = k + needle.size();
		while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
		std::uint64_t acc = 0;
		bool any = false;
		while (i < body.size() && body[i] >= '0' && body[i] <= '9')
		{
			acc = acc * 10 + std::uint64_t(body[i] - '0');
			if (acc > 0xFFFFFFFFull) return false;
			any = true;
			++i;
		}
		if (!any) return false;
		out = static_cast<std::uint32_t>(acc);
		return true;
	}

	bool extractStringField(const std::string& body, const char* key, std::string& out)
	{
		std::string needle = std::string("\"") + key + "\":\"";
		const auto k = body.find(needle);
		if (k == std::string::npos) return false;
		std::size_t i = k + needle.size();
		out.clear();
		while (i < body.size())
		{
			char c = body[i];
			if (c == '\\' && i + 1 < body.size())
			{
				char n = body[i + 1];
				out.push_back(n == 'n' ? '\n' : n == 't' ? '\t' : n);
				i += 2;
				continue;
			}
			if (c == '"') return true;
			out.push_back(c);
			++i;
		}
		return false;
	}

	int b64DecodeChar(char c)
	{
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	}

	bool b64Decode(const std::string& in, std::vector<std::uint8_t>& out)
	{
		out.clear();
		out.reserve((in.size() / 4) * 3);
		int buf = 0, bits = 0;
		for (char c : in)
		{
			if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
			int v = b64DecodeChar(c);
			if (v < 0) return false;
			buf = (buf << 6) | v;
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
			}
		}
		return true;
	}

	void b64Encode(const std::vector<std::uint8_t>& in, std::string& out)
	{
		static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		out.clear();
		out.reserve(((in.size() + 2) / 3) * 4);
		std::size_t i = 0;
		while (i + 3 <= in.size())
		{
			std::uint32_t n = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
			out.push_back(tbl[(n >> 18) & 0x3F]);
			out.push_back(tbl[(n >> 12) & 0x3F]);
			out.push_back(tbl[(n >>  6) & 0x3F]);
			out.push_back(tbl[ n        & 0x3F]);
			i += 3;
		}
		if (i < in.size())
		{
			std::uint32_t n = in[i] << 16;
			if (i + 1 < in.size()) n |= in[i+1] << 8;
			out.push_back(tbl[(n >> 18) & 0x3F]);
			out.push_back(tbl[(n >> 12) & 0x3F]);
			out.push_back(i + 1 < in.size() ? tbl[(n >> 6) & 0x3F] : '=');
			out.push_back('=');
		}
	}

	SOCKET connectOnce(const Hooks::Dispatcher::Config& cfg)
	{
		SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			Con::Warning("[dispatcher] socket() failed (WSAGetLastError=%d)", ::WSAGetLastError());
			return INVALID_SOCKET;
		}
		::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&kRecvTimeoutMs), sizeof(kRecvTimeoutMs));
		::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&kSendTimeoutMs), sizeof(kSendTimeoutMs));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(cfg.port);
		if (::inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) != 1)
		{
			addrinfo hints{};
			hints.ai_family   = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* res = nullptr;
			if (::getaddrinfo(cfg.host.c_str(), nullptr, &hints, &res) != 0 || !res)
			{
				Con::Warning("[dispatcher] getaddrinfo(%s) failed", cfg.host.c_str());
				::closesocket(sock);
				return INVALID_SOCKET;
			}
			addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
			::freeaddrinfo(res);
		}

		if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
		{
			Con::Warning("[dispatcher] connect %s:%u failed (WSAGetLastError=%d)",
			             cfg.host.c_str(), cfg.port, ::WSAGetLastError());
			::closesocket(sock);
			return INVALID_SOCKET;
		}
		return sock;
	}

	std::uint64_t nowMs()
	{
		using namespace std::chrono;
		return static_cast<std::uint64_t>(
			duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
	}

	void dispatchFrame(const std::string& body)
	{
		// Crude type sniff — find the "type" field.
		std::string ty;
		if (!extractStringField(body, "type", ty))
		{
			Con::Warning("[dispatcher] frame missing type: %s", body.c_str());
			return;
		}

		if (ty == "pong")
		{
			// Silent — would spam every 30s.
			return;
		}
		if (ty == "forward_error")
		{
			std::string reason, tgt;
			extractStringField(body, "reason", reason);
			extractStringField(body, "target_peer_id", tgt);
			Con::Warning("[dispatcher] forward_error reason=%s target=%s",
			             reason.c_str(), tgt.c_str());
			return;
		}
		if (ty == "delivery")
		{
			std::string from, payload_b64;
			extractStringField(body, "from_peer_id", from);
			extractStringField(body, "payload_b64", payload_b64);
			std::vector<std::uint8_t> payload;
			if (!b64Decode(payload_b64, payload))
			{
				Con::Warning("[dispatcher] delivery from=%s: bad base64", from.c_str());
				return;
			}
			Hooks::Dispatcher::DeliveryCallback cb;
			{
				std::lock_guard<std::mutex> lk(g_cbMu);
				cb = g_cb;
			}
			if (cb) cb(from, payload);
			else    Con::Echo("[dispatcher] delivery from=%s bytes=%zu (no callback)",
			                  from.c_str(), payload.size());
			return;
		}

		if (ty == "sector_claimed")
		{
			std::uint32_t sid = 0;
			std::string peer;
			extractU32Field   (body, "world_sector_id", sid);
			extractStringField(body, "peer_id",         peer);
			Con::Echo("[dispatcher] sector_claimed sector=%u peer=%s",
			          (unsigned)sid, peer.c_str());
			return;
		}
		if (ty == "sector_resolved" || ty == "sector_unknown")
		{
			const bool known = (ty == "sector_resolved");
			std::uint32_t sid = 0;
			std::string peer;
			extractU32Field   (body, "world_sector_id", sid);
			if (known) extractStringField(body, "peer_id", peer);

			Hooks::Dispatcher::ResolveCallback cb;
			{
				std::lock_guard<std::mutex> lk(g_resolveMu);
				auto it = g_resolveCbs.find(sid);
				if (it != g_resolveCbs.end())
				{
					cb = std::move(it->second);
					g_resolveCbs.erase(it);
				}
			}
			if (cb)
			{
				Hooks::Dispatcher::ResolveResult r;
				r.world_sector_id = sid;
				r.known           = known;
				r.peer_id         = peer;
				cb(r);
			}
			else
			{
				Con::Echo("[dispatcher] resolve reply sector=%u known=%d peer=%s (no waiter)",
				          (unsigned)sid, (int)known, peer.c_str());
			}
			return;
		}

		Con::Warning("[dispatcher] unknown frame type=%s", ty.c_str());
	}

	// Returns: 1 = readable, 0 = timeout, -1 = error.
	int waitReadable(SOCKET s, int timeoutMs)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
		return ::select(0, &rfds, nullptr, nullptr, &tv);
	}

	// One session = connect, hello/hello_ack, then a unified loop:
	//   - drain send queue
	//   - emit ping if interval elapsed
	//   - select(100ms); on readable, recvFrame + dispatch
	// Returns when the session ends. The bool out-param is true iff at
	// least one ping round-trip succeeded — caller uses it to reset backoff.
	void runSession(const Hooks::Dispatcher::Config& cfg, bool& had_successful_ping)
	{
		had_successful_ping = false;
		SOCKET sock = connectOnce(cfg);
		if (sock == INVALID_SOCKET) return;

		const std::string uuid = deterministicUuid(cfg.world_id);
		char helloBuf[256];
		const int n = std::snprintf(helloBuf, sizeof(helloBuf),
			"{\"type\":\"hello\",\"world_id\":%u,\"server_uuid\":\"%s\",\"proto_version\":1}",
			static_cast<unsigned>(cfg.world_id), uuid.c_str());
		if (n <= 0 || n >= static_cast<int>(sizeof(helloBuf)))
		{
			Con::Warning("[dispatcher] hello body overflow");
			::closesocket(sock);
			return;
		}
		if (!sendFrame(sock, std::string(helloBuf, helloBuf + n)))
		{
			Con::Warning("[dispatcher] send hello failed (WSAGetLastError=%d)", ::WSAGetLastError());
			::closesocket(sock);
			return;
		}
		std::string ack;
		if (!recvFrame(sock, ack))
		{
			Con::Warning("[dispatcher] recv hello_ack failed (WSAGetLastError=%d)", ::WSAGetLastError());
			::closesocket(sock);
			return;
		}
		Con::Echo("[dispatcher] session up — hello_ack: %s", ack.c_str());
		g_sessionUp.store(true);

		// Auto-claim configured sectors. Enqueue each as a separate
		// frame; replies will be logged by dispatchFrame as they come
		// back. Goes via the normal queue so the writer thread serialises
		// these with any concurrent SendTo from other threads.
		for (std::uint32_t sid : cfg.sector_claims)
		{
			char buf[96];
			const int sn = std::snprintf(buf, sizeof(buf),
				"{\"type\":\"claim_sector\",\"world_sector_id\":%u}",
				static_cast<unsigned>(sid));
			if (sn > 0 && sn < static_cast<int>(sizeof(buf)))
			{
				std::lock_guard<std::mutex> lk(g_qMu);
				if (g_queue.size() < kSendQueueMax)
					g_queue.emplace_back(buf, buf + sn);
			}
		}
		if (!cfg.sector_claims.empty())
			Con::Echo("[dispatcher] queued %zu sector claim(s)", cfg.sector_claims.size());

		std::uint64_t last_ping_ms = nowMs();
		bool fatal = false;
		while (!fatal)
		{
			// 1. Drain send queue under lock, then write outside.
			std::deque<std::string> batch;
			{
				std::lock_guard<std::mutex> lk(g_qMu);
				batch.swap(g_queue);
			}
			while (!batch.empty())
			{
				if (!sendFrame(sock, batch.front()))
				{
					Con::Warning("[dispatcher] queued send failed (WSAGetLastError=%d)", ::WSAGetLastError());
					fatal = true;
					break;
				}
				batch.pop_front();
			}
			if (fatal) break;

			// 2. Ping if interval elapsed.
			if (nowMs() - last_ping_ms >= static_cast<std::uint64_t>(kPingIntervalMs))
			{
				char pingBuf[64];
				const int pn = std::snprintf(pingBuf, sizeof(pingBuf),
					"{\"type\":\"ping\",\"ts\":%llu}",
					static_cast<unsigned long long>(nowMs()));
				if (pn > 0 && pn < static_cast<int>(sizeof(pingBuf)))
				{
					if (!sendFrame(sock, std::string(pingBuf, pingBuf + pn)))
					{
						Con::Warning("[dispatcher] ping send failed (WSAGetLastError=%d)", ::WSAGetLastError());
						break;
					}
					last_ping_ms = nowMs();
				}
			}

			// 3. Wait readable, then read at most one frame per loop turn.
			int sr = waitReadable(sock, kSelectPollMs);
			if (sr < 0)
			{
				Con::Warning("[dispatcher] select failed (WSAGetLastError=%d)", ::WSAGetLastError());
				break;
			}
			if (sr == 0) continue;

			std::string frame;
			if (!recvFrame(sock, frame))
			{
				Con::Warning("[dispatcher] recv frame failed (WSAGetLastError=%d)", ::WSAGetLastError());
				break;
			}
			// Treat first inbound frame past hello as proof of round-trip
			// (pong, delivery, or forward_error all qualify).
			had_successful_ping = true;
			dispatchFrame(frame);
		}
		g_sessionUp.store(false);
		::closesocket(sock);
	}
}

void Hooks::Dispatcher::SetDeliveryCallback(DeliveryCallback cb)
{
	std::lock_guard<std::mutex> lk(g_cbMu);
	g_cb = std::move(cb);
}

bool Hooks::Dispatcher::SendTo(const std::string& target_peer_id,
                               const std::vector<std::uint8_t>& payload)
{
	if (!g_sessionUp.load()) return false;

	std::string b64;
	b64Encode(payload, b64);

	// Build the frame JSON. target_peer_id is treated as opaque text; we
	// don't escape it because daemon-issued UUIDs are [0-9a-f-] only.
	std::string body;
	body.reserve(80 + target_peer_id.size() + b64.size());
	body  = "{\"type\":\"forward\",\"target_peer_id\":\"";
	body += target_peer_id;
	body += "\",\"payload_b64\":\"";
	body += b64;
	body += "\"}";

	std::lock_guard<std::mutex> lk(g_qMu);
	if (g_queue.size() >= kSendQueueMax)
	{
		Con::Warning("[dispatcher] SendTo: queue full (%zu), dropping", g_queue.size());
		return false;
	}
	g_queue.push_back(std::move(body));
	return true;
}

bool Hooks::Dispatcher::ResolveSector(std::uint32_t world_sector_id, ResolveCallback cb)
{
	if (!g_sessionUp.load()) return false;

	{
		std::lock_guard<std::mutex> lk(g_resolveMu);
		// If a prior resolve for the same sector is in flight, the
		// new caller replaces it — most-recent-wins. Acceptable for
		// the test workload; revisit if real edge-trigger paths grow
		// concurrent resolves of the same id.
		g_resolveCbs[world_sector_id] = std::move(cb);
	}

	char buf[96];
	const int n = std::snprintf(buf, sizeof(buf),
		"{\"type\":\"resolve_sector\",\"world_sector_id\":%u}",
		static_cast<unsigned>(world_sector_id));
	if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return false;

	std::lock_guard<std::mutex> lk(g_qMu);
	if (g_queue.size() >= kSendQueueMax)
	{
		Con::Warning("[dispatcher] ResolveSector: queue full, dropping");
		// Roll back the callback registration so it doesn't dangle.
		std::lock_guard<std::mutex> rlk(g_resolveMu);
		g_resolveCbs.erase(world_sector_id);
		return false;
	}
	g_queue.emplace_back(buf, buf + n);
	return true;
}

void Hooks::Dispatcher::RunForever(const Config& cfg)
{
	if (!ensureWsa()) return;

	int backoffMs = kBackoffMinMs;
	for (;;)
	{
		bool succeeded = false;
		runSession(cfg, succeeded);
		if (succeeded) backoffMs = kBackoffMinMs;
		Con::Echo("[dispatcher] session ended, reconnecting in %d ms", backoffMs);
		std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
		// Windows headers define a min() macro; (std::min) defeats it.
		backoffMs = (std::min)(backoffMs * 2, kBackoffMaxMs);
	}
}

void Hooks::Dispatcher::SpawnConnect(const Config& cfg)
{
	std::thread([cfg] {
		Hooks::Dispatcher::RunForever(cfg);
	}).detach();
}
