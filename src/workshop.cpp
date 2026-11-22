#include "workshop.hpp"

#include "gmod_lzma.hpp"
#include "lua.hpp"
#include "steam.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr int kErrCreateQuery = -1;
constexpr int kErrSendQuery = -2;
constexpr int kErrResultCount = -3;
constexpr int kErrInvalidId = -5;
constexpr int kErrIdMismatch = -6;
constexpr uint32_t kMaxChildren = 4096;
constexpr uint64_t kMaxCompressed = 1024ull * 1024ull * 1024ull;
constexpr const char *kHookName = "gmsv_workshop";

namespace fs = std::filesystem;

void log_info(const char *message) {
    std::fputs(message, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void log_error(const char *message) {
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
}

std::string id_text(uint64_t id) {
    return std::to_string(id);
}

std::string_view bounded_text(const char *data, std::size_t cap) {
    std::size_t length = 0;
    if (data != nullptr) {
        while (length < cap && data[length] != '\0') {
            ++length;
        }
    }
    return {data == nullptr ? "" : data, length};
}

bool extension_is(const fs::path &path, std::string_view expected) {
    const std::string ext = path.extension().string();
    if (ext.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < ext.size(); ++i) {
        const auto ch = static_cast<unsigned char>(ext[i]);
        if (static_cast<char>(std::tolower(ch)) != expected[i]) {
            return false;
        }
    }
    return true;
}

// game.MountGMA on current dedicated servers only opens a GMA that sits directly
// in an allowed folder (cache/, data/, download/). cache/srcds/<id>.gma is visible
// to file.Find and is what the Rust module writes, but MountGMA rejects it.
fs::path cache_disk_path(uint64_t id) {
    return fs::path("garrysmod") / "cache" / (id_text(id) + ".gma");
}

std::string cache_game_path(uint64_t id) {
    return "cache/" + id_text(id) + ".gma";
}

bool replace_file(const fs::path &from, const fs::path &to, std::string &error) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    if (ec) {
        error = "failed to publish cache file: " + ec.message();
        fs::remove(from, ec);
        return false;
    }
    return true;
}

bool read_file(const fs::path &path, std::vector<uint8_t> &out, std::string &error) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        error = "failed to stat workshop file: " + ec.message();
        return false;
    }
    if (size > kMaxCompressed) {
        error = "compressed workshop file is too large";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "failed to open workshop file";
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        input.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size));
    }
    if (!input) {
        error = "failed to read workshop file";
        out.clear();
        return false;
    }
    return true;
}

bool write_cache_bytes(uint64_t id, const std::vector<uint8_t> &bytes, std::string &error) {
    const fs::path dest = cache_disk_path(id);
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        error = "failed to create cache directory: " + ec.message();
        return false;
    }

    const fs::path temp = dest.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "failed to create cache file";
            return false;
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        if (!output) {
            error = "failed to write cache file";
            fs::remove(temp, ec);
            return false;
        }
    }
    return replace_file(temp, dest, error);
}

bool copy_gma(uint64_t id, const fs::path &source, std::string &error) {
    const fs::path dest = cache_disk_path(id);
    std::error_code ec;
    if (fs::exists(dest, ec) && fs::equivalent(source, dest, ec)) {
        return true;
    }

    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        error = "failed to create cache directory: " + ec.message();
        return false;
    }

    const fs::path temp = dest.string() + ".tmp";
    fs::copy_file(source, temp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "failed to copy gma: " + ec.message();
        return false;
    }
    return replace_file(temp, dest, error);
}

bool decompress_gma(uint64_t id, const fs::path &source, std::string &error) {
    std::vector<uint8_t> compressed;
    if (!read_file(source, compressed, error)) {
        return false;
    }

    std::vector<uint8_t> plain;
    if (!decompress_gmod_lzma(compressed.data(), compressed.size(), plain, error)) {
        return false;
    }
    compressed.clear();

    if (plain.size() < 4 || plain[0] != 'G' || plain[1] != 'M' || plain[2] != 'A' ||
        plain[3] != 'D') {
        error = "decompressed workshop file is not a GMA";
        return false;
    }
    return write_cache_bytes(id, plain, error);
}

bool cache_gma(uint64_t id, const fs::path &source, std::string &error) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        std::optional<fs::path> gma;
        std::optional<fs::path> bin;
        std::optional<fs::path> other;
        bool extra_other = false;

        fs::directory_iterator it(source, ec);
        const fs::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || file_ec) {
                continue;
            }
            const fs::path &file = it->path();
            if (extension_is(file, ".gma")) {
                if (!gma) {
                    gma = file;
                }
            } else if (extension_is(file, ".bin")) {
                if (!bin) {
                    bin = file;
                }
            } else if (!other) {
                other = file;
            } else {
                extra_other = true;
            }
        }

        if (gma) {
            return copy_gma(id, *gma, error);
        }
        if (bin) {
            return decompress_gma(id, *bin, error);
        }
        if (other && !extra_other) {
            return decompress_gma(id, *other, error);
        }
        error = "workshop folder has no usable GMA";
        return false;
    }

    if (fs::is_regular_file(source, ec)) {
        if (extension_is(source, ".gma")) {
            return copy_gma(id, source, error);
        }
        return decompress_gma(id, source, error);
    }

    error = "workshop install path is missing";
    return false;
}

void report_callback_error(LuaState &lua) {
    const auto [text, length] = lua.to_lstring(-1);
    std::string message = "[gmsv_workshop] callback error: ";
    if (text != nullptr && length > 0) {
        message.append(text, length);
    }
    log_error(message.c_str());
    lua.pop();
}

void call_download(lua_State *state, int callback, const char *path) {
    LuaState lua(state);
    lua.raw_geti(LUA_REGISTRYINDEX, callback);
    lua.unref(callback);
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop();
        return;
    }

    if (path == nullptr) {
        lua.push_nil();
        lua.push_nil();
    } else {
        lua.push_string(path);
        const int top = lua.get_top();
        lua.get_global("file");
        if (lua.type(-1) != LUA_TTABLE) {
            lua.set_top(top);
            lua.push_nil();
        } else {
            lua.get_field(-1, "Open");
            if (lua.type(-1) != LUA_TFUNCTION) {
                lua.set_top(top);
                lua.push_nil();
            } else {
                lua.push_string(path);
                lua.push_string("rb");
                lua.push_string("GAME");
                if (lua.pcall(3, 1) != 0) {
                    report_callback_error(lua);
                    lua.set_top(top);
                    lua.push_nil();
                } else {
                    lua.remove(-2);
                }
            }
        }
    }

    if (lua.pcall(2, 0) != 0) {
        report_callback_error(lua);
    }
}

void call_file_info_nil(lua_State *state, int callback) {
    LuaState lua(state);
    lua.raw_geti(LUA_REGISTRYINDEX, callback);
    lua.unref(callback);
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop();
        return;
    }
    lua.push_nil();
    if (lua.pcall(1, 0) != 0) {
        report_callback_error(lua);
    }
}

void set_string_field(LuaState &lua, const char *key, std::string_view value) {
    lua.push_string(value);
    lua.set_field(-2, key);
}

void set_number_field(LuaState &lua, const char *key, double value) {
    lua.push_number(value);
    lua.set_field(-2, key);
}

void set_bool_field(LuaState &lua, const char *key, bool value) {
    lua.push_bool(value);
    lua.set_field(-2, key);
}

void call_file_info_error(lua_State *state, int callback, uint64_t id, int code) {
    LuaState lua(state);
    lua.raw_geti(LUA_REGISTRYINDEX, callback);
    lua.unref(callback);
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop();
        return;
    }

    lua.create_table(0, 2);
    set_string_field(lua, "id", id_text(id));
    set_number_field(lua, "error", static_cast<double>(code));
    if (lua.pcall(1, 0) != 0) {
        report_callback_error(lua);
    }
}

std::optional<std::string> install_folder(uint64_t id) {
    SteamApi &steam = steam_api();
    void *ugc = steam.ugc();
    if (ugc == nullptr) {
        return std::nullopt;
    }
    if ((steam.item_state(ugc, id) & kItemStateInstalled) == 0) {
        return std::nullopt;
    }

    char folder[4096] = {};
    if (!steam.item_install_info(ugc, id, folder, static_cast<uint32_t>(sizeof(folder) - 1))) {
        return std::nullopt;
    }
    folder[sizeof(folder) - 1] = '\0';
    if (folder[0] == '\0') {
        return std::nullopt;
    }
    return std::string(folder);
}

} // namespace

Workshop &Workshop::instance() {
    static Workshop workshop;
    return workshop;
}

void Workshop::bind(lua_State *state) {
    if (state_ != nullptr && state_ != state) {
        release_queries();
        queued_.clear();
        pending_.clear();
        hook_ = false;
        log_error("[gmsv_workshop] Lua state changed; cancelled pending workshop requests");
    }
    state_ = state;
}

void Workshop::shutdown(lua_State *state) {
    const bool same_state = state != nullptr && state == state_ && lua_api().ready();
    if (same_state) {
        remove_hook(state);
        LuaState lua(state);
        for (const auto &entry : queued_) {
            lua.unref(entry.second);
        }
        for (const auto &entry : pending_) {
            lua.unref(entry.second);
        }
        for (const Query &query : queries_) {
            lua.unref(query.callback);
        }
    }

    queued_.clear();
    pending_.clear();
    release_queries();
    hook_ = false;
    state_ = nullptr;
}

void Workshop::reject_download(lua_State *state, int callback) {
    call_download(state, callback, nullptr);
}

void Workshop::reject_file_info(lua_State *state, int callback) {
    call_file_info_nil(state, callback);
}

void Workshop::drop_download(lua_State *state, uint64_t published_file_id) {
    LuaState lua(state);
    const auto drop = [&](std::unordered_map<uint64_t, int> &pending) {
        const auto it = pending.find(published_file_id);
        if (it == pending.end()) {
            return;
        }
        lua.unref(it->second);
        pending.erase(it);
    };
    drop(queued_);
    drop(pending_);
}

void Workshop::release_queries() {
    void *ugc = steam_api().ready() ? steam_api().ugc() : nullptr;
    if (ugc != nullptr) {
        for (const Query &query : queries_) {
            steam_api().release_query(ugc, query.handle);
        }
    }
    queries_.clear();
}

bool Workshop::ensure_hook(lua_State *state) {
    if (hook_) {
        return true;
    }

    LuaState lua(state);
    lua.get_global("hook");
    if (lua.type(-1) != LUA_TTABLE) {
        lua.pop();
        return false;
    }
    lua.get_field(-1, "Add");
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop(2);
        return false;
    }

    lua.push_string("Think");
    lua.push_string(kHookName);
    lua.push_function(workshop_think);
    if (lua.pcall(3, 0) != 0) {
        report_callback_error(lua);
        lua.pop();
        return false;
    }

    lua.pop();
    hook_ = true;
    return true;
}

void Workshop::remove_hook(lua_State *state) {
    if (!hook_ || state == nullptr) {
        hook_ = false;
        return;
    }

    LuaState lua(state);
    lua.get_global("hook");
    if (lua.type(-1) != LUA_TTABLE) {
        lua.pop();
        hook_ = false;
        return;
    }
    lua.get_field(-1, "Remove");
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop(2);
        hook_ = false;
        return;
    }

    lua.push_string("Think");
    lua.push_string(kHookName);
    if (lua.pcall(2, 0) != 0) {
        report_callback_error(lua);
    }
    lua.pop();
    hook_ = false;
}

void Workshop::deliver_download(lua_State *state,
                                uint64_t published_file_id,
                                int callback,
                                const std::string &folder) {
    std::string error;
    if (!cache_gma(published_file_id, fs::path(folder), error)) {
        std::string message = "[gmsv_workshop] Failed to process download: " + error;
        log_error(message.c_str());
        call_download(state, callback, nullptr);
        return;
    }

    const std::string relative = cache_game_path(published_file_id);
    call_download(state, callback, relative.c_str());
}

void Workshop::download(lua_State *state, uint64_t published_file_id, int callback) {
    drop_download(state, published_file_id);

    std::error_code ec;
    if (fs::is_regular_file(cache_disk_path(published_file_id), ec)) {
        deliver_download(state, published_file_id, callback, cache_disk_path(published_file_id).string());
        return;
    }

    SteamApi &steam = steam_api();
    if (!steam.ready() && !steam.load()) {
        log_error(("[gmsv_workshop] " + steam.error()).c_str());
        call_download(state, callback, nullptr);
        return;
    }

    if (const std::optional<std::string> folder = install_folder(published_file_id)) {
        deliver_download(state, published_file_id, callback, *folder);
        return;
    }

    void *ugc = steam.ugc();
    if (ugc == nullptr) {
        log_error("[gmsv_workshop] ISteamUGC is not available");
        call_download(state, callback, nullptr);
        return;
    }

    if (!steam.logged_on()) {
        queued_[published_file_id] = callback;
        if (!ensure_hook(state)) {
            queued_.erase(published_file_id);
            log_error("[gmsv_workshop] failed to install Think hook");
            call_download(state, callback, nullptr);
            return;
        }
        std::string message = "[gmsv_workshop] Queued " + id_text(published_file_id);
        log_info(message.c_str());
        return;
    }

    steam.suspend_downloads(ugc, false);
    if (!steam.download_item(ugc, published_file_id, true)) {
        std::string message = "[gmsv_workshop] Item ID " + id_text(published_file_id) +
                              " is invalid or the server is not logged onto Steam";
        log_error(message.c_str());
        call_download(state, callback, nullptr);
        return;
    }

    if (const std::optional<std::string> folder = install_folder(published_file_id)) {
        deliver_download(state, published_file_id, callback, *folder);
        return;
    }

    pending_[published_file_id] = callback;
    if (!ensure_hook(state)) {
        pending_.erase(published_file_id);
        log_error("[gmsv_workshop] failed to install Think hook");
        call_download(state, callback, nullptr);
        return;
    }

    std::string message = "[gmsv_workshop] Downloading " + id_text(published_file_id);
    log_info(message.c_str());
}

void Workshop::deliver_query(lua_State *state,
                             void *ugc,
                             void *utils,
                             const Query &query,
                             bool io_failure) {
    const auto fail = [&](int code) {
        steam_api().release_query(ugc, query.handle);
        call_file_info_error(state, query.callback, query.id, code);
    };

    alignas(16) unsigned char completed_raw[sizeof(SteamUGCQueryCompleted_t) + 256];
    std::memset(completed_raw, 0, sizeof(completed_raw));
    auto *completed = reinterpret_cast<SteamUGCQueryCompleted_t *>(completed_raw);
    bool failed = false;
    const bool ok = steam_api().get_api_call_result(utils,
                                                    query.call,
                                                    completed,
                                                    static_cast<int>(sizeof(SteamUGCQueryCompleted_t)),
                                                    kSteamUGCQueryCompleted,
                                                    failed);
    if (!ok) {
        fail(kErrSendQuery);
        return;
    }
    if (io_failure || failed) {
        fail(kEResultIOFailure);
        return;
    }
    if (completed->m_eResult != kEResultOK) {
        fail(completed->m_eResult);
        return;
    }
    if (completed->m_unNumResultsReturned != 1) {
        fail(kErrResultCount);
        return;
    }

    alignas(16) unsigned char raw[sizeof(SteamUGCDetails_t) + 512];
    std::memset(raw, 0, sizeof(raw));
    auto *details = reinterpret_cast<SteamUGCDetails_t *>(raw);
    if (!steam_api().get_query_result(ugc, query.handle, 0, details)) {
        fail(kErrResultCount);
        return;
    }

    if (details->m_nPublishedFileId == 0) {
        fail(kErrInvalidId);
        return;
    }
    if (details->m_nPublishedFileId != query.id) {
        fail(kErrIdMismatch);
        return;
    }
    if (details->m_eResult != kEResultOK) {
        fail(details->m_eResult);
        return;
    }

    uint32_t child_count = details->m_unNumChildren;
    if (child_count > kMaxChildren) {
        child_count = kMaxChildren;
    }
    std::vector<uint64_t> children(child_count);
    if (child_count > 0) {
        steam_api().get_query_children(ugc, query.handle, 0, children.data(), child_count);
    }

    const int32_t file_size = details->m_nFileSize;
    const int32_t preview_size = details->m_nPreviewFileSize;
    const uint32_t created = details->m_rtimeCreated;
    const uint32_t updated = details->m_rtimeUpdated;
    const uint32_t votes_up = details->m_unVotesUp;
    const uint32_t votes_down = details->m_unVotesDown;
    const float score = details->m_flScore;
    const bool banned = details->m_bBanned;
    const std::string title(bounded_text(details->m_rgchTitle, kUgcTitleMax));
    const std::string description(bounded_text(details->m_rgchDescription, kUgcDescriptionMax));
    const std::string tags(bounded_text(details->m_rgchTags, kUgcTagsMax));
    const std::string preview_url(bounded_text(details->m_rgchURL, kUgcUrlMax));
    const std::string owner = id_text(details->m_ulSteamIDOwner);
    const std::string file_id = id_text(details->m_hFile);
    const std::string preview_id = id_text(details->m_hPreviewFile);
    const std::string published_id = id_text(query.id);

    steam_api().release_query(ugc, query.handle);

    LuaState lua(state);
    lua.raw_geti(LUA_REGISTRYINDEX, query.callback);
    lua.unref(query.callback);
    if (lua.type(-1) != LUA_TFUNCTION) {
        lua.pop();
        return;
    }

    lua.create_table(0, 18);
    set_string_field(lua, "id", published_id);
    set_string_field(lua, "title", title);
    set_string_field(lua, "description", description);
    set_string_field(lua, "owner", owner);
    set_string_field(lua, "previewid", preview_id);
    set_string_field(lua, "fileid", file_id);
    set_string_field(lua, "tags", tags);
    set_bool_field(lua, "banned", banned);
    set_number_field(lua, "created", static_cast<double>(created));
    set_number_field(lua, "updated", static_cast<double>(updated));
    set_number_field(lua, "size", static_cast<double>(file_size));
    set_string_field(lua, "previewurl", preview_url);
    set_number_field(lua, "previewsize", static_cast<double>(preview_size));
    set_number_field(lua, "up", static_cast<double>(votes_up));
    set_number_field(lua, "down", static_cast<double>(votes_down));
    set_number_field(lua, "total", static_cast<double>(static_cast<uint64_t>(votes_up) + votes_down));
    set_number_field(lua, "score", static_cast<double>(score));

    lua.create_table(static_cast<int>(children.size()), 0);
    for (std::size_t i = 0; i < children.size(); ++i) {
        lua.push_string(id_text(children[i]));
        lua.raw_seti(-2, static_cast<int>(i + 1));
    }
    lua.set_field(-2, "children");

    if (lua.pcall(1, 0) != 0) {
        report_callback_error(lua);
    }
}

void Workshop::file_info(lua_State *state, uint64_t published_file_id, int callback, bool extra_info) {
    SteamApi &steam = steam_api();
    if (!steam.ready() && !steam.load()) {
        log_error(("[gmsv_workshop] " + steam.error()).c_str());
        call_file_info_error(state, callback, published_file_id, kErrCreateQuery);
        return;
    }

    void *ugc = steam.ugc();
    const uint64_t handle = steam.create_details_query(ugc, published_file_id);
    if (ugc == nullptr || handle == kUgcQueryHandleInvalid) {
        call_file_info_error(state, callback, published_file_id, kErrCreateQuery);
        return;
    }

    steam.set_allow_cached_response(ugc, handle, 60);
    steam.set_return_children(ugc, handle, true);
    if (extra_info) {
        steam.set_return_long_description(ugc, handle, true);
    }

    const uint64_t call = steam.send_query(ugc, handle);
    if (call == 0) {
        steam.release_query(ugc, handle);
        call_file_info_error(state, callback, published_file_id, kErrSendQuery);
        return;
    }

    queries_.push_back(Query{published_file_id, callback, call, handle});
    if (!ensure_hook(state)) {
        queries_.pop_back();
        steam.release_query(ugc, handle);
        log_error("[gmsv_workshop] failed to install Think hook");
        call_file_info_error(state, callback, published_file_id, kErrSendQuery);
    }
}

void Workshop::poll_downloads(lua_State *state) {
    struct Ready {
        uint64_t id;
        int callback;
        std::string folder;
    };

    std::vector<Ready> ready;
    for (auto it = pending_.begin(); it != pending_.end();) {
        std::optional<std::string> folder = install_folder(it->first);
        if (!folder) {
            ++it;
            continue;
        }
        ready.push_back(Ready{it->first, it->second, std::move(*folder)});
        it = pending_.erase(it);
    }

    for (const Ready &item : ready) {
        deliver_download(state, item.id, item.callback, item.folder);
    }
}

void Workshop::poll_queries(lua_State *state) {
    if (queries_.empty()) {
        return;
    }

    SteamApi &steam = steam_api();
    void *utils = steam.utils();
    void *ugc = steam.ugc();
    if (utils == nullptr || ugc == nullptr) {
        return;
    }

    std::vector<Query> keep;
    std::vector<std::pair<Query, bool>> done;
    keep.reserve(queries_.size());
    for (const Query &query : queries_) {
        bool failed = false;
        if (steam.is_api_call_completed(utils, query.call, failed)) {
            done.emplace_back(query, failed);
        } else {
            keep.push_back(query);
        }
    }
    queries_ = std::move(keep);

    for (const auto &item : done) {
        deliver_query(state, ugc, utils, item.first, item.second);
    }
}

void Workshop::poll(lua_State *state) {
    if (!queued_.empty() && steam_api().logged_on()) {
        std::unordered_map<uint64_t, int> queued = std::move(queued_);
        queued_.clear();
        for (const auto &entry : queued) {
            download(state, entry.first, entry.second);
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_download_poll_ >= std::chrono::milliseconds(500)) {
        last_download_poll_ = now;
        poll_downloads(state);
    }
    poll_queries(state);

    if (queued_.empty() && pending_.empty() && queries_.empty()) {
        remove_hook(state);
    }
}

int workshop_think(lua_State *state) {
    try {
        Workshop::instance().poll(state);
    } catch (const std::exception &ex) {
        std::string message = std::string("[gmsv_workshop] ") + ex.what();
        log_error(message.c_str());
    }
    return 0;
}
