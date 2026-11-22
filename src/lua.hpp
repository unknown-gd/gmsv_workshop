#pragma once

#include "lua_api.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

inline constexpr int LUA_REGISTRYINDEX = -10000;
inline constexpr int LUA_GLOBALSINDEX = -10002;
inline constexpr int LUA_TNONE = -1;
inline constexpr int LUA_TNIL = 0;
inline constexpr int LUA_TBOOLEAN = 1;
inline constexpr int LUA_TNUMBER = 3;
inline constexpr int LUA_TSTRING = 4;
inline constexpr int LUA_TTABLE = 5;
inline constexpr int LUA_TFUNCTION = 6;
inline constexpr int LUA_NOREF = -2;

struct LuaState {
    lua_State *L = nullptr;

    explicit LuaState(lua_State *state) : L(state) {}

    int get_top() const { return lua_api().lua_gettop(L); }

    void set_top(int index) const { lua_api().lua_settop(L, index); }

    void pop(int count = 1) const { set_top(-count - 1); }

    int type(int index) const { return lua_api().lua_type(L, index); }

    bool is_string(int index) const { return lua_api().lua_isstring(L, index) != 0; }

    bool to_boolean(int index) const { return lua_api().lua_toboolean(L, index) != 0; }

    std::pair<const char *, std::size_t> to_lstring(int index) const {
        std::size_t len = 0;
        const char *text = lua_api().lua_tolstring(L, index, &len);
        return {text, text ? len : 0};
    }

    void push_nil() const { lua_api().lua_pushnil(L); }

    void push_bool(bool value) const { lua_api().lua_pushboolean(L, value ? 1 : 0); }

    void push_number(double value) const { lua_api().lua_pushnumber(L, value); }

    void push_lstring(const char *data, std::size_t len) const {
        lua_api().lua_pushlstring(L, data, len);
    }

    void push_string(std::string_view data) const { push_lstring(data.data(), data.size()); }

    void push_function(lua_CFunction func) const { lua_api().lua_pushcclosure(L, func, 0); }

    void push_value(int index) const { lua_api().lua_pushvalue(L, index); }

    void get_field(int index, const char *key) const { lua_api().lua_getfield(L, index, key); }

    void set_field(int index, const char *key) const { lua_api().lua_setfield(L, index, key); }

    void get_global(const char *name) const { get_field(LUA_GLOBALSINDEX, name); }

    void set_global(const char *name) const { set_field(LUA_GLOBALSINDEX, name); }

    void raw_geti(int index, int n) const { lua_api().lua_rawgeti(L, index, n); }

    void raw_seti(int index, int n) const { lua_api().lua_rawseti(L, index, n); }

    void create_table(int narr = 0, int nrec = 0) const {
        lua_api().lua_createtable(L, narr, nrec);
    }

    void remove(int index) const { lua_api().lua_remove(L, index); }

    int ref_registry() const { return lua_api().luaL_ref(L, LUA_REGISTRYINDEX); }

    void unref(int ref) const {
        if (ref != LUA_NOREF) {
            lua_api().luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }

    int pcall(int nargs, int nresults) const { return lua_api().lua_pcall(L, nargs, nresults, 0); }

    [[noreturn]] void raise() const {
        lua_api().lua_error(L);
#if defined(_MSC_VER)
        __assume(0);
#else
        __builtin_unreachable();
#endif
    }

    [[noreturn]] void error(std::string_view message) const {
        push_string(message);
        raise();
    }
};
