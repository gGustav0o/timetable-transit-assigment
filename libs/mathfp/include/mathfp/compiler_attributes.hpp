#pragma once

// --- Feature-test helpers ----------------------------------------------------
#ifndef MATHFP_HAS_CPP_ATTR
#  if defined(__has_cpp_attribute)
#    define MATHFP_HAS_CPP_ATTR(x) __has_cpp_attribute(x)
#  else
#    define MATHFP_HAS_CPP_ATTR(x) 0
#  endif
#endif

#ifndef MATHFP_HAS_ATTR
#  if defined(__has_attribute)
#    define MATHFP_HAS_ATTR(x) __has_attribute(x)
#  else
#    define MATHFP_HAS_ATTR(x) 0
#  endif
#endif

#ifndef MATHFP_HAS_DECLSPEC_ATTR
#  if defined(__has_declspec_attribute)
#    define MATHFP_HAS_DECLSPEC_ATTR(x) __has_declspec_attribute(x)
#  else
#    define MATHFP_HAS_DECLSPEC_ATTR(x) 0
#  endif
#endif

#ifndef MATHFP_HAS_BUILTIN
#  if defined(__has_builtin)
#    define MATHFP_HAS_BUILTIN(x) __has_builtin(x)
#  else
#    define MATHFP_HAS_BUILTIN(x) 0
#  endif
#endif

#ifndef MATHFP_STRINGIFY
#  define MATHFP_STRINGIFY_IMPL(x) #x
#  define MATHFP_STRINGIFY(x) MATHFP_STRINGIFY_IMPL(x)
#endif

#ifndef MATHFP_CPLUSPLUS
#  if defined(_MSVC_LANG)
#    define MATHFP_CPLUSPLUS _MSVC_LANG
#  else
#    define MATHFP_CPLUSPLUS __cplusplus
#  endif
#endif

// --- Compiler detection ------------------------------------------------------
#if defined(_MSC_VER)
#  define MATHFP_COMPILER_MSVC 1
#else
#  define MATHFP_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#  define MATHFP_COMPILER_CLANG 1
#else
#  define MATHFP_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !MATHFP_COMPILER_CLANG
#  define MATHFP_COMPILER_GCC 1
#else
#  define MATHFP_COMPILER_GCC 0
#endif

#if defined(__INTEL_COMPILER) || defined(__INTEL_LLVM_COMPILER)
#  define MATHFP_COMPILER_INTEL 1
#else
#  define MATHFP_COMPILER_INTEL 0
#endif

#if defined(__CUDACC__) || defined(__CUDA_ARCH__)
#  define MATHFP_COMPILER_NVCC 1
#else
#  define MATHFP_COMPILER_NVCC 0
#endif

// --- Branch prediction hints -------------------------------------------------
#if (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define MATHFP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define MATHFP_LIKELY(x)   (x)
#  define MATHFP_UNLIKELY(x) (x)
#endif

// --- Function hot/cold sections ---------------------------------------------
#if (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_HOT   __attribute__((hot))
#  define MATHFP_COLD  __attribute__((cold))
#else
#  define MATHFP_HOT
#  define MATHFP_COLD
#endif

// --- Force-inline / noinline -------------------------------------------------
#if MATHFP_COMPILER_MSVC
#  define MATHFP_FORCEINLINE __forceinline
#  define MATHFP_NOINLINE    __declspec(noinline)
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_FORCEINLINE inline __attribute__((always_inline))
#  define MATHFP_NOINLINE    __attribute__((noinline))
#else
#  define MATHFP_FORCEINLINE inline
#  define MATHFP_NOINLINE
#endif

// --- Standard-ish attributes -------------------------------------------------
// [[nodiscard]] / warn_unused_result
#if MATHFP_HAS_CPP_ATTR(nodiscard) || (MATHFP_CPLUSPLUS >= 201703L)
#  define MATHFP_NODISCARD [[nodiscard]]
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_NODISCARD __attribute__((warn_unused_result))
#elif MATHFP_COMPILER_MSVC
#  define MATHFP_NODISCARD _Check_return_
#else
#  define MATHFP_NODISCARD
#endif

// [[noreturn]]
#if MATHFP_HAS_CPP_ATTR(noreturn) || (MATHFP_CPLUSPLUS >= 201103L)
#  define MATHFP_NORETURN [[noreturn]]
#elif MATHFP_COMPILER_MSVC
#  define MATHFP_NORETURN __declspec(noreturn)
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_NORETURN __attribute__((noreturn))
#else
#  define MATHFP_NORETURN
#endif

// [[deprecated("msg")]]
#if MATHFP_HAS_CPP_ATTR(deprecated) || (MATHFP_CPLUSPLUS >= 201402L)
#  define MATHFP_DEPRECATED(msg) [[deprecated(msg)]]
#  define MATHFP_DEPRECATED_     [[deprecated]]
#elif MATHFP_COMPILER_MSVC
#  define MATHFP_DEPRECATED(msg) __declspec(deprecated(msg))
#  define MATHFP_DEPRECATED_
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_DEPRECATED(msg) __attribute__((deprecated(msg)))
#  define MATHFP_DEPRECATED_
#else
#  define MATHFP_DEPRECATED(msg)
#  define MATHFP_DEPRECATED_
#endif

// [[fallthrough]] (C++17) – use no-op when unavailable
#if MATHFP_HAS_CPP_ATTR(fallthrough) || (MATHFP_CPLUSPLUS >= 201703L)
#  define MATHFP_FALLTHROUGH [[fallthrough]]
#elif MATHFP_HAS_CPP_ATTR(gnu::fallthrough)
#  define MATHFP_FALLTHROUGH [[gnu::fallthrough]]
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_FALLTHROUGH __attribute__((fallthrough))
#else
#  define MATHFP_FALLTHROUGH ((void)0)
#endif

// [[likely]] / [[unlikely]] (C++20) – statement attributes; prefer MATHFP_LIKELY/UNLIKELY for expressions
#if MATHFP_HAS_CPP_ATTR(likely) && (MATHFP_CPLUSPLUS >= 202002L)
#  define MATHFP_ATTR_LIKELY [[likely]]
#else
#  define MATHFP_ATTR_LIKELY
#endif
#if MATHFP_HAS_CPP_ATTR(unlikely) && (MATHFP_CPLUSPLUS >= 202002L)
#  define MATHFP_ATTR_UNLIKELY [[unlikely]]
#else
#  define MATHFP_ATTR_UNLIKELY
#endif

// --- Purity & const-ness (no observable state changes) ----------------------
#if (defined(IMAGEFX_NO_DEBUG) || defined(NDEBUG)) && (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_PURE      __attribute__((pure))      // depends only on args / global state, no side effects
#  define MATHFP_CONST_FN  __attribute__((const))     // depends only on args, no global memory access
#else
#  define MATHFP_PURE
#  define MATHFP_CONST_FN
#endif

// --- malloc-like result (non-aliasing, fresh memory) ------------------------
#if (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_MALLOC_FN __attribute__((malloc))
#else
#  define MATHFP_MALLOC_FN
#endif

// --- Alignment of declarations ----------------------------------------------
#if MATHFP_COMPILER_MSVC
#  define MATHFP_ALIGNED(n) __declspec(align(n))
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_ALIGNED(n) __attribute__((aligned(n)))
#else
#  define MATHFP_ALIGNED(n)
#endif

// --- Restrict keyword (pointer aliasing hint) -------------------------------
#if MATHFP_COMPILER_MSVC
#  define MATHFP_RESTRICT __restrict
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_RESTRICT __restrict__
#else
#  define MATHFP_RESTRICT
#endif

// --- Unreachable & assume ----------------------------------------------------
#if MATHFP_COMPILER_MSVC
#  define MATHFP_UNREACHABLE() __assume(0)
#  define MATHFP_ASSUME(cond)  __assume(cond)
#elif MATHFP_COMPILER_CLANG
#  define MATHFP_UNREACHABLE() __builtin_unreachable()
#  if MATHFP_HAS_BUILTIN(__builtin_assume)
#    define MATHFP_ASSUME(cond) __builtin_assume(cond)
#  else
#    define MATHFP_ASSUME(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
#  endif
#elif MATHFP_COMPILER_GCC
#  define MATHFP_UNREACHABLE() __builtin_unreachable()
#  define MATHFP_ASSUME(cond)  do { if (!(cond)) __builtin_unreachable(); } while(0)
#else
#  define MATHFP_UNREACHABLE() ((void)0)
#  define MATHFP_ASSUME(cond)  ((void)0)
#endif

// --- Format checking (printf/scanf style) -----------------------------------
#if (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_FORMAT(archetype, fmt_index, vararg_index) __attribute__((format(archetype, fmt_index, vararg_index)))
#else
#  define MATHFP_FORMAT(archetype, fmt_index, vararg_index)
#endif

// --- Visibility / DLL export/import -----------------------------------------
#if MATHFP_COMPILER_MSVC
#  define MATHFP_DLL_EXPORT __declspec(dllexport)
#  define MATHFP_DLL_IMPORT __declspec(dllimport)
#  define MATHFP_DLL_LOCAL
#elif (MATHFP_COMPILER_GCC || MATHFP_COMPILER_CLANG)
#  define MATHFP_DLL_EXPORT __attribute__((visibility("default")))
#  define MATHFP_DLL_IMPORT __attribute__((visibility("default")))
#  define MATHFP_DLL_LOCAL  __attribute__((visibility("hidden")))
#else
#  define MATHFP_DLL_EXPORT
#  define MATHFP_DLL_IMPORT
#  define MATHFP_DLL_LOCAL
#endif

// --- Warning control (push/pop & disable) -----------------------------------
#if MATHFP_COMPILER_CLANG
#  define MATHFP_DIAGNOSTIC_PUSH _Pragma("clang diagnostic push")
#  define MATHFP_DIAGNOSTIC_POP  _Pragma("clang diagnostic pop")
#  define MATHFP_DIAGNOSTIC_IGNORE(w) _Pragma(MATHFP_STRINGIFY(clang diagnostic ignored w)) // pass string literal: "-W..."
#elif MATHFP_COMPILER_GCC
#  define MATHFP_DIAGNOSTIC_PUSH _Pragma("GCC diagnostic push")
#  define MATHFP_DIAGNOSTIC_POP  _Pragma("GCC diagnostic pop")
#  define MATHFP_DIAGNOSTIC_IGNORE(w) _Pragma(MATHFP_STRINGIFY(GCC diagnostic ignored w))
#elif MATHFP_COMPILER_MSVC
#  define MATHFP_DIAGNOSTIC_PUSH __pragma(warning(push))
#  define MATHFP_DIAGNOSTIC_POP  __pragma(warning(pop))
#  define MATHFP_DIAGNOSTIC_IGNORE(n) __pragma(warning(disable: n)) // pass number, e.g. 4996
#else
#  define MATHFP_DIAGNOSTIC_PUSH
#  define MATHFP_DIAGNOSTIC_POP
#  define MATHFP_DIAGNOSTIC_IGNORE(x)
#endif

// --- Flatten / target specific (best-effort) --------------------------------
#if MATHFP_COMPILER_GCC
#  define MATHFP_FLATTEN __attribute__((flatten))
#else
#  define MATHFP_FLATTEN
#endif

// Example for x86 vectorcall on MSVC & Clang-cl; no-op elsewhere.
#if MATHFP_COMPILER_MSVC
#  define MATHFP_VECTORCALL __vectorcall
#else
#  define MATHFP_VECTORCALL
#endif


#if MATHFP_COMPILER_MSVC
#  define MATHFP_FUNCTION_SIGNATURE __FUNCSIG__
#else
#  define MATHFP_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif
