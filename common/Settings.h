#pragma once
// B1.14: each daemon declares its options ONCE in a Settings registry. The registry parses the command
// line and /var/mobile/fidelio/settings.conf (precedence: command line > settings.conf > default),
// writes a manifest the NowPlaying app renders a config UI from, and re-reads settings.conf when it
// changes so live options take effect without a restart. Plain C++ (host + ARMv6 device), no exceptions.
//
// settings.conf is UTF-8 "daemon.key=value" lines (ssh-editable); unknown keys / invalid values are
// ignored with one log line and never stop a daemon. The manifest is settings-<daemon>.manifest.plist
// (a property list NSDictionary/NSArray load on iOS 3.1), written atomically.
#include <stdint.h>

#include <string>
#include <vector>

namespace fidelio {

struct Choice { std::string value, label; };

struct Option {
  std::string key;          // stable id, also the settings.conf key (after "<daemon>.")
  std::string flag;         // command-line flag, e.g. "--max-volume" ("" = no flag)
  enum Type { BOOL, ENUM, INT, PERCENT } type = BOOL;
  std::vector<Choice> choices;  // ENUM
  long imin = 0, imax = 100, istep = 1;  // INT / PERCENT
  std::string unit;         // INT: "ms", "kbps", ...
  std::string def;          // default value as a string
  std::string title;        // short UI title
  std::string desc;         // one sentence
  std::string group;        // "Volume", "Source switching", "Display", "AirPlay", "Spotify", "Advanced"
  bool live = true;         // applies live (true) or needs a restart of this daemon (false)
  bool advanced = false;    // hidden behind the "advanced" disclosure

  // runtime
  std::string value;        // current effective value
  std::string origin;       // "default" | "file" | "command line"
  bool locked = false;      // fixed on the command line: the app shows it but cannot change it
};

class Registry {
 public:
  Registry(const std::string& daemon, const std::string& build) : daemon_(daemon), build_(build) {}
  Option& add(const Option& o);  // declares an option (value defaults to o.def, origin "default")

  // Command line: for each registered flag present in argv, set value + origin "command line" + locked.
  // Unknown flags are left for the daemon's own parser (returns them). Bool flags take on|off; others a value.
  void parseArgv(int argc, char** argv);

  // settings.conf: apply "<daemon>.key=value" lines to options NOT locked on the command line.
  // Validates against the option's type/choices/range; a bad or unknown line is logged once, ignored.
  // Returns true if any value changed.
  bool loadFile(const std::string& path);

  // Re-read the file if its mtime changed since the last load. Returns true if a value changed (so the
  // caller re-applies the live options and rewrites the manifest).
  bool reloadIfChanged(const std::string& path);

  // Write settings-<daemon>.manifest.plist atomically (temp + rename) into dir.
  bool writeManifest(const std::string& dir) const;

  // Typed access by key (returns the default on an unknown key / parse failure).
  const std::string& get(const std::string& key) const;
  bool getBool(const std::string& key) const;
  long getInt(const std::string& key) const;

  const std::vector<Option>& options() const { return opts_; }
  const std::string& daemon() const { return daemon_; }

 private:
  Option* find(const std::string& key);
  const Option* find(const std::string& key) const;
  bool validate(const Option& o, const std::string& v) const;
  std::string daemon_, build_;
  std::vector<Option> opts_;
  long lastMtime_ = 0;
  static const std::string kEmpty;
};

}  // namespace fidelio
