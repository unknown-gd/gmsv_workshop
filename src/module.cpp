#include "lua.hpp"
#include "steam.hpp"
#include "workshop.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#define WORKSHOP_EXPORT extern "C" __declspec(dllexport)
#else
#define WORKSHOP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

constexpr const char *kVersion = "1.0.0";

[[noreturn]] void raise_message(LuaState &state, std::string message) {
    state.push_string(message);
    std::string{}.swap(message);
    state.raise();
}

std::optional<uint64_t> parse_workshop_id(LuaState &lua, int index) {
    const auto [text, length] = lua.to_lstring(index);
    if (text == nullptr || length == 0 || length > 20) {
        return std::nullopt;
    }

    uint64_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

int ref_callback(LuaState &lua) {
    lua.push_value(2);
    return lua.ref_registry();
}

int download_ugc(lua_State *state) {
    LuaState lua(state);
    if (!lua.is_string(1)) {
        lua.error("DownloadUGC: argument #1 must be a workshop id");
    }
    if (lua.type(2) != LUA_TFUNCTION) {
        lua.error("DownloadUGC: argument #2 must be a function");
    }

    const std::optional<uint64_t> id = parse_workshop_id(lua, 1);
    const int callback = ref_callback(lua);
    try {
        if (!id) {
            Workshop::instance().reject_download(state, callback);
        } else {
            Workshop::instance().download(state, *id, callback);
        }
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "[gmsv_workshop] DownloadUGC failed: %s\n", ex.what());
    }
    return 0;
}

int file_info(lua_State *state) {
    LuaState lua(state);
    if (!lua.is_string(1)) {
        lua.error("FileInfo: argument #1 must be a workshop id");
    }
    if (lua.type(2) != LUA_TFUNCTION) {
        lua.error("FileInfo: argument #2 must be a function");
    }

    bool extra_info = false;
    if (lua.get_top() >= 3 && lua.type(3) == LUA_TBOOLEAN) {
        extra_info = lua.to_boolean(3);
    }

    const std::optional<uint64_t> id = parse_workshop_id(lua, 1);
    const int callback = ref_callback(lua);
    try {
        if (!id) {
            Workshop::instance().reject_file_info(state, callback);
        } else {
            Workshop::instance().file_info(state, *id, callback, extra_info);
        }
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "[gmsv_workshop] FileInfo failed: %s\n", ex.what());
    }
    return 0;
}

void register_api(LuaState &lua) {
    lua.get_global("steamworks");
    if (lua.type(-1) != LUA_TTABLE) {
        lua.pop();
        lua.create_table(0, 4);
    }

    lua.push_string(kVersion);
    lua.set_field(-2, "gmsv_workshop");
    lua.push_function(download_ugc);
    lua.set_field(-2, "DownloadUGC");
    lua.push_function(file_info);
    lua.set_field(-2, "FileInfo");
    lua.set_global("steamworks");
}

} // namespace

WORKSHOP_EXPORT int gmod13_open(lua_State *state) {
    LuaAPI &api = lua_api();
    if (!api.load()) {
        const char *detail = api.error().empty() ? "Lua API is not initialized" : api.error().c_str();
        std::fprintf(stderr, "[gmsv_workshop] refusing to register module (%s)\n", detail);
        if (api.can_raise()) {
            LuaState lua(state);
            std::string message = "[gmsv_workshop] failed to initialize (";
            message += detail;
            message += ")";
            raise_message(lua, std::move(message));
        }
        return 0;
    }

    SteamApi &steam = steam_api();
    if (!steam.load()) {
        std::fprintf(stderr, "[gmsv_workshop] %s\n", steam.error().c_str());
    } else {
        std::fprintf(stdout, "[gmsv_workshop] %s bound %s\n", kVersion, steam.library_path().c_str());
        std::fflush(stdout);
    }

    LuaState lua(state);
    Workshop::instance().bind(state);
    register_api(lua);
    return 0;
}

WORKSHOP_EXPORT int gmod13_close(lua_State *state) {
    Workshop::instance().shutdown(state);
    lua_api().unload();
    return 0;
}
