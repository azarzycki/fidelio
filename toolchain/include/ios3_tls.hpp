// ios3_tls.hpp - per-thread variables for iPhone OS 3 (no thread_local/__thread: clang only
// allows TLS on 32-bit iOS >= 9, and iOS 3 dyld has no TLV support). Uses pthread keys.
//
//   static ios3::thread_local_var<int> counter;       // instead of: thread_local int counter = 0;
//   counter.get()++;                                  // value-initialised on first use per thread
//   static ios3::thread_local_var<Foo> foo{[]{ return Foo(1,2); }};   // custom initialiser
#pragma once
#include <pthread.h>
#include <functional>
#include <utility>

namespace ios3 {
template <class T> class thread_local_var {
public:
    thread_local_var() : init_([] { return T(); }) { pthread_key_create(&key_, &destroy); }
    explicit thread_local_var(std::function<T()> init) : init_(std::move(init)) { pthread_key_create(&key_, &destroy); }
    thread_local_var(const thread_local_var &) = delete;
    thread_local_var &operator=(const thread_local_var &) = delete;
    // Note: the key is never deleted (thread_local objects live for the whole program).
    T &get() {
        void *p = pthread_getspecific(key_);
        if (!p) { p = new T(init_()); pthread_setspecific(key_, p); }
        return *static_cast<T *>(p);
    }
    T &operator*() { return get(); }
    T *operator->() { return &get(); }
    thread_local_var &operator=(const T &v) { get() = v; return *this; }
    operator T &() { return get(); }
private:
    static void destroy(void *p) { delete static_cast<T *>(p); }
    pthread_key_t key_;
    std::function<T()> init_;
};
} // namespace ios3
