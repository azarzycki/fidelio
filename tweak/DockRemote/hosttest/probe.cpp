// B1.16.16: the DockRemote take-or-pass condition as a command, for audiod's host suite remote_target
// (audiod/hosttest/test_remote_target.py): it runs the tweak's own shouldForwardAt() against the files a
// host audiod writes. Usage: dockremote_probe DIR SOCK CODE
//   DIR holds settings.conf, active-source, remote-target and remote-tweak.disabled (audiod's --status-dir).
// Prints "take" or "pass" (and exits 0), or a usage error (exit 2).
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "tweak/DockRemote/DockRemoteCore.h"

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: dockremote_probe DIR SOCK CODE\n"); return 2; }
  std::string d = argv[1];
  std::string disable = d + "/remote-tweak.disabled", conf = d + "/settings.conf", active = d + "/active-source", target = d + "/remote-target";
  dockremote::Paths p = { disable.c_str(), conf.c_str(), active.c_str(), target.c_str(), argv[2] };
  printf("%s\n", dockremote::shouldForwardAt(p, (uint32_t)atoi(argv[3])) ? "take" : "pass");
  return 0;
}
