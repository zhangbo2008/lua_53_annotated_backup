/*
** $Id: lstring.h,v 1.61.1.1 2017/04/19 17:20:42 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"

//第一个是对齐大小,然后是后面填入真实字符串的数据.2022-08-29,15点32  原来是这样.数据就是放在他的struct结构体后面紧跟数据即可.不用定义什么struct啥的.
#define sizelstring(l) (sizeof(union UTString) + ((l) + 1) * sizeof(char))

#define sizeludata(l) (sizeof(union UUdata) + (l))
#define sizeudata(u) sizeludata((u)->len)

#define luaS_newliteral(L, s) (luaS_newlstr(L, "" s, \
                                            (sizeof(s) / sizeof(char)) - 1))

/*
** test whether a string is a reserved word
*/  //为什么看extra是否大于0就行?????????原因是global_state里面存了这些关键字.如果s在这些关键字里面存着,会在字符串创建的时候extra里面写入global_state里面对应位置的哈希值.所以他是大于0的. kan llex.c:70即可. 创建时候extra写入了. 会进入global 池子中.
#define isreserved(s) ((s)->tt == LUA_TSHRSTR && (s)->extra > 0)

/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a, b) check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))

LUAI_FUNC unsigned int luaS_hash(const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int luaS_hashlongstr(TString *ts);
LUAI_FUNC int luaS_eqlngstr(TString *a, TString *b);
LUAI_FUNC void luaS_resize(lua_State *L, int newsize);
LUAI_FUNC void luaS_clearcache(global_State *g);
LUAI_FUNC void luaS_init(lua_State *L);
LUAI_FUNC void luaS_remove(lua_State *L, TString *ts);
LUAI_FUNC Udata *luaS_newudata(lua_State *L, size_t s);
LUAI_FUNC TString *luaS_newlstr(lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *luaS_new(lua_State *L, const char *str);
LUAI_FUNC TString *luaS_createlngstrobj(lua_State *L, size_t l);

#endif
