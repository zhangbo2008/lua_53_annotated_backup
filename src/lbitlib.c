/*
** $Id: lbitlib.c,v 1.30.1.1 2017/04/19 17:20:42 roberto Exp $
** Standard library for bitwise operations
** See Copyright Notice in lua.h
*/
// bit操作的.
#define lbitlib_c
#define LUA_LIB

#include "lprefix.h"


#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#if defined(LUA_COMPAT_BITLIB)		/* { */


#define pushunsigned(L,n)	lua_pushinteger(L, (lua_Integer)(n))
#define checkunsigned(L,i)	((lua_Unsigned)luaL_checkinteger(L,i))


/* number of bits to consider in a number */
#if !defined(LUA_NBITS)
#define LUA_NBITS	32
#endif


/*
** a lua_Unsigned with its first LUA_NBITS bits equal to 1. (Shift must
** be made in two parts to avoid problems when LUA_NBITS is equal to the
** number of bits in a lua_Unsigned.)
*/

// ALLONES代表的是32位的1，其他位全是0
// ALLONES的作用就是过滤32位之外的位，只保留32位数字
#define ALLONES		(~(((~(lua_Unsigned)0) << (LUA_NBITS - 1)) << 1))


/* macro to trim extra bits */
#define trim(x)		((x) & ALLONES)

//构造一个最低的n位全是1其他全是0的.
/* builds a number with 'n' ones (1 <= n <= LUA_NBITS) */
#define mask(n)		(~((ALLONES << 1) << ((n) - 1)))



static lua_Unsigned andaux (lua_State *L) {
  int i, n = lua_gettop(L);
  lua_Unsigned r = ~(lua_Unsigned)0;
  for (i = 1; i <= n; i++)
    r &= checkunsigned(L, i);
  return trim(r);
}


static int b_and (lua_State *L) {
  lua_Unsigned r = andaux(L);
  pushunsigned(L, r);
  return 1;
}


static int b_test (lua_State *L) {
  lua_Unsigned r = andaux(L);
  lua_pushboolean(L, r != 0);
  return 1;
}


static int b_or (lua_State *L) {
  int i, n = lua_gettop(L);
  lua_Unsigned r = 0;
  for (i = 1; i <= n; i++)
    r |= checkunsigned(L, i);
  pushunsigned(L, trim(r));
  return 1;
}


static int b_xor (lua_State *L) {
  int i, n = lua_gettop(L);
  lua_Unsigned r = 0;
  for (i = 1; i <= n; i++)
    r ^= checkunsigned(L, i);
  pushunsigned(L, trim(r));
  return 1;
}


static int b_not (lua_State *L) {
  lua_Unsigned r = ~checkunsigned(L, 1);
  pushunsigned(L, trim(r));
  return 1;
}


static int b_shift (lua_State *L, lua_Unsigned r, lua_Integer i) {
  if (i < 0) {  /* shift right? */
    i = -i;
    r = trim(r);
    if (i >= LUA_NBITS) r = 0;
    else r >>= i;
  }
  else {  /* shift left */
    if (i >= LUA_NBITS) r = 0;
    else r <<= i;
    r = trim(r);
  }
  pushunsigned(L, r);
  return 1;
}


static int b_lshift (lua_State *L) {
  return b_shift(L, checkunsigned(L, 1), luaL_checkinteger(L, 2));
}


static int b_rshift (lua_State *L) {
  return b_shift(L, checkunsigned(L, 1), -luaL_checkinteger(L, 2));
}

// L[2]<<L[1] 结果入栈.
static int b_arshift (lua_State *L) {
  lua_Unsigned r = checkunsigned(L, 1);
  lua_Integer i = luaL_checkinteger(L, 2);
  if (i < 0 || !(r & ((lua_Unsigned)1 << (LUA_NBITS - 1))))
    return b_shift(L, r, -i);
  else {  /* arithmetic shift for 'negative' number */
    if (i >= LUA_NBITS) r = ALLONES;
    else
      r = trim((r >> i) | ~(trim(~(lua_Unsigned)0) >> i));  /* add signal bit */
    pushunsigned(L, r);
    return 1;
  }
}

// 这个旋转操作就是移位操作，然后把移出去的部分，在进行或运算补充进来// 核心操作在于 r = (r << i) | (r >> (LUA_NBITS - i));// 首先向左移i个单位，然后移出去的部分通过右移（LUA_NBITS - i）的方式保留下
static int b_rot (lua_State *L, lua_Integer d) {
  lua_Unsigned r = checkunsigned(L, 1);
  int i = d & (LUA_NBITS - 1);  /* i = d % NBITS */
  r = trim(r);
  if (i != 0)  /* avoid undefined shift of LUA_NBITS when i == 0 */
    r = (r << i) | (r >> (LUA_NBITS - i));
  pushunsigned(L, trim(r));
  return 1;
}


static int b_lrot (lua_State *L) {
  return b_rot(L, luaL_checkinteger(L, 2));
}


static int b_rrot (lua_State *L) {
  return b_rot(L, -luaL_checkinteger(L, 2));
}


/*  就是读取参数的意思. 第一个参数放f里面第二个放width里面.
** get field and width arguments for field-manipulation functions,
** checking whether they are valid.
** ('luaL_error' called without 'return' to avoid later warnings about
** 'width' being used uninitialized.)
*/
static int fieldargs (lua_State *L, int farg, int *width) {
  lua_Integer f = luaL_checkinteger(L, farg);
  lua_Integer w = luaL_optinteger(L, farg + 1, 1);
  luaL_argcheck(L, 0 <= f, farg, "field cannot be negative");
  luaL_argcheck(L, 0 < w, farg + 1, "width must be positive");
  if (f + w > LUA_NBITS)
    luaL_error(L, "trying to access non-existent bits");
  *width = (int)w;
  return (int)f;
}

// bit32.extract(x,f,w)，该运算返回的结果是从x的f位开始w位数。
// 假如执行：bit32.extract(11,1,3)，运算过程是从11的二进制表示的1011第一位开始(整数部分的最右边指的是第0位)，至包含自身的3位，即101组成的整数，为5.
// mask(w)的作用是构造一个低位为w个1，其余位为0的32位二进制数字，以此来保存低w位的数字
static int b_extract (lua_State *L) {
  int w;
  lua_Unsigned r = trim(checkunsigned(L, 1));
  int f = fieldargs(L, 2, &w);
  r = (r >> f) & mask(w);
  pushunsigned(L, r);
  return 1;
}
// (r & ~(m << f)) 这部分算出的是r除了指定位的数字，此时指定为变为0
// ((v & m) << f)  这部分算出的是替换后的r指定位替换后的数字
static int b_replace (lua_State *L) {
  int w;
  lua_Unsigned r = trim(checkunsigned(L, 1));
  lua_Unsigned v = trim(checkunsigned(L, 2));
  int f = fieldargs(L, 3, &w);
  lua_Unsigned m = mask(w);
  r = (r & ~(m << f)) | ((v & m) << f);
  pushunsigned(L, r);
  return 1;
}


static const luaL_Reg bitlib[] = {
  {"arshift", b_arshift},
  {"band", b_and},
  {"bnot", b_not},
  {"bor", b_or},
  {"bxor", b_xor},
  {"btest", b_test},
  {"extract", b_extract},
  {"lrotate", b_lrot},
  {"lshift", b_lshift},
  {"replace", b_replace},
  {"rrotate", b_rrot},
  {"rshift", b_rshift},
  {NULL, NULL}
};



LUAMOD_API int luaopen_bit32 (lua_State *L) {
  luaL_newlib(L, bitlib);
  return 1;
}


#else					/* }{ */


LUAMOD_API int luaopen_bit32 (lua_State *L) {
  return luaL_error(L, "library 'bit32' has been deprecated");
}

#endif					/* } */
