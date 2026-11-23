// NowPlaying - dock display for cspot-ios3 on iPhone OS 3.1 (armv6).
#import <UIKit/UIKit.h>
#import "NPAppDelegate.h"
#import "NPEventProbe.h"

int main(int argc, char* argv[]) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  int r = UIApplicationMain(argc, argv, @"NPApplication", @"NPAppDelegate");
  [pool release];
  return r;
}
