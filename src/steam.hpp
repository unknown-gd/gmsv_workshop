#pragma once

#include "steam_types.hpp"

#include <cstdint>
#include <string>

// Talks to the dedicated server's already-loaded libsteam_api. Never loads a
// second copy and never registers a Steam callback dispatcher.
class SteamApi {
public:
    bool load();
    bool ready() const { return ready_; }
    const std::string &error() const { return error_; }
    const std::string &library_path() const { return library_path_; }

    bool logged_on() const;
    void *ugc() const;
    void *utils() const;

    uint64_t create_details_query(void *ugc, uint64_t published_file_id) const;
    uint64_t send_query(void *ugc, uint64_t handle) const;
    bool set_allow_cached_response(void *ugc, uint64_t handle, uint32_t max_age_seconds) const;
    bool set_return_children(void *ugc, uint64_t handle, bool enabled) const;
    bool set_return_long_description(void *ugc, uint64_t handle, bool enabled) const;
    bool get_query_result(void *ugc, uint64_t handle, uint32_t index, SteamUGCDetails_t *out) const;
    bool get_query_children(void *ugc,
                            uint64_t handle,
                            uint32_t index,
                            uint64_t *ids,
                            uint32_t count) const;
    void release_query(void *ugc, uint64_t handle) const;

    bool download_item(void *ugc, uint64_t published_file_id, bool high_priority) const;
    void suspend_downloads(void *ugc, bool suspend) const;
    uint32_t item_state(void *ugc, uint64_t published_file_id) const;
    bool item_install_info(void *ugc, uint64_t published_file_id, char *folder, uint32_t cch) const;

    bool is_api_call_completed(void *utils, uint64_t call, bool &failed) const;
    bool get_api_call_result(void *utils,
                             uint64_t call,
                             void *callback,
                             int callback_size,
                             int callback_id,
                             bool &failed) const;

private:
    using GameServerUGCFn = void *(*)();
    using GameServerUtilsFn = void *(*)();
    using GameServerFn = void *(*)();
    using BLoggedOnFn = bool (*)(void *);
    using CreateQueryFn = uint64_t (*)(void *, uint64_t *, uint32_t);
    using SendQueryFn = uint64_t (*)(void *, uint64_t);
    using SetCachedFn = bool (*)(void *, uint64_t, uint32_t);
    using SetBoolFn = bool (*)(void *, uint64_t, bool);
    using GetResultFn = bool (*)(void *, uint64_t, uint32_t, SteamUGCDetails_t *);
    using GetChildrenFn = bool (*)(void *, uint64_t, uint32_t, uint64_t *, uint32_t);
    using ReleaseQueryFn = bool (*)(void *, uint64_t);
    using DownloadItemFn = bool (*)(void *, uint64_t, bool);
    using SuspendDownloadsFn = void (*)(void *, bool);
    using ItemStateFn = uint32_t (*)(void *, uint64_t);
    using ItemInstallInfoFn = bool (*)(void *, uint64_t, uint64_t *, char *, uint32_t, uint32_t *);
    using IsCallCompletedFn = bool (*)(void *, uint64_t, bool *);
    using GetCallResultFn = bool (*)(void *, uint64_t, void *, int, int, bool *);

    template <typename T>
    bool bind(const char *name, T &out);

    bool ready_ = false;
    void *library_ = nullptr;
    std::string library_path_;
    std::string error_;

    GameServerUGCFn steam_gameserver_ugc_ = nullptr;
    GameServerUtilsFn steam_gameserver_utils_ = nullptr;
    GameServerFn steam_gameserver_ = nullptr;
    BLoggedOnFn blogged_on_ = nullptr;
    CreateQueryFn create_query_ = nullptr;
    SendQueryFn send_query_ = nullptr;
    SetCachedFn set_cached_ = nullptr;
    SetBoolFn set_children_ = nullptr;
    SetBoolFn set_long_description_ = nullptr;
    GetResultFn get_result_ = nullptr;
    GetChildrenFn get_children_ = nullptr;
    ReleaseQueryFn release_query_ = nullptr;
    DownloadItemFn download_item_ = nullptr;
    SuspendDownloadsFn suspend_downloads_ = nullptr;
    ItemStateFn item_state_ = nullptr;
    ItemInstallInfoFn item_install_info_ = nullptr;
    IsCallCompletedFn is_call_completed_ = nullptr;
    GetCallResultFn get_call_result_ = nullptr;
};

SteamApi &steam_api();
