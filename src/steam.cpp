#include "steam.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle open_resident_steam(std::string &path) {
    const char *names[] = {"steam_api64.dll", "steam_api.dll"};
    for (const char *name : names) {
        if (LibraryHandle handle = GetModuleHandleA(name)) {
            path = name;
            return handle;
        }
    }
    return nullptr;
}

void *resolve_symbol(LibraryHandle lib, const char *name) {
    return reinterpret_cast<void *>(GetProcAddress(lib, name));
}

std::string last_os_error() {
    return "Win32 error " + std::to_string(GetLastError());
}
#else
using LibraryHandle = void *;

void remember_steam_path(const std::string &candidate, std::string &path, LibraryHandle &handle) {
    if (candidate.empty() || handle != nullptr) {
        return;
    }
    std::string cleaned = candidate;
    const std::string deleted = " (deleted)";
    if (cleaned.size() >= deleted.size() &&
        cleaned.compare(cleaned.size() - deleted.size(), deleted.size(), deleted) == 0) {
        cleaned.resize(cleaned.size() - deleted.size());
    }
    if (cleaned.find("libsteam_api.so") == std::string::npos) {
        return;
    }

    // NOLOAD returns the mapping srcds already created. A normal dlopen can
    // map a second, uninitialized libsteam_api and crash on the first call.
    if (void *opened = dlopen(cleaned.c_str(), RTLD_NOLOAD | RTLD_LAZY | RTLD_LOCAL)) {
        path = std::move(cleaned);
        handle = opened;
    }
}

LibraryHandle open_resident_steam(std::string &path) {
    LibraryHandle handle = nullptr;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const auto slash = line.find('/');
        if (slash == std::string::npos) {
            continue;
        }
        remember_steam_path(line.substr(slash), path, handle);
        if (handle != nullptr) {
            return handle;
        }
    }

    const char *names[] = {
        "libsteam_api.so",
        "bin/linux64/libsteam_api.so",
        "bin/linux32/libsteam_api.so",
        "bin/libsteam_api.so",
    };
    for (const char *name : names) {
        remember_steam_path(name, path, handle);
        if (handle != nullptr) {
            return handle;
        }
    }
    return nullptr;
}

void *resolve_symbol(LibraryHandle lib, const char *name) {
    dlerror();
    return dlsym(lib, name);
}

std::string last_os_error() {
    const char *err = dlerror();
    return err ? err : "unknown error";
}
#endif

} // namespace

template <typename T>
bool SteamApi::bind(const char *name, T &out) {
    void *symbol = resolve_symbol(static_cast<LibraryHandle>(library_), name);
    if (!symbol) {
        error_ = std::string("missing '") + name + "' in " + library_path_ + " (" + last_os_error() +
                 "). Refusing to call a missing Steam entry point.";
        out = nullptr;
        return false;
    }
    out = reinterpret_cast<T>(symbol);
    return true;
}

bool SteamApi::load() {
    if (ready_) {
        return true;
    }

    error_.clear();
    library_path_.clear();
    library_ = open_resident_steam(library_path_);
    if (!library_) {
        error_ = "could not find the dedicated server's already-loaded libsteam_api. "
                 "Refusing to load a second copy.";
        return false;
    }

    const bool ok =
        bind("SteamAPI_SteamGameServerUGC_v021", steam_gameserver_ugc_) &&
        bind("SteamAPI_SteamGameServerUtils_v010", steam_gameserver_utils_) &&
        bind("SteamAPI_SteamGameServer_v015", steam_gameserver_) &&
        bind("SteamAPI_ISteamGameServer_BLoggedOn", blogged_on_) &&
        bind("SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest", create_query_) &&
        bind("SteamAPI_ISteamUGC_SendQueryUGCRequest", send_query_) &&
        bind("SteamAPI_ISteamUGC_SetAllowCachedResponse", set_cached_) &&
        bind("SteamAPI_ISteamUGC_SetReturnChildren", set_children_) &&
        bind("SteamAPI_ISteamUGC_SetReturnLongDescription", set_long_description_) &&
        bind("SteamAPI_ISteamUGC_GetQueryUGCResult", get_result_) &&
        bind("SteamAPI_ISteamUGC_GetQueryUGCChildren", get_children_) &&
        bind("SteamAPI_ISteamUGC_ReleaseQueryUGCRequest", release_query_) &&
        bind("SteamAPI_ISteamUGC_DownloadItem", download_item_) &&
        bind("SteamAPI_ISteamUGC_SuspendDownloads", suspend_downloads_) &&
        bind("SteamAPI_ISteamUGC_GetItemState", item_state_) &&
        bind("SteamAPI_ISteamUGC_GetItemInstallInfo", item_install_info_) &&
        bind("SteamAPI_ISteamUtils_IsAPICallCompleted", is_call_completed_) &&
        bind("SteamAPI_ISteamUtils_GetAPICallResult", get_call_result_);

    if (!ok) {
        library_ = nullptr;
        return false;
    }

    ready_ = true;
    return true;
}

bool SteamApi::logged_on() const {
    if (!ready_ || steam_gameserver_ == nullptr || blogged_on_ == nullptr) {
        return false;
    }
    void *server = steam_gameserver_();
    return server != nullptr && blogged_on_(server);
}

void *SteamApi::ugc() const {
    if (!ready_ || steam_gameserver_ugc_ == nullptr) {
        return nullptr;
    }
    return steam_gameserver_ugc_();
}

void *SteamApi::utils() const {
    if (!ready_ || steam_gameserver_utils_ == nullptr) {
        return nullptr;
    }
    return steam_gameserver_utils_();
}

uint64_t SteamApi::create_details_query(void *ugc, uint64_t published_file_id) const {
    if (ugc == nullptr || create_query_ == nullptr) {
        return kUgcQueryHandleInvalid;
    }
    return create_query_(ugc, &published_file_id, 1);
}

uint64_t SteamApi::send_query(void *ugc, uint64_t handle) const {
    if (ugc == nullptr || send_query_ == nullptr) {
        return 0;
    }
    return send_query_(ugc, handle);
}

bool SteamApi::set_allow_cached_response(void *ugc, uint64_t handle, uint32_t max_age_seconds) const {
    if (ugc == nullptr || set_cached_ == nullptr) {
        return false;
    }
    return set_cached_(ugc, handle, max_age_seconds);
}

bool SteamApi::set_return_children(void *ugc, uint64_t handle, bool enabled) const {
    if (ugc == nullptr || set_children_ == nullptr) {
        return false;
    }
    return set_children_(ugc, handle, enabled);
}

bool SteamApi::set_return_long_description(void *ugc, uint64_t handle, bool enabled) const {
    if (ugc == nullptr || set_long_description_ == nullptr) {
        return false;
    }
    return set_long_description_(ugc, handle, enabled);
}

bool SteamApi::get_query_result(void *ugc,
                                uint64_t handle,
                                uint32_t index,
                                SteamUGCDetails_t *out) const {
    if (ugc == nullptr || get_result_ == nullptr || out == nullptr) {
        return false;
    }
    return get_result_(ugc, handle, index, out);
}

bool SteamApi::get_query_children(void *ugc,
                                  uint64_t handle,
                                  uint32_t index,
                                  uint64_t *ids,
                                  uint32_t count) const {
    if (ugc == nullptr || get_children_ == nullptr || ids == nullptr || count == 0) {
        return false;
    }
    return get_children_(ugc, handle, index, ids, count);
}

void SteamApi::release_query(void *ugc, uint64_t handle) const {
    if (ugc == nullptr || release_query_ == nullptr || handle == kUgcQueryHandleInvalid) {
        return;
    }
    release_query_(ugc, handle);
}

bool SteamApi::download_item(void *ugc, uint64_t published_file_id, bool high_priority) const {
    if (ugc == nullptr || download_item_ == nullptr) {
        return false;
    }
    return download_item_(ugc, published_file_id, high_priority);
}

void SteamApi::suspend_downloads(void *ugc, bool suspend) const {
    if (ugc == nullptr || suspend_downloads_ == nullptr) {
        return;
    }
    suspend_downloads_(ugc, suspend);
}

uint32_t SteamApi::item_state(void *ugc, uint64_t published_file_id) const {
    if (ugc == nullptr || item_state_ == nullptr) {
        return 0;
    }
    return item_state_(ugc, published_file_id);
}

bool SteamApi::item_install_info(void *ugc,
                                 uint64_t published_file_id,
                                 char *folder,
                                 uint32_t cch) const {
    if (ugc == nullptr || item_install_info_ == nullptr || folder == nullptr || cch == 0) {
        return false;
    }
    uint64_t size_on_disk = 0;
    uint32_t timestamp = 0;
    return item_install_info_(ugc, published_file_id, &size_on_disk, folder, cch, &timestamp);
}

bool SteamApi::is_api_call_completed(void *utils, uint64_t call, bool &failed) const {
    failed = false;
    if (utils == nullptr || is_call_completed_ == nullptr || call == 0) {
        return false;
    }
    return is_call_completed_(utils, call, &failed);
}

bool SteamApi::get_api_call_result(void *utils,
                                   uint64_t call,
                                   void *callback,
                                   int callback_size,
                                   int callback_id,
                                   bool &failed) const {
    failed = false;
    if (utils == nullptr || get_call_result_ == nullptr || callback == nullptr || call == 0) {
        return false;
    }
    return get_call_result_(utils, call, callback, callback_size, callback_id, &failed);
}

SteamApi &steam_api() {
    static SteamApi api;
    return api;
}
