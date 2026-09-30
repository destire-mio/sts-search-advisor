#pragma once
#include <atomic>
#include <chrono>
#include <stdexcept>
namespace advisor {
struct Interrupted : std::runtime_error { using std::runtime_error::runtime_error; };
struct Control {
    std::atomic<bool> cancelled{false};
    std::chrono::steady_clock::time_point deadline;
};
inline thread_local Control *control = nullptr;
inline void poll() {
    if (!control) return;
    if (control->cancelled.load()) throw Interrupted("CANCELLED");
    if (std::chrono::steady_clock::now() >= control->deadline) throw Interrupted("TIMEOUT");
}
}
