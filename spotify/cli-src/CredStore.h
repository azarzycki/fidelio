#pragma once
// Reusable-credentials file helpers, shared by main.cpp and the host test (user_switch_test).
//
// The credentials-store file holds a reusable Spotify auth blob (loginBlob->toJson(): authData is a
// base64 secret). It is therefore written owner-only (0600) and atomically (tmp + rename), and the
// callers log only usernames, never authData. These were static functions in main.cpp until B1.20;
// they moved into this header so the store round-trip and the validity check can be tested directly.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "cJSON.h"

inline std::string readFile(const std::string& path) {
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
inline bool writePrivateFile(const std::string& path, const std::string& data) {
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
inline bool credentialsLookValid(const std::string& json) {
  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) return false;
  cJSON* type = cJSON_GetObjectItem(root, "authType");
  cJSON* user = cJSON_GetObjectItem(root, "username");
  cJSON* data = cJSON_GetObjectItem(root, "authData");
  bool ok = cJSON_IsNumber(type) && cJSON_IsString(user) && cJSON_IsString(data) && data->valuestring[0] != 0;
  cJSON_Delete(root);
  return ok;
}
