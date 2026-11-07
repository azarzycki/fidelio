// cspot-ios3: command-line Spotify Connect receiver for ARMv6 iPhone OS 3.x.
// Based on feelfreelinux/cspot targets/cli/main.cpp (1b07a9c), using the squeezelite-esp32
// cspot fork (ee1720c). Zeroconf discovery via the system dns_sd (bell's Apple MDNSService),
// HTTP login endpoint via bell's civetweb server, audio via AudioQueue.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "BellHTTPServer.h"
#include "BellLogger.h"
#include "BellUtils.h"  // for BELL_SLEEP_MS
#include "CSpotContext.h"
#include "CliPlayer.h"
#include "Logger.h"
#include "LoginBlob.h"
#include "MDNSService.h"
#include "MercurySession.h"
#include "SpircHandler.h"
#include "StatusWriter.h"
#include "TrackPlayer.h"
#include "TrackQueue.h"
#include "WrappedSemaphore.h"
#include "civetweb.h"
#include "ios3_trace.h"

static std::string readFile(const std::string& path) {
  std::string out;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

static bool writeFile(const std::string& path, const std::string& data) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return true;
}

struct Options {
  std::string username, password, credentials, name = "iPhone Fidelio";
  std::string clientId, clientSecret;
  int bitrate = 96;  // kindest to a 412 MHz ARM11
  int port = 7864;
  bool help = false;
  bool noAudio = false;
  AudioQueueSink::Options audio;  // --audio-route, --on-route-change
  std::string statusDir = "/var/mobile/fidelio";  // "" disables the status files
};

static Options parseArgs(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + what);
      return argv[++i];
    };
    if (a == "-h" || a == "--help") o.help = true;
    else if (a == "-u" || a == "--username") o.username = next("-u");
    else if (a == "-p" || a == "--password") o.password = next("-p");
    else if (a == "-c" || a == "--credentials") o.credentials = next("-c");
    else if (a == "-n" || a == "--name") o.name = next("-n");
    else if (a == "-b" || a == "--bitrate") o.bitrate = atoi(next("-b").c_str());
    else if (a == "--port") o.port = atoi(next("--port").c_str());
    else if (a == "--client-id") o.clientId = next("--client-id");
    else if (a == "--client-secret") o.clientSecret = next("--client-secret");
    else if (a == "--no-audio") o.noAudio = true;
    else if (a == "--status-dir") o.statusDir = next("--status-dir");
    else if (a == "--no-status") o.statusDir.clear();
    else if (a == "--audio-route") {
      std::string v = next("--audio-route");
      if (v == "auto") o.audio.route = AudioQueueSink::ROUTE_AUTO;
      else if (v == "speaker") o.audio.route = AudioQueueSink::ROUTE_SPEAKER;
      else if (v == "lineout") o.audio.route = AudioQueueSink::ROUTE_LINEOUT;
      else throw std::invalid_argument("--audio-route must be auto|speaker|lineout");
    } else if (a == "--on-route-change") {
      std::string v = next("--on-route-change");
      if (v == "recreate") o.audio.recovery = AudioQueueSink::RECOVER_RECREATE;
      else if (v == "restart") o.audio.recovery = AudioQueueSink::RECOVER_RESTART;
      else if (v == "none") o.audio.recovery = AudioQueueSink::RECOVER_NONE;
      else throw std::invalid_argument("--on-route-change must be recreate|restart|none");
    }
    else throw std::invalid_argument("unknown option " + a);
  }
  return o;
}

static const char* eventName(cspot::SpircHandler::EventType t) {
  using E = cspot::SpircHandler::EventType;
  switch (t) {
    case E::PLAY_PAUSE: return "PLAY_PAUSE";
    case E::VOLUME: return "VOLUME";
    case E::TRACK_INFO: return "TRACK_INFO";
    case E::DISC: return "DISC";
    case E::NEXT: return "NEXT";
    case E::PREV: return "PREV";
    case E::SEEK: return "SEEK";
    case E::DEPLETED: return "DEPLETED";
    case E::FLUSH: return "FLUSH";
    case E::PLAYBACK_START: return "PLAYBACK_START";
  }
  return "?";
}

class ZeroconfAuthenticator {
 public:
  // A real addUser body is a few KB (base64 blob + DH key). The limit keeps an unauthenticated
  // LAN client from making us allocate Content-Length bytes (body.resize zero-fills them).
  static const long long kMaxBodyBytes = 64 * 1024;
  static constexpr const char* kOkJson = "{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}";
  static constexpr const char* kBadRequestJson =
      "{\"status\":103,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ARGUMENTS\"}";
  int serverPort = 7864;
  std::unique_ptr<bell::BellHTTPServer> server;
  std::unique_ptr<MDNSService> mdns;
  std::shared_ptr<cspot::LoginBlob> blob;
  std::function<void()> onAuthSuccess;
  std::function<void()> onClose;
  // Set by the first accepted addUser. The blob is then owned by the session (it is reused for
  // every reconnect), so later POSTs must not rewrite it; we do not support switching users.
  std::atomic<bool> authenticated{false};

  void registerHandlers() {
    server = std::make_unique<bell::BellHTTPServer>(serverPort);

    server->registerGet("/spotify_info", [this](struct mg_connection*) {
      IOS3_TRACE("zeroconf GET /spotify_info");
      return server->makeJsonResponse(blob->buildZeroconfInfo());
    });
    server->registerGet("/close", [this](struct mg_connection*) {
      onClose();
      return server->makeEmptyResponse();
    });
    server->registerPost("/spotify_info", [this](struct mg_connection* conn) {
      IOS3_TRACE("zeroconf POST received");
      if (authenticated) {
        IOS3_TRACE("zeroconf POST after login, ignored");
        return server->makeJsonResponse(kOkJson);
      }
      auto requestInfo = mg_get_request_info(conn);
      long long length = requestInfo->content_length;
      if (length <= 0) {
        IOS3_TRACE("zeroconf POST without body, ignored");
        return server->makeJsonResponse(kBadRequestJson);
      }
      if (length > kMaxBodyBytes) {
        CSPOT_LOG(error, "Zeroconf POST body of %lld bytes rejected", length);
        return server->makeJsonResponse(kBadRequestJson);
      }
      std::string body((size_t)length, '\0');
      int got = mg_read(conn, body.data(), body.size());
      if (got != (int)length) {
        CSPOT_LOG(error, "Zeroconf POST body truncated (%d of %lld bytes)", got, length);
        return server->makeJsonResponse(kBadRequestJson);
      }
      // Count the fields first (no fixed limit), then split in place.
      int num = mg_split_form_urlencoded(body.data(), nullptr, 0);
      std::vector<mg_header> hd(num > 0 ? num : 1);
      num = mg_split_form_urlencoded(body.data(), hd.data(), hd.size());
      std::map<std::string, std::string> queryMap;
      for (int i = 0; i < num; i++) {
        if (hd[i].name) queryMap[hd[i].name] = hd[i].value ? hd[i].value : "";  // value is NULL for "key&"
      }
      IOS3_TRACE("zeroconf POST body %lld bytes, %d fields", length, num);
      if (queryMap["userName"].empty() || queryMap["blob"].empty() || queryMap["clientKey"].empty()) {
        CSPOT_LOG(error, "Zeroconf POST without userName/blob/clientKey, ignored");
        return server->makeJsonResponse(kBadRequestJson);
      }
      try {
        blob->loadZeroconfQuery(queryMap);
      } catch (const std::exception& e) {
        CSPOT_LOG(error, "Zeroconf blob rejected: %s", e.what());
        return server->makeJsonResponse(kBadRequestJson);
      }
      IOS3_TRACE("loadZeroconfQuery done (user '%s')", blob->getUserName().c_str());
      authenticated = true;
      onAuthSuccess();
      return server->makeJsonResponse(kOkJson);
    });

    mdns = MDNSService::registerService(blob->getDeviceName(), "_spotify-connect", "_tcp", "", serverPort,
                                        {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}});
    std::cout << "Advertised '" << blob->getDeviceName() << "' via Bonjour; waiting for the Spotify app..."
              << std::endl;
  }
};

int main(int argc, char** argv) {
  // Logs must survive a reboot: line-buffered stdout even when piped, unbuffered stderr.
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  signal(SIGPIPE, SIG_IGN);  // no MSG_NOSIGNAL on Darwin; a closed socket must not kill us
  bell::setDefaultLogger();
  bell::enableTimestampLogging();

  Options args;
  try {
    args = parseArgs(argc, argv);
  } catch (const std::invalid_argument& e) {
    std::cout << "Invalid options: " << e.what() << "\nPass --help for more information.\n";
    return 1;
  }
  if (args.help) {
    std::cout << "Usage: cspot-ios3 [OPTION]...\n"
                 "Spotify Connect receiver for iPhone OS 3 (ARMv6), playing through AudioQueue.\n\n"
                 "Run without -u/-c to be discovered via Bonjour by the Spotify app on the LAN.\n"
                 "  -n, --name NAME          device name shown in Spotify (default: iPhone Fidelio)\n"
                 "  -u, --username USER      log in with username/password instead of Zeroconf\n"
                 "  -p, --password PASS\n"
                 "  -c, --credentials FILE   store/load reusable credentials (JSON)\n"
                 "  -b, --bitrate 96|160|320 Vorbis bitrate (default 96)\n"
                 "  --port PORT              Zeroconf HTTP port (default 7864)\n"
                 "  --client-id ID / --client-secret SECRET\n"
                 "                           Spotify developer app credentials; this cspot fork fetches\n"
                 "                           its Web API token from accounts.spotify.com with them\n"
                 "  --audio-route auto|speaker|lineout\n"
                 "                           auto/lineout: follow the system route (dock line-out when docked,\n"
                 "                           speaker otherwise); speaker: force the built-in speaker (iOS 3 can\n"
                 "                           only override towards the speaker)\n"
                 "  --on-route-change recreate|restart|none\n"
                 "                           what to do when the audio route changes or an interruption ends:\n"
                 "                           recreate the AudioQueue (default), stop/start it, or nothing\n"
                 "  --no-audio               diagnostic: no AudioQueue sink and no player thread;\n"
                 "                           decoded audio is discarded, Spirc events are logged\n"
                 "  --status-dir DIR         write status.json / status.plist / cover.jpg there\n"
                 "                           (default /var/mobile/fidelio); --no-status disables\n";
    return 0;
  }
  IOS3_TRACE("cspot-ios3 rev5 start (no-audio=%d, bitrate=%d, port=%d)", (int)args.noAudio, args.bitrate, args.port);

  std::unique_ptr<StatusWriter> status;
  if (!args.statusDir.empty()) {
    status = std::make_unique<StatusWriter>(args.statusDir, args.name, args.bitrate);
    IOS3_TRACE("StatusWriter writing to %s", args.statusDir.c_str());
  }

  std::unique_ptr<AudioQueueSink> audioSink;
  if (!args.noAudio) {
    audioSink = std::make_unique<AudioQueueSink>(args.audio);
    IOS3_TRACE("AudioQueueSink created");
  }
  auto loggedInSemaphore = std::make_shared<bell::WrappedSemaphore>(1);
  auto zeroconfServer = std::make_unique<ZeroconfAuthenticator>();
  zeroconfServer->serverPort = args.port;
  std::atomic<bool> isRunning{true};
  zeroconfServer->onClose = [&isRunning]() { isRunning = false; };

  auto loginBlob = std::make_shared<cspot::LoginBlob>(args.name);
  bool haveCredentials = false;

  if (!args.username.empty()) {
    loginBlob->loadUserPass(args.username, args.password);
    haveCredentials = true;
  } else if (!args.credentials.empty()) {
    std::string json = readFile(args.credentials);
    if (!json.empty()) {
      loginBlob->loadJson(json);
      haveCredentials = true;
    }
  }

  if (haveCredentials) {
    loggedInSemaphore->give();
  } else {
    zeroconfServer->blob = loginBlob;
    zeroconfServer->onAuthSuccess = [loggedInSemaphore]() { loggedInSemaphore->give(); };
    // A restart within TCP TIME_WAIT of the previous instance can fail to bind the port;
    // retry instead of dying on the CivetException (which otherwise escapes main).
    const int maxAttempts = 60;
    for (int attempt = 1;; attempt++) {
      try {
        zeroconfServer->registerHandlers();
        break;
      } catch (const std::exception& e) {
        if (attempt >= maxAttempts) {
          CSPOT_LOG(error, "Could not start the Zeroconf HTTP server on port %d: %s", args.port, e.what());
          return 3;
        }
        CSPOT_LOG(error, "Zeroconf HTTP server on port %d failed to start (%s); retrying in 2 s (%d/%d)",
                  args.port, e.what(), attempt, maxAttempts);
        BELL_SLEEP_MS(2000);
      }
    }
    IOS3_TRACE("zeroconf server + mDNS registered");
  }

  loggedInSemaphore->wait();
  IOS3_TRACE("credentials available, creating context");
  auto ctx = cspot::Context::createFromBlob(loginBlob);
  if (args.bitrate == 320) ctx->config.audioFormat = AudioFormat_OGG_VORBIS_320;
  else if (args.bitrate == 160) ctx->config.audioFormat = AudioFormat_OGG_VORBIS_160;
  else ctx->config.audioFormat = AudioFormat_OGG_VORBIS_96;
  ctx->config.clientId = args.clientId;
  ctx->config.clientSecret = args.clientSecret;

  IOS3_TRACE("connectWithRandomAp begin");
  ctx->session->connectWithRandomAp();
  IOS3_TRACE("connectWithRandomAp done");
  IOS3_TRACE("authenticate begin");
  ctx->config.authData = ctx->session->authenticate(loginBlob);
  IOS3_TRACE("authenticate done, authData len=%d", (int)ctx->config.authData.size());

  if (ctx->config.authData.empty()) {
    CSPOT_LOG(error, "Authentication failed");
    return 2;
  }
  if (!args.credentials.empty()) writeFile(args.credentials, ctx->getCredentialsJson());

  auto handler = std::make_shared<cspot::SpircHandler>(ctx);
  IOS3_TRACE("SpircHandler created");
  ctx->session->startTask();
  IOS3_TRACE("startTask done");
  if (status) {
    std::shared_ptr<cspot::Context> ctxForStatus = ctx;
    status->setCountryProvider([ctxForStatus]() { return ctxForStatus->session->getCountryCode(); });
    status->setConnection(ctx->session->lastApAddress, ctx->session->getCountryCode());
  }

  std::shared_ptr<CliPlayer> player;
  if (args.noAudio) {
    // Diagnostic mode: keep the full cspot pipeline (track queue, CDN fetch, Vorbis decode)
    // but discard the PCM and only log the events. No AudioQueue, no player thread.
    handler->getTrackPlayer()->setDataCallback(
        [](uint8_t*, size_t bytes, std::string_view) { return bytes; });
    StatusWriter* statusPtr = status.get();
    handler->setEventHandler([statusPtr](std::unique_ptr<cspot::SpircHandler::Event> event) {
      using E = cspot::SpircHandler::EventType;
      if (statusPtr) statusPtr->onEvent(*event);
      switch (event->eventType) {
        case E::PLAY_PAUSE: IOS3_TRACE("spirc event PLAY_PAUSE paused=%d", (int)std::get<bool>(event->data)); break;
        case E::VOLUME: IOS3_TRACE("spirc event VOLUME %d", std::get<int>(event->data)); break;
        case E::SEEK: IOS3_TRACE("spirc event SEEK %d ms", std::get<int>(event->data)); break;
        case E::PLAYBACK_START: IOS3_TRACE("spirc event PLAYBACK_START at %d ms", std::get<int>(event->data)); break;
        case E::TRACK_INFO: {
          auto& info = std::get<cspot::TrackInfo>(event->data);
          IOS3_TRACE("spirc event TRACK_INFO %s - %s (%s)", info.artist.c_str(), info.name.c_str(), info.album.c_str());
          break;
        }
        default: IOS3_TRACE("spirc event %s", eventName(event->eventType)); break;
      }
    });
    IOS3_TRACE("no-audio mode: discarding sink installed");
  } else {
    player = std::make_shared<CliPlayer>(std::move(audioSink), handler, status.get());
    IOS3_TRACE("CliPlayer created");
  }

  IOS3_TRACE("entering handlePacket loop");
  while (isRunning) ctx->session->handlePacket();

  handler->disconnect();
  if (player) player->disconnect();
  return 0;
}
