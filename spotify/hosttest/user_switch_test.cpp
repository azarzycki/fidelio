// Host test for the Spotify receiver's sequential cross-account control (B1.20):
//   * postDecision(): the pure Zeroconf add-user decision (UserSwitch.h).
//   * the credentials-store round-trip and validity check (CredStore.h).
// Compiled standalone like cover_url_test (c++ -O2 -std=c++17 -I spotify/cli-src), plus cJSON for
// credentialsLookValid. No real credentials anywhere: authData is a fabricated base64 string.
#include <cstdio>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "CredStore.h"   // readFile, writePrivateFile, credentialsLookValid
#include "UserSwitch.h"  // postDecision, PostDecision
#include "cJSON.h"

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

// A fabricated reusable-creds document shaped exactly like cspot::LoginBlob::toJson().
// authData is base64("not-a-real-secret") = a clearly fake, non-real secret.
#define FAKE_AUTHDATA "bm90LWEtcmVhbC1zZWNyZXQ="
static const std::string kGood =
    "{\"authData\":\"" FAKE_AUTHDATA "\",\"authType\":1,\"username\":\"bob\"}";

int main() {
  using D = cspot::PostDecision;

  // ---- postDecision: the four outcomes, with fake usernames ---------------------------------
  // Not authenticated yet: the first add-user is always a first login, whatever "playing" says.
  CHECK(cspot::postDecision(false, false, "", "alice") == D::FirstLogin, "not authenticated -> FirstLogin");
  CHECK(cspot::postDecision(true, false, "", "alice") == D::FirstLogin, "not authenticated, 'playing' ignored -> FirstLogin");
  // Authenticated as alice: a re-add by alice is always OK, even mid-song.
  CHECK(cspot::postDecision(false, true, "alice", "alice") == D::SameUserOk, "same account while idle -> SameUserOk");
  CHECK(cspot::postDecision(true, true, "alice", "alice") == D::SameUserOk, "same account while playing -> SameUserOk (a re-add, not a switch)");
  // A different account: refuse fast while playing, honour the switch while idle.
  CHECK(cspot::postDecision(true, true, "alice", "bob") == D::RefuseBusy, "different account while playing -> RefuseBusy");
  CHECK(cspot::postDecision(false, true, "alice", "bob") == D::SwitchToNewUser, "different account while idle -> SwitchToNewUser");

  // ---- credentialsLookValid: only a document with all three fields is accepted ---------------
  CHECK(credentialsLookValid(kGood), "a well-formed toJson()-shaped doc is valid");
  CHECK(!credentialsLookValid("{\"authType\":1,\"username\":\"bob\"}"), "missing authData is rejected");
  CHECK(!credentialsLookValid("{\"authData\":\"" FAKE_AUTHDATA "\",\"authType\":1}"), "missing username is rejected");
  CHECK(!credentialsLookValid("{\"authData\":\"" FAKE_AUTHDATA "\",\"username\":\"bob\"}"), "missing authType is rejected");
  CHECK(!credentialsLookValid("{\"authData\":\"\",\"authType\":1,\"username\":\"bob\"}"), "empty authData is rejected");
  CHECK(!credentialsLookValid("{\"authData\":\"" FAKE_AUTHDATA "\",\"authType\":\"1\",\"username\":\"bob\"}"), "authType as a string (not a number) is rejected");
  CHECK(!credentialsLookValid("not json at all {"), "a non-JSON string is rejected");

  // ---- store round-trip: write (0600, atomic) -> read -> fields survive -----------------------
  const std::string path = "/tmp/user_switch_test." + std::to_string((long)getpid()) + ".json";
  unlink(path.c_str());

  CHECK(writePrivateFile(path, kGood), "writePrivateFile writes the credentials store");
  struct stat st = {};
  CHECK(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "the store file is mode 0600 (owner-only secret)");

  const std::string back = readFile(path);
  CHECK(back == kGood, "readFile returns exactly the bytes writePrivateFile wrote");
  CHECK(credentialsLookValid(back), "the written store passes credentialsLookValid");

  cJSON* root = cJSON_ParseWithLength(back.c_str(), back.size());
  cJSON* u = root ? cJSON_GetObjectItem(root, "username") : nullptr;
  cJSON* t = root ? cJSON_GetObjectItem(root, "authType") : nullptr;
  cJSON* d = root ? cJSON_GetObjectItem(root, "authData") : nullptr;
  CHECK(u && cJSON_IsString(u) && std::string(u->valuestring) == "bob", "round-trip preserves username");
  CHECK(t && cJSON_IsNumber(t) && t->valueint == 1, "round-trip preserves authType");
  CHECK(d && cJSON_IsString(d) && std::string(d->valuestring) == FAKE_AUTHDATA, "round-trip preserves authData (base64, verbatim)");
  cJSON_Delete(root);

  // A corrupt store on disk is rejected (it must never reach LoginBlob::loadJson).
  CHECK(writePrivateFile(path, "{ corrupt, not valid json"), "writePrivateFile writes without validating (validation is on read)");
  CHECK(!credentialsLookValid(readFile(path)), "a corrupt store file read back is rejected");
  unlink(path.c_str());

  printf(fails ? "\n%d FAILURES\n" : "\nall user-switch checks passed\n", fails);
  return fails ? 1 : 0;
}
