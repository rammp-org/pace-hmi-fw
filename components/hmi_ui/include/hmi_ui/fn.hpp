#pragma once
// Fn: what a view's Config holds for "call this": a plain function, or a member function bound to
// the object it runs on. It lets one view built at compile time (constinit) call another without
// a global in between (CS-CMP-02: dependencies come in through the Config).

#include <cstddef>
#include <type_traits>
#include <utility>

namespace hmi::ui {

template <class Signature> class Fn;

/// @brief A non-owning callable: a plain function (or a captureless lambda), or `Method` bound to
///        an object that outlives it. Two pointers and a function pointer; constant-initialisable,
///        no allocation, no captures. Calling an empty Fn calls a null function, as a null
///        function pointer would.
///
/// A plain function converts implicitly, so a Config written with function pointers and
/// captureless lambdas reads as it did:
/// @code
///   ButtonGrid grid{.off_bottom = [] { ... }};
///   ButtonGrid grid{.off_bottom = Fn<void()>::bind<&NavView::to_key>(&nav)};
/// @endcode
template <class R, class... Args> class Fn<R(Args...)> {
public:
  using Plain = R (*)(Args...);

  constexpr Fn() noexcept = default;
  /// Empty, as a null function pointer. Implicit on purpose (explicit(false)), like a pointer's.
  explicit(false) constexpr Fn(std::nullptr_t) noexcept {}
  /// A plain function, or a captureless lambda (anything that converts to one). Implicit, as a
  /// function pointer's conversions are.
  template <class F, class = std::enable_if_t<std::is_convertible_v<F, Plain> &&
                                              !std::is_same_v<std::remove_cvref_t<F>, Fn>>>
  explicit(false) constexpr Fn(F function) noexcept
      : plain_(static_cast<Plain>(function)) {}

  /// @brief `Method` (a member function of T, const or not) called on `object`.
  /// @param object must outlive the Fn; not null.
  template <auto Method, class T> [[nodiscard]] static constexpr Fn bind(T *object) noexcept {
    return Fn(&call<Method, T>, object);
  }

  R operator()(Args... args) const {
    if (bound_ != nullptr) {
      return bound_(object_, std::forward<Args>(args)...);
    }
    return plain_(std::forward<Args>(args)...);
  }

  /// True unless empty.
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return plain_ != nullptr || bound_ != nullptr;
  }

private:
  using Bound = R (*)(void *, Args...);

  constexpr Fn(Bound bound, void *object) noexcept
      : bound_(bound)
      , object_(object) {}

  template <auto Method, class T> static R call(void *object, Args... args) {
    return (static_cast<T *>(object)->*Method)(std::forward<Args>(args)...);
  }

  Plain plain_ = nullptr;
  Bound bound_ = nullptr;
  void *object_ = nullptr;
};

namespace fn_detail {
template <class M> struct Signature;
template <class R, class T, class... A> struct Signature<R (T::*)(A...)> { using type = R(A...); };
template <class R, class T, class... A> struct Signature<R (T::*)(A...) const> {
  using type = R(A...);
};
template <class R, class T, class... A> struct Signature<R (T::*)(A...) noexcept> {
  using type = R(A...);
};
template <class R, class T, class... A> struct Signature<R (T::*)(A...) const noexcept> {
  using type = R(A...);
};
} // namespace fn_detail

/// @brief `Method` bound to `object`, as an Fn of the method's own signature:
///        `bind<&NavView::to_key>(&nav)` is an `Fn<void()>`.
template <auto Method, class T> [[nodiscard]] constexpr auto bind(T *object) noexcept {
  using Sig = typename fn_detail::Signature<decltype(Method)>::type;
  return Fn<Sig>::template bind<Method>(object);
}

} // namespace hmi::ui
