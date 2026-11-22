#pragma once

#include <cstddef>
#include <string>

struct lua_State;

using lua_CFunction = int (*)(lua_State *L);

struct LuaAPI {
    int (*lua_gettop)(lua_State *L) = nullptr;
    void (*lua_settop)(lua_State *L, int index) = nullptr;
    int (*lua_type)(lua_State *L, int index) = nullptr;
    int (*lua_isstring)(lua_State *L, int index) = nullptr;
    int (*lua_toboolean)(lua_State *L, int index) = nullptr;
    const char *(*lua_tolstring)(lua_State *L, int index, std::size_t *len) = nullptr;
    void (*lua_pushnil)(lua_State *L) = nullptr;
    void (*lua_pushboolean)(lua_State *L, int b) = nullptr;
    void (*lua_pushnumber)(lua_State *L, double n) = nullptr;
    void (*lua_pushlstring)(lua_State *L, const char *s, std::size_t len) = nullptr;
    void (*lua_pushcclosure)(lua_State *L, lua_CFunction fn, int n) = nullptr;
    void (*lua_pushvalue)(lua_State *L, int index) = nullptr;
    void (*lua_getfield)(lua_State *L, int index, const char *k) = nullptr;
    void (*lua_setfield)(lua_State *L, int index, const char *k) = nullptr;
    void (*lua_rawgeti)(lua_State *L, int index, int n) = nullptr;
    void (*lua_rawseti)(lua_State *L, int index, int n) = nullptr;
    void (*lua_createtable)(lua_State *L, int narr, int nrec) = nullptr;
    void (*lua_remove)(lua_State *L, int index) = nullptr;
    int (*lua_pcall)(lua_State *L, int nargs, int nresults, int errfunc) = nullptr;
    int (*luaL_ref)(lua_State *L, int t) = nullptr;
    void (*luaL_unref)(lua_State *L, int t, int ref) = nullptr;
    int (*lua_error)(lua_State *L) = nullptr;

    bool ready() const { return loaded_; }
    bool can_raise() const { return lua_pushlstring != nullptr && lua_error != nullptr; }
    const std::string &error() const { return error_; }

    bool load();
    void unload();

private:
    bool loaded_ = false;
    bool library_owned_ = false;
    void *library_ = nullptr;
    std::string error_;
};

LuaAPI &lua_api();
