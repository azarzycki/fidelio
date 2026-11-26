#pragma once
// Pure decision for a Zeroconf add-user POST, kept free of any cspot/bell type so it can be unit
// tested on the host (spotify/hosttest/user_switch_test.cpp).
//
// cspot's blob is create-once-run-forever (every bell::Task thread is detached, there is no hard
// join), so a different account cannot be switched in place. Instead the receiver stays discoverable
// whenever it is not actively playing: a different account's add-user is honoured by persisting that
// account's credentials and exiting cleanly, which launchd relaunches to log in as the new account.
// While a song is playing the same POST is refused fast so the other phone shows "couldn't connect"
// instead of spinning. A re-add by the account already logged in is always fine.
#include <string>

namespace cspot {

enum class PostDecision {
  FirstLogin,       // not logged in yet: accept this as the first account (today's path)
  SameUserOk,       // already logged in as this same account: a re-add, answer OK
  SwitchToNewUser,  // a different account while idle: honour the switch (persist creds + relaunch)
  RefuseBusy,       // a different account while actively playing: fast-fail, do not switch
};

// playing       - is the account currently logged in actively playing right now?
// authenticated - is the receiver already logged in to an account?
// currentUser   - the account the receiver is logged in as (empty when not authenticated)
// postUser      - the account named in this add-user POST
inline PostDecision postDecision(bool playing, bool authenticated,
                                 const std::string& currentUser,
                                 const std::string& postUser) {
  if (!authenticated) return PostDecision::FirstLogin;
  if (postUser == currentUser) return PostDecision::SameUserOk;
  if (playing) return PostDecision::RefuseBusy;
  return PostDecision::SwitchToNewUser;
}

}  // namespace cspot
