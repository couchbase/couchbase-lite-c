# Couchbase Lite C

Cross-platform Couchbase Lite SDK with a public C API and inline C++ wrapper API. The repo builds with CMake for macOS, Linux, Windows, and Android, and with `CBL_C.xcodeproj` for Apple platforms.

## Repository Layout

This CE repo expects an optional EE sibling next to it:

```text
<workspace>/
+-- couchbase-lite-c/       # this repo
`-- couchbase-lite-c-ee/    # Enterprise Edition sources, when building EE
```

Key paths:
- `include/cbl/` public C API headers; `include/cbl++/` public C++ wrapper API headers.
- `src/` implementation and platform-specific sources.
- `src/exports/` exported-symbol templates and `generate_exports.sh`.
- `test/` Catch-based C/C++ tests and assets.
- `cmake/` platform build settings and toolchain files.
- `scripts/` local build, packaging, Android, Apple, and coverage scripts.
- `Xcode/` xcconfigs, module map, plist files, and Xcode helper scripts.
- `CBL_C.xcodeproj` Xcode targets and schemes.
- `vendor/couchbase-lite-core/` LiteCore submodule, with nested submodules.
- `../couchbase-lite-c-ee/src/` EE C API source files used when `BUILD_ENTERPRISE=ON`.
- `../couchbase-lite-c-ee/couchbase-lite-core-EE/` EE LiteCore sources used by EE builds.

## Guardrails

- The CMake and Xcode builds are configured separately and must stay in sync. Whenever files are added, removed, or moved, or the project structure changes, **both** `CMakeLists.txt` (sources are listed explicitly) and `CBL_C.xcodeproj` need to be updated. Do not hand-edit `CBL_C.xcodeproj/project.pbxproj`; ask the developer to mirror the change in Xcode.
- Do not edit generated export files in `src/exports/generated` directly. Update the templates and run `src/exports/generate_exports.sh` instead.

## Build And Test

Prefer the narrowest build/test that covers the change.

CMake CE on macOS/Linux:

```bash
cmake -S . -B build_cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build_cmake --target CBL_C_Tests
./build_cmake/test/CBL_C_Tests -r list
```

CMake EE on macOS/Linux, with `../couchbase-lite-c-ee` present:

```bash
cmake -S . -B build_cmake_ee -DCMAKE_BUILD_TYPE=Debug -DBUILD_ENTERPRISE=ON
cmake --build build_cmake_ee --target CBL_C_Tests
./build_cmake_ee/test/CBL_C_Tests -r list
```

Xcode macOS / iOS:

```bash
xcodebuild -list -project CBL_C.xcodeproj
xcodebuild test -project CBL_C.xcodeproj -scheme CBL_Tests -destination "platform=macOS"
xcodebuild test -project CBL_C.xcodeproj -scheme CBL_EE_Tests -configuration Debug_EE -destination "platform=macOS"
xcodebuild test -project CBL_C.xcodeproj -scheme CouchbaseLiteTests-iOS-App -sdk iphonesimulator -destination "<iPhone Simulator Destination>"
```