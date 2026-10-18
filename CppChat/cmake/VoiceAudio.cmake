include(FetchContent)
# Xiph's unmodified, BSD licensed reference codec. No experimental neural models.
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cppchat_opus
  URL https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
  URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(cppchat_opus)
set(CPPCHAT_OPUS_SOURCE "${cppchat_opus_SOURCE_DIR}" CACHE INTERNAL "Pinned Opus sources")
