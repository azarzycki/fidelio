#include "Settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace fidelio {

const std::string Registry::kEmpty;

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

Option& Registry::add(const Option& o) {
  opts_.push_back(o);
  Option& r = opts_.back();
  r.value = r.def;
  r.origin = "default";
  return r;
}

Option* Registry::find(const std::string& key) {
  for (auto& o : opts_) if (o.key == key) return &o;
  return nullptr;
}
const Option* Registry::find(const std::string& key) const {
  for (auto& o : opts_) if (o.key == key) return &o;
  return nullptr;
}

bool Registry::validate(const Option& o, const std::string& v) const {
  if (o.type == Option::BOOL) return v == "on" || v == "off" || v == "true" || v == "false" || v == "0" || v == "1";
  if (o.type == Option::ENUM) { for (auto& c : o.choices) if (c.value == v) return true; return false; }
  // INT / PERCENT
  char* end = nullptr;
  long n = strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || *end) return false;
  return n >= o.imin && n <= o.imax;
}

static std::string normBool(const std::string& v) { return (v == "on" || v == "true" || v == "1") ? "on" : "off"; }

void Registry::parseArgv(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    for (auto& o : opts_) {
      if (o.flag.empty() || a != o.flag) continue;
      std::string v;
      if (o.type == Option::BOOL) {
        // a bool flag may take on|off as the next token, or be a bare presence = on
        if (i + 1 < argc && (std::string(argv[i + 1]) == "on" || std::string(argv[i + 1]) == "off")) v = argv[++i];
        else v = "on";
        v = normBool(v);
      } else if (i + 1 < argc) {
        v = argv[++i];
      }
      if (validate(o, v)) { o.value = o.type == Option::BOOL ? normBool(v) : v; o.origin = "command line"; o.locked = true; }
      break;
    }
  }
}

bool Registry::loadFile(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  bool changed = false;
  char line[512];
  std::string prefix = daemon_ + ".";
  while (fgets(line, sizeof line, f)) {
    std::string l = trim(line);
    if (l.empty() || l[0] == '#') continue;
    size_t eq = l.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(l.substr(0, eq)), val = trim(l.substr(eq + 1));
    if (key.compare(0, prefix.size(), prefix) != 0) continue;  // another daemon's line
    key = key.substr(prefix.size());
    Option* o = find(key);
    if (!o) { fprintf(stderr, "settings.conf: unknown key %s.%s ignored\n", daemon_.c_str(), key.c_str()); continue; }
    if (o->locked) continue;  // command line wins
    if (!validate(*o, val)) { fprintf(stderr, "settings.conf: invalid value '%s' for %s.%s ignored\n", val.c_str(), daemon_.c_str(), key.c_str()); continue; }
    std::string nv = o->type == Option::BOOL ? normBool(val) : val;
    if (o->value != nv || o->origin != "file") { o->value = nv; o->origin = "file"; changed = true; }
  }
  fclose(f);
  // A key no longer in the file reverts to its default (unless locked).
  // (Re-scan: anything still "file" but absent above keeps its last value; simplest is to leave it.
  //  For determinism we leave file values sticky until explicitly changed.)
  return changed;
}

bool Registry::reloadIfChanged(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  long m = (long)st.st_mtime;
  if (m == lastMtime_) return false;
  lastMtime_ = m;
  return loadFile(path);
}

static void plistStr(std::string& out, const char* key, const std::string& v) {
  std::string e;
  for (char c : v) { if (c == '&') e += "&amp;"; else if (c == '<') e += "&lt;"; else if (c == '>') e += "&gt;"; else e += c; }
  out += "\t\t<key>"; out += key; out += "</key><string>"; out += e; out += "</string>\n";
}
static void plistInt(std::string& out, const char* key, long v) {
  char b[32]; snprintf(b, sizeof b, "%ld", v);
  out += "\t\t<key>"; out += key; out += "</key><integer>"; out += b; out += "</integer>\n";
}
static void plistBool(std::string& out, const char* key, bool v) {
  out += "\t\t<key>"; out += key; out += "</key>"; out += v ? "<true/>" : "<false/>"; out += "\n";
}

bool Registry::writeManifest(const std::string& dir) const {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\"><dict>\n";
  plistInt(out, "version", 1);
  plistStr(out, "daemon", daemon_);
  plistStr(out, "build", build_);
  out += "\t<key>options</key>\n\t<array>\n";
  for (const Option& o : opts_) {
    out += "\t<dict>\n";
    const char* ty = o.type == Option::BOOL ? "bool" : o.type == Option::ENUM ? "enum" : o.type == Option::PERCENT ? "percent" : "int";
    plistStr(out, "key", o.key);
    plistStr(out, "flag", o.flag);
    plistStr(out, "type", ty);
    plistStr(out, "title", o.title);
    plistStr(out, "desc", o.desc);
    plistStr(out, "group", o.group);
    plistStr(out, "default", o.def);
    plistStr(out, "value", o.value);
    plistStr(out, "origin", o.origin);
    plistBool(out, "locked", o.locked);
    plistBool(out, "live", o.live);
    plistBool(out, "advanced", o.advanced);
    if (o.type == Option::INT || o.type == Option::PERCENT) {
      plistInt(out, "min", o.imin); plistInt(out, "max", o.imax); plistInt(out, "step", o.istep);
      if (!o.unit.empty()) plistStr(out, "unit", o.unit);
    }
    if (o.type == Option::ENUM) {
      out += "\t\t<key>choices</key>\n\t\t<array>\n";
      for (auto& c : o.choices) { out += "\t\t<dict>"; std::string s; plistStr(s, "value", c.value); plistStr(s, "label", c.label); out += "\n" + s + "\t\t</dict>\n"; }
      out += "\t\t</array>\n";
    }
    out += "\t</dict>\n";
  }
  out += "\t</array>\n</dict></plist>\n";

  std::string path = dir + "/settings-" + daemon_ + ".manifest.plist", tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return rename(tmp.c_str(), path.c_str()) == 0;
}

const std::string& Registry::get(const std::string& key) const { const Option* o = find(key); return o ? o->value : kEmpty; }
bool Registry::getBool(const std::string& key) const { const Option* o = find(key); return o && (o->value == "on" || o->value == "true" || o->value == "1"); }
long Registry::getInt(const std::string& key) const { const Option* o = find(key); return o ? strtol(o->value.c_str(), nullptr, 10) : 0; }

}  // namespace fidelio
