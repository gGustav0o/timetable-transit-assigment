# Build And Test

Confirmed:
- Build system: CMake >= 3.20 with vcpkg manifest mode. Main targets in root `CMakeLists.txt`:
  - `timetable_core` static library, public include dir `include`, `cxx_std_20`.
  - `timetable-transit-assigment` executable from `src/main.cpp`, linked to `timetable_core`.
  - `timetable_tests` GTest executable under `tests/CMakeLists.txt` when `TIMETABLE_BUILD_TESTS` is enabled.
- Root dependencies from `vcpkg.json`: `tl-expected`, `fmt`, Boost container/unordered/range/graph/circular-buffer, `eigen3`, `vincentlaucsb-csv-parser`, `xlsxio`, `pegtl`, `ftxui`, `spdlog`, `gtest`. Baseline: `39e7b2243343cb39d153a759239dc6a9dfa1f705`.
- `timetable_core` links `mathfp::all`, Boost targets, `fmt::fmt`, `spdlog::spdlog`, CSV parser, xlsxio-read target, and FTXUI component/dom/screen.
- Windows workflow in README/tools:
  - Set `$env:VCPKG_ROOT` to a user-writable vcpkg checkout; manifest mode writes under `$env:VCPKG_ROOT/buildtrees`.
  - Preferred all-tests wrapper: `.	ools	est.ps1`.
  - Fast known-good local run: `.	ools	est.ps1 -ReuseConfigure`.
  - Single test/suite: `.	ools	est.ps1 -TestRegex FriedrichHofsaessWekeckRegression`.
  - Explicit equivalent: `cmake --fresh --preset vcpkg`; `cmake --build --preset windows-msvc-debug`; `ctest --preset windows-msvc-debug`.
- `tools/test.ps1` imports the Visual Studio C++ environment, requires `VCPKG_ROOT`, checks the vcpkg toolchain and writable buildtrees, defaults to fresh configure when supported, then runs CMake build and CTest presets.
- Presets:
  - Configure: `vcpkg`, `vcpkg-no-capacity-stages`, `vcpkg-linux`, `vcpkg-linux-no-capacity-stages`, `code-intel`.
  - Build: `debug`, `release`, `windows-msvc-debug`, `windows-msvc-release`, `release-no-capacity-stages`, `wsl-debug`, `wsl-release`, `wsl-release-no-capacity-stages`, `code-intel`.
  - Test: `windows-msvc-debug`, `windows-msvc-release`, `code-intel`.
- `code-intel` preset uses `build/code-intel`, Ninja, Debug, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and `TIMETABLE_BUILD_TESTS=ON`.
- Test coverage is mostly domain-focused: architecture rules, branch arena, capacity-aware config/diagnostics/exposure/load/penalty/overload, day-path, residual modules, search execution/cost/generation/kernel/problem/projection/pruning/runtime diagnostics, split modules, suffix lower bound, support prefix, plus `mathfp/ranges_zip_tests.cpp`.

Inferences / assumptions:
- For normal coding completion on Windows, run `.	ools	est.ps1` unless task scope justifies a narrower `-TestRegex` first.
- Use `.	ools	est.ps1 -ReuseConfigure` only after the local CMake/vcpkg/MSVC environment is known-good.