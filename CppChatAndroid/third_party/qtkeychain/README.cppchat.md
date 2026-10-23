# QtKeychain

Upstream: https://github.com/frankosterfeld/qtkeychain
Version: 0.17.0
Commit: 875f77d9f61bd97fd84cca47ce3bc71186dfbd09
License: BSD-3-Clause (COPYING and per-file notices retained).

Only upstream library/build sources are vendored; demos, translations and upstream tests are not built.
CppChat links the static library on Windows and Android with Qt 6.12.0; its MobileAccount adapter disables insecure fallback.
No upstream source changes. Host integration tests cover persistence, account isolation, logout and deletion; Android Keystore still requires device validation.
