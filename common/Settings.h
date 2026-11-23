#pragma once
// B1.14/B1.15: each daemon declares its options ONCE in a Settings registry. The registry writes a
// manifest the NowPlaying app renders a config UI from, reads /var/mobile/dockreceiver/settings.conf, and
// (audiod) re-reads it live. Plain C++ (host + ARMv6 device), no exceptions.
//
// B1.15 — single source of truth. Each Option is BOUND to the daemon's real options struct by two
// callbacks: read() returns the value the code actually uses, write(v) sets it. The daemon's hand
// parser still owns the command line; the registry does not re-parse values, it only:
//   - markCommandLine(argv): marks which options any spelling of a flag set (origin "command line",
//     locked), so the app shows them read-only. Short AND long spellings are listed in Option::flags.
//   - loadFile(conf): applies "<daemon>.key=value" to options NOT set on the command line, by calling
//     write(v) on the live struct (so a file value really takes effect, not just a parallel copy).
//   - refreshValues(): value = read() for every option, so the manifest is the live truth.
//   - selfCheck(): logs FAIL if, for a default option, read() != default, or for a file option,
//     read() != the file value that was applied. Catches "registry default drifts from the program's"
//     and "file value shown but not applied".
//
// settings.conf is UTF-8 "daemon.key=value" lines (ssh-editable); unknown keys / invalid values are
// ignored with one log line and never stop a daemon. The manifest is settings-<daemon>.manifest.plist
// (a property list NSDictionary/NSArray load on iOS 3.1), written atomically.
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

namespace dockreceiver {

struct Choice { std::string value, label; };

struct Option {
  std::string key;          // stable id, also the settings.conf key (after "<daemon>.")
  std::string flag;         // canonical long flag for display, e.g. "--max-volume" ("" = no flag)
  std::vector<std::string> flags;  // B1.15: ALL command-line spellings incl. short (e.g. {"--name","-n"}); empty => {flag}
  enum Type { BOOL, ENUM, INT, PERCENT, STRING } type = BOOL;
  std::vector<Choice> choices;  // ENUM
  long imin = 0, imax = 100, istep = 1;  // INT / PERCENT
  long maxlen = 63;         // STRING: max byte length (Bonjour instance names are <= 63 bytes)
  std::string unit;         // INT: "ms", "kbps", ...
  std::string def;          // default value as a string — MUST equal the program's own default
  std::string title;        // short UI title
  std::string desc;         // one sentence
  std::string group;        // "Volume", "Source switching", "Display", "AirPlay", "Spotify", "Advanced"
  bool live = true;         // applies live (true) or needs a restart of this daemon (false)
  bool advanced = false;    // hidden behind the "advanced" disclosure

  // B1.15 binding to the live options struct (set by the daemon's buildRegistry). read/write are the
  // single source of truth; `value` is just read()'s result cached at manifest-write time.
  std::function<std::string()> read;              // effective value from the live struct, as a string
  std::function<bool(const std::string&)> write;  // set the live struct from a value string; returns ok

  // runtime bookkeeping (NOT a parallel source of truth):
  std::string value;        // == read() after refreshValues(); the manifest's "effective value"
  std::string origin;       // "default" | "file" | "command line"
  bool locked = false;      // fixed on the command line: the app shows it but cannot change it
};

class Registry {
 public:
  Registry(const std::string& daemon, const std::string& build) : daemon_(daemon), build_(build) {}
  Option& add(const Option& o);  // declares an option (value defaults to o.def, origin "default")

  // Command line: for every registered flag spelling present in argv, mark origin "command line" and
  // locked. Does NOT set the value (the daemon's own parser already did that in the live struct).
  void markCommandLine(int argc, char** argv);

  // settings.conf: apply "<daemon>.key=value" to options NOT locked on the command line, by calling
  // write(v) on the live struct. Validates against the option's type/choices/range; a bad or unknown
  // line is logged once and ignored. Returns true if any value changed. On a re-load, a key that is
  // gone from the file is left at its last applied value (file values are sticky; documented).
  bool loadFile(const std::string& path);

  // Re-read the file if its mtime changed since the last load. Returns true if a value changed.
  bool reloadIfChanged(const std::string& path);

  // value = read() for every bound option (call before writeManifest and after a live reload).
  void refreshValues();

  // Startup consistency check (call AFTER markCommandLine + loadFile, BEFORE refreshValues): for each
  // option, compares read() to what it should be (the default for a "default" option, the applied file
  // value for a "file" option). Logs each disagreement to stderr. Returns true if all consistent.
  bool selfCheck() const;

  // Write settings-<daemon>.manifest.plist atomically (temp + rename) into dir.
  bool writeManifest(const std::string& dir) const;

  // Typed access by key (returns the default on an unknown key / parse failure).
  const std::string& get(const std::string& key) const;
  bool getBool(const std::string& key) const;
  long getInt(const std::string& key) const;

  const std::vector<Option>& options() const { return opts_; }
  const std::string& daemon() const { return daemon_; }
  static std::string normBool(const std::string& v);

 private:
  Option* find(const std::string& key);
  const Option* find(const std::string& key) const;
  bool validate(const Option& o, const std::string& v) const;
  bool flagMatches(const Option& o, const std::string& arg) const;  // arg is one of o's spellings
  bool flagTakesValue(const Option& o) const { return o.type != Option::BOOL; }
  std::string daemon_, build_;
  std::vector<Option> opts_;
  long lastMtime_ = 0;
  static const std::string kEmpty;
};

}  // namespace dockreceiver
