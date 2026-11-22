#pragma once

#include "steam_types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

class Workshop {
public:
    static Workshop &instance();

    void bind(lua_State *state);
    void shutdown(lua_State *state);

    // Takes ownership of the registry reference.
    void download(lua_State *state, uint64_t published_file_id, int callback);
    void file_info(lua_State *state, uint64_t published_file_id, int callback, bool extra_info);
    void reject_download(lua_State *state, int callback);
    void reject_file_info(lua_State *state, int callback);

    void poll(lua_State *state);

private:
    struct Query {
        uint64_t id = 0;
        int callback = -2;
        uint64_t call = 0;
        uint64_t handle = kUgcQueryHandleInvalid;
    };

    Workshop() = default;

    void drop_download(lua_State *state, uint64_t published_file_id);
    void release_queries();
    bool ensure_hook(lua_State *state);
    void remove_hook(lua_State *state);
    void poll_downloads(lua_State *state);
    void poll_queries(lua_State *state);
    void deliver_download(lua_State *state,
                          uint64_t published_file_id,
                          int callback,
                          const std::string &folder);
    void deliver_query(lua_State *state, void *ugc, void *utils, const Query &query, bool failed);

    lua_State *state_ = nullptr;
    bool hook_ = false;
    std::unordered_map<uint64_t, int> queued_;
    std::unordered_map<uint64_t, int> pending_;
    std::vector<Query> queries_;
    std::chrono::steady_clock::time_point last_download_poll_{};
};

int workshop_think(lua_State *state);
