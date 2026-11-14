// fidelio-lockctl: enable / disable a SpringBoard lock-screen bundle by name, or report status, by
// calling the private SpringBoardServices MIG routine SBEnableLockScreenBundle through dlopen/dlsym
// (never linked), exactly as audiod's DisplayWakeApple.cpp calls its SpringBoardServices functions.
// Lets Fidelio.lockbundle be tested by hand without injecting anything into SpringBoard.
//
// Prototype derived by disassembling the 3.1.2 SDK SpringBoardServices stub (B1.11): three args,
//   kern_return_t SBEnableLockScreenBundle(mach_port_t serverPort, uint8_t enable, const char* name);
// r0 = port, r1 = a byte (enable flag), r2 = the bundle name (mig_strncpy'd). Confirm on the device.
#include <dlfcn.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

typedef mach_port_t (*ServerPortFn)(void);
typedef kern_return_t (*EnableFn)(mach_port_t, unsigned char, const char*);

static const char* kPlugins = "/System/Library/SpringBoardPlugins";

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: fidelio-lockctl enable NAME | disable NAME | status [NAME]\n"
                    "  NAME is the bundle name without .lockbundle (default Fidelio)\n");
    return 2;
  }
  std::string cmd = argv[1];
  const char* name = argc > 2 ? argv[2] : "Fidelio";

  void* sbs = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_LAZY);
  if (!sbs) { fprintf(stderr, "dlopen SpringBoardServices failed: %s\n", dlerror() ? dlerror() : "?"); return 1; }
  ServerPortFn serverPort = (ServerPortFn)dlsym(sbs, "SBSSpringBoardServerPort");
  EnableFn enableBundle = (EnableFn)dlsym(sbs, "SBEnableLockScreenBundle");

  if (cmd == "status") {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.lockbundle", kPlugins, name);
    struct stat st;
    bool installed = stat(path, &st) == 0;
    printf("bundle %s.lockbundle: %s\n", name, installed ? path : "NOT installed");
    printf("SBSSpringBoardServerPort: %s\n", serverPort ? "resolved" : "MISSING");
    printf("SBEnableLockScreenBundle: %s\n", enableBundle ? "resolved" : "MISSING");
    printf("(there is no exported getter for the enabled state; use enable/disable and watch the lock screen)\n");
    return 0;
  }
  bool enable = cmd == "enable";
  if (!enable && cmd != "disable") { fprintf(stderr, "unknown command '%s'\n", cmd.c_str()); return 2; }
  if (!serverPort || !enableBundle) { fprintf(stderr, "SpringBoardServices symbols missing (SBSSpringBoardServerPort/SBEnableLockScreenBundle)\n"); return 1; }
  mach_port_t port = serverPort();
  if (!port) { fprintf(stderr, "SBSSpringBoardServerPort returned 0 (SpringBoard not reachable)\n"); return 1; }
  kern_return_t kr = enableBundle(port, enable ? 1 : 0, name);
  printf("SBEnableLockScreenBundle(port, %s, \"%s\") -> %d (%s)\n", enable ? "enable" : "disable", name, (int)kr,
         kr == KERN_SUCCESS ? "ok" : mach_error_string(kr));
  return kr == KERN_SUCCESS ? 0 : 1;
}
