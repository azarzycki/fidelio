// B1.16: send one transport verb to audiod via the shared control helper (the same path the app and
// the DockRemote tweak use). Usage: control_send SOCK VERB(1..5)
#include <stdio.h>
#include <stdlib.h>
#include "AudiodControl.h"
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: control_send SOCK VERB\n"); return 2; }
  int ok = audiod_send_control(argv[1], atoi(argv[2]), 500);
  printf("control_send verb %s -> %s\n", argv[2], ok ? "sent" : "failed");
  return ok ? 0 : 1;
}
