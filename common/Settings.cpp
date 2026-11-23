#include "Settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace dockreceiver {

const std::string Registry::kEmpty;

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Registry::normBool(const std::string& v) { return (v == "on" || v == "true" || v == "1") ? "on" : "off"; }

Option& Registry::add(const Option& o) {
  opts_.push_back(o);
  Option& r = opts_.back();
  if (r.flags.empty() && !r.flag.empty()) r.flags.push_back(r.flag);
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

bool Registry::flagMatches(const Option& o, const std::string& arg) const {
  for (const std::string& f : o.flags) if (!f.empty() && f == arg) return true;
  return false;
}

bool Registry::validate(const Option& o, const std::string& v) const {
  if (o.type == Option::BOOL) return v == "on" || v == "off" || v == "true" || v == "false" || v == "0" || v == "1";
  if (o.type == Option::ENUM) { for (auto& c : o.choices) if (c.value == v) return true; return false; }
  if (o.type == Option::STRING) {
    if (v.empty() || (long)v.size() > o.maxlen) return false;
    for (unsigned char c : v) if (c < 0x20 || c == 0x7f) return false;  // no control chars; Bonjour allows UTF-8 printable
    return true;
  }
  // INT / PERCENT
  char* end = nullptr;
  long n = strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || *end) return false;
  return n >= o.imin && n <= o.imax;
}

void Registry::markCommandLine(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    for (auto& o : opts_) {
      if (!flagMatches(o, a)) continue;
      o.origin = "command line";
      o.locked = true;
      // Skip the value token so a value that looks like another flag is not re-matched. A BOOL flag
      // only consumes a following on|off; a bare presence is "on".
      if (flagTakesValue(o)) { if (i + 1 < argc) i++; }
      else if (i + 1 < argc) { std::string n = argv[i + 1]; if (n == "on" || n == "off") i++; }
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
    // Apply to the LIVE struct (single source of truth), not just the registry's copy.
    if (o->write && !o->write(nv)) { fprintf(stderr, "settings.conf: %s.%s='%s' rejected by the daemon\n", daemon_.c_str(), key.c_str(), nv.c_str()); continue; }
    if (o->value != nv || o->origin != "file") { o->value = nv; o->origin = "file"; changed = true; }
  }
  fclose(f);
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

void Registry::refreshValues() {
  for (auto& o : opts_) if (o.read) o.value = o.read();
}

bool Registry::selfCheck() const {
  bool ok = true;
  for (const Option& o : opts_) {
    if (!o.read) continue;
    std::string eff = o.read();
    if (o.type == Option::BOOL) eff = normBool(eff);
    if (o.origin == "default") {
      std::string def = o.type == Option::BOOL ? normBool(o.def) : o.def;
      if (eff != def) {
        fprintf(stderr, "settings: SELF-CHECK FAIL %s.%s: registry default '%s' but the program uses '%s' — fix the registry default to match the program\n",
                daemon_.c_str(), o.key.c_str(), o.def.c_str(), eff.c_str());
        ok = false;
      }
    } else if (o.origin == "file") {
      std::string want = o.type == Option::BOOL ? normBool(o.value) : o.value;
      if (eff != want) {
        fprintf(stderr, "settings: SELF-CHECK FAIL %s.%s: settings.conf value '%s' was not applied (the program uses '%s')\n",
                daemon_.c_str(), o.key.c_str(), want.c_str(), eff.c_str());
        ok = false;
      }
    }
    // "command line": the hand parser owns the value; nothing to assert (manifest shows read()).
  }
  if (ok) fprintf(stderr, "settings: self-check ok (%zu options consistent with the live config)\n", opts_.size());
  return ok;
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
    const char* ty = o.type == Option::BOOL ? "bool" : o.type == Option::ENUM ? "enum"
                   : o.type == Option::PERCENT ? "percent" : o.type == Option::STRING ? "string" : "int";
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
    if (o.type == Option::STRING) plistInt(out, "maxlen", o.maxlen);
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

}  // namespace dockreceiver
