#include "Dmap.h"

#include <string.h>

static void walk(const uint8_t* d, size_t len, std::map<std::string, std::string>& out, int depth) {
  size_t i = 0;
  while (i + 8 <= len) {
    std::string tag((const char*)d + i, 4);
    uint32_t n = ((uint32_t)d[i + 4] << 24) | ((uint32_t)d[i + 5] << 16) | ((uint32_t)d[i + 6] << 8) | d[i + 7];
    i += 8;
    if (n > len - i) n = (uint32_t)(len - i);
    bool container = tag == "mlit" || tag == "mlcl" || tag == "mlog" || tag == "mdcl" || tag == "msrv" || tag == "mccr" ||
                     tag == "adbs" || tag == "abro" || tag == "mupd";
    if (container && depth < 4) walk(d + i, n, out, depth + 1);
    else out[tag] = std::string((const char*)d + i, n);
    i += n;
  }
}

std::map<std::string, std::string> dmapParse(const uint8_t* data, size_t len) {
  std::map<std::string, std::string> out;
  walk(data, len, out, 0);
  return out;
}
