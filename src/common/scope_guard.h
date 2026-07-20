#pragma once

#include <utility>

namespace pdfimg {

template <typename Fn>
class ScopeGuard {
 public:
  explicit ScopeGuard(Fn fn) noexcept : fn_(std::move(fn)) {}
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& other) noexcept
      : fn_(std::move(other.fn_)), active_(other.active_) {
    other.active_ = false;
  }
  ~ScopeGuard() noexcept {
    if (active_) fn_();
  }
  void Dismiss() noexcept { active_ = false; }

 private:
  Fn fn_;
  bool active_ = true;
};

template <typename Fn>
ScopeGuard<Fn> MakeScopeGuard(Fn fn) noexcept {
  return ScopeGuard<Fn>(std::move(fn));
}

}  // namespace pdfimg

