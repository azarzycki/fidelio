// cspot-ios3: command-line Spotify Connect receiver for ARMv6 iPhone OS 3.x.
// Based on feelfreelinux/cspot targets/cli/main.cpp (1b07a9c), using the squeezelite-esp32
// cspot fork (ee1720c). Zeroconf discovery via the system dns_sd (bell's Apple MDNSService),
// HTTP login endpoint via bell's civetweb server, audio via AudioQueue.
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
#include "PlayerSink.h"
VolumeCurve PlayerSink::curve = VOLUME_LOG;
#include "audiod/proto.h"
#include "Logger.h"
#include "LoginBlob.h"
#include "MDNSService.h"
#include "MercurySession.h"
#include "SpircHandler.h"
#include "StatusWriter.h"
#include "TrackPlayer.h"
#include "TrackQueue.h"
#include "WrappedSemaphore.h"
#include "cJSON.h"
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

// Owner-only and atomic (tmp + rename): the file holds the reusable auth blob, and the device
// reboots under memory pressure, so a half-written file must never replace a good one.
static bool writePrivateFile(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  bool ok = fchmod(fd, 0600) == 0;
  for (size_t done = 0; ok && done < data.size();) {
    ssize_t n = write(fd, data.data() + done, data.size() - done);
    if (n <= 0) ok = false;
    else done += (size_t)n;
  }
  ok = (close(fd) == 0) && ok;
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  return true;
}

// LoginBlob::loadJson dereferences the fields without checking, so a corrupt file would crash us
// at every start. Only hand it a document that has what it reads.
static bool credentialsLookValid(const std::string& json) {
  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) return false;
  cJSON* type = cJSON_GetObjectItem(root, "authType");
  cJSON* user = cJSON_GetObjectItem(root, "username");
  cJSON* data = cJSON_GetObjectItem(root, "authData");
  bool ok = cJSON_IsNumber(type) && cJSON_IsString(user) && cJSON_IsString(data) && data->valuestring[0] != 0;
  cJSON_Delete(root);
  return ok;
}

struct Options {
  std::string username, password, credentials, name = "iPhone Fidelio";
  std::string clientId, clientSecret;
  int bitrate = 96;  // kindest to a 412 MHz ARM11
  int port = 7864;
  bool help = false;
  bool noAudio = false;
  bool directAudio = false;  // --direct-audio: local AudioQueue instead of fidelio-audiod
  std::string audiodSock = getenv("AUDIOD_SOCK") && *getenv("AUDIOD_SOCK") ? getenv("AUDIOD_SOCK") : AUDIOD_DEFAULT_SOCK;
  int sinkTestSec = 0;       // --sink-test N: play N s of a 440 Hz tone through the sink and exit
  int sinkTestVolumePct = -1;  // --sink-test-volume PCT: 40 % into the tone, move the Connect slider there (host test of B1.9)
  int audiodLeadMs = 0;      // --audiod-lead-ms: 0 = SocketSink default / env AUDIOD_LEAD_MS
  int initialVolumePct = 50; // --initial-volume: what Connect sees and what we play at before the first slider move
  VolumeCurve volumeCurve = VOLUME_LOG;  // --volume-curve
#ifdef __APPLE__
  AudioQueueSink::Options audio;  // --audio-route, --on-route-change, --audio-session (direct audio only)
#endif
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
    else if (a == "--direct-audio") o.directAudio = true;
    else if (a == "--audiod-sock") o.audiodSock = next("--audiod-sock");
    else if (a == "--audiod-lead-ms") o.audiodLeadMs = atoi(next("--audiod-lead-ms").c_str());
    else if (a == "--sink-test") o.sinkTestSec = atoi(next("--sink-test").c_str());
    else if (a == "--sink-test-volume") o.sinkTestVolumePct = atoi(next("--sink-test-volume").c_str());
    else if (a == "--initial-volume") {
      o.initialVolumePct = atoi(next("--initial-volume").c_str());
      if (o.initialVolumePct < 0 || o.initialVolumePct > 100) throw std::invalid_argument("--initial-volume must be 0..100");
    } else if (a == "--volume-curve") {
      std::string v = next("--volume-curve");
      if (v == "log") o.volumeCurve = VOLUME_LOG;
      else if (v == "cubic") o.volumeCurve = VOLUME_CUBIC;
      else if (v == "linear") o.volumeCurve = VOLUME_LINEAR;
      else throw std::invalid_argument("--volume-curve must be log|cubic|linear");
    }
    else if (a == "--status-dir") o.statusDir = next("--status-dir");
    else if (a == "--no-status") o.statusDir.clear();
    else if (a == "--audio-route") {
      std::string v = next("--audio-route");
#ifdef __APPLE__
      if (v == "auto") o.audio.route = AudioQueueSink::ROUTE_AUTO;
      else if (v == "speaker") o.audio.route = AudioQueueSink::ROUTE_SPEAKER;
      else if (v == "lineout") o.audio.route = AudioQueueSink::ROUTE_LINEOUT;
      else throw std::invalid_argument("--audio-route must be auto|speaker|lineout");
#endif
    } else if (a == "--on-route-change") {
      std::string v = next("--on-route-change");
#ifdef __APPLE__
      if (v == "recreate") o.audio.recovery = AudioQueueSink::RECOVER_RECREATE;
      else if (v == "restart") o.audio.recovery = AudioQueueSink::RECOVER_RESTART;
      else if (v == "none") o.audio.recovery = AudioQueueSink::RECOVER_NONE;
      else throw std::invalid_argument("--on-route-change must be recreate|restart|none");
#endif
    } else if (a == "--audio-session") {
      std::string v = next("--audio-session");
#ifdef __APPLE__
      if (v == "auto") o.audio.session = AudioQueueSink::SESSION_AUTO;
      else if (v == "on") o.audio.session = AudioQueueSink::SESSION_ON;
      else if (v == "off") o.audio.session = AudioQueueSink::SESSION_OFF;
      else throw std::invalid_argument("--audio-session must be auto|on|off");
#endif
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
                 "Spotify Connect receiver for iPhone OS 3 (ARMv6), playing through fidelio-audiod.\n\n"
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
                 "  --audiod-sock PATH       fidelio-audiod socket (default " AUDIOD_DEFAULT_SOCK "; env AUDIOD_SOCK)\n"
                 "  --audiod-lead-ms MS      how far ahead of real time to stream to audiod (default 750; env AUDIOD_LEAD_MS)\n"
                 "  --direct-audio           play through a local AudioQueue instead of fidelio-audiod\n"
                 "                           (standalone debugging; the two options below apply to it)\n"
                 "  --sink-test SECONDS      play a 440 Hz tone through the sink for that long and exit\n"
                 "  --sink-test-volume PCT   with --sink-test: move the Connect slider to PCT 40 %% into the tone (B1.9 host test)\n"
                 "  --initial-volume PCT     Connect volume reported and applied at startup (default 50; the\n"
                 "                           library's 0 made the slider sit at zero and jump on first touch)\n"
                 "  --volume-curve log|cubic|linear\n"
                 "                           slider -> gain: log over 60 dB (default, like librespot), cubic, or linear\n"
                 "  --audio-route auto|speaker|lineout\n"
                 "                           auto/lineout: follow the system route (dock line-out when docked,\n"
                 "                           speaker otherwise); speaker: force the built-in speaker (iOS 3 can\n"
                 "                           only override towards the speaker)\n"
                 "  --on-route-change recreate|restart|none\n"
                 "                           what to do when the audio route changes or an interruption ends:\n"
                 "                           recreate the AudioQueue (default), stop/start it, or nothing\n"
                 "  --audio-session auto|on|off\n"
                 "                           auto (default): full AudioSession, retried forever if it fails, bare queue\n"
                 "                           meanwhile; on: no audio until the session is up; off: bare queue, no\n"
                 "                           AudioSession calls (A/B diagnosis only)\n"
                 "  --no-audio               diagnostic: no AudioQueue sink and no player thread;\n"
                 "                           decoded audio is discarded, Spirc events are logged\n"
                 "  --status-dir DIR         write status.json / status.plist / cover.jpg there\n"
                 "                           (default /var/mobile/fidelio); --no-status disables\n";
    return 0;
  }
  IOS3_TRACE("cspot-ios3 rev8 start (no-audio=%d, bitrate=%d, port=%d)", (int)args.noAudio, args.bitrate, args.port);

  std::unique_ptr<StatusWriter> status;
  if (!args.statusDir.empty()) {
    status = std::make_unique<StatusWriter>(args.statusDir, args.name, args.bitrate);
    IOS3_TRACE("StatusWriter writing to %s", args.statusDir.c_str());
  }

  std::unique_ptr<PlayerSink> audioSink;
  if (!args.noAudio) {
    if (args.directAudio) {
#ifdef __APPLE__
      audioSink = std::make_unique<LocalQueueSink>(args.audio);
#else
      fprintf(stderr, "--direct-audio: there is no AudioQueue on the host\n");
      return 2;
#endif
    } else {
      audioSink = std::make_unique<AudiodSink>(args.audiodSock, (unsigned)(args.audiodLeadMs > 0 ? args.audiodLeadMs : 0));
    }
    PlayerSink::curve = args.volumeCurve;
    uint16_t v0 = (uint16_t)(args.initialVolumePct * 65535 / 100);
    // B1.9: give audiod's WELCOME a moment (it says whether the slider drives the hardware and the
    // current level). In absolute mode nothing is SET at startup: the hardware level is what the
    // controller will show. Otherwise (software mode, old audiod, audiod not up yet) B1.7 applies.
    for (int i = 0; i < 25 && !audioSink->hardwareVolumeAbsolute(); i++) { audioSink->poll(); usleep(20000); }
    if (audioSink->hardwareVolumeAbsolute()) {
      IOS3_TRACE("audio sink: %s; audiod drives the HARDWARE volume (absolute): level %u %% (slider %u) is what Connect will show; "
                 "--initial-volume %d ignored, curve %s applies to nothing here",
                 audioSink->describe().c_str(), audioSink->hardwareSlider() * 100 / 65535, audioSink->hardwareSlider(), args.initialVolumePct,
                 volumeCurveName(args.volumeCurve));
    } else {
      audioSink->volumeChanged(v0);  // play at what Connect will show
      IOS3_TRACE("audio sink: %s; volume curve %s, initial %d%% -> gain %.4f", audioSink->describe().c_str(), volumeCurveName(args.volumeCurve),
                 args.initialVolumePct, connectVolumeToGain(v0, args.volumeCurve));
    }
  }
  if (args.sinkTestSec > 0) {
    // No Spotify at all: just prove the audio path (host: into audiod; device: dock/speaker).
    if (!audioSink) { fprintf(stderr, "--sink-test needs a sink (drop --no-audio)\n"); return 2; }
    IOS3_TRACE("sink test: %d s of a 440 Hz tone", args.sinkTestSec);
    audioSink->onHardwareVolume = [](void*, bool absolute, uint16_t slider) {
      IOS3_TRACE("sink test: audiod reports hardware volume %s: %u %% (slider %u)", absolute ? "absolute" : "software", slider * 100 / 65535, slider);
    };
    std::vector<int16_t> tone(882 * 2);
    double phase = 0;
    for (int c = 0; c < args.sinkTestSec * 50; c++) {
      audioSink->poll();
      if (args.sinkTestVolumePct >= 0 && c == args.sinkTestSec * 20) {  // 40 % in: a user slider move
        uint16_t v = (uint16_t)(args.sinkTestVolumePct * 65535 / 100);
        IOS3_TRACE("sink test: Connect slider -> %d %% (slider %u)", args.sinkTestVolumePct, v);
        audioSink->volumeChanged(v);
      }
      for (int i = 0; i < 882; i++) {
        int16_t v = (int16_t)(8000 * sin(phase));
        tone[2 * i] = tone[2 * i + 1] = v;
        phase += 2 * M_PI * 440 / 44100;
      }
      audioSink->feedPCMFrames(reinterpret_cast<const uint8_t*>(tone.data()), tone.size() * 2);
    }
    while (audioSink->bufferedSeconds() > 0.05) { audioSink->poll(); usleep(20000); }
    for (int i = 0; i < 10; i++) { audioSink->poll(); usleep(20000); }  // pick up a late HWVOLUME report
    audioSink->audioEnded();
    IOS3_TRACE("sink test done");
    return 0;
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
    if (json.empty()) {
      // first run: fall through to Zeroconf, the file is written after login
    } else if (credentialsLookValid(json)) {
      loginBlob->loadJson(json);
      haveCredentials = true;
    } else {
      CSPOT_LOG(error, "Credentials file %s is not usable, logging in via Zeroconf instead", args.credentials.c_str());
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
  ctx->config.volume = args.initialVolumePct * 65535 / 100;  // reported in device_state until the first remote change
  if (audioSink && audioSink->hardwareVolumeAbsolute()) {  // B1.9: the true hardware level instead (never set anything at startup)
    ctx->config.volume = audioSink->hardwareSlider();
    IOS3_TRACE("device_state volume = hardware level %u %% (absolute mode)", audioSink->hardwareSlider() * 100 / 65535);
  }

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
  if (!args.credentials.empty() && !writePrivateFile(args.credentials, ctx->getCredentialsJson()))
    CSPOT_LOG(error, "Cannot write credentials to %s: %s", args.credentials.c_str(), strerror(errno));

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
