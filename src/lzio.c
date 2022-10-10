/*
** $Id: lzio.c,v 1.37.1.1 2017/04/19 17:20:42 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/

#define lzio_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "llimits.h"
#include "lmem.h"
#include "lstate.h"
#include "lzio.h"

// z进行填充,然后返回第一个字符.   被f_parser来调用luaZ_fill这个函数.
int luaZ_fill (ZIO *z) {
  size_t size;
  lua_State *L = z->L;
  const char *buff;
  lua_unlock(L);
  buff = z->reader(L, z->data, &size); //文件读取，返回size 大小表示读了多少字符. getF方法, 函数返回一个char * , 数据读到buff中.
  lua_lock(L);
  if (buff == NULL || size == 0)
    return EOZ;
  z->n = size - 1;  /* discount char being returned */   //因为我们上面调用reader方法就是返回了一个字符. 所以还需要读的字符-1. z->n表示还有多少个字符没有被读取.所以需要剪去我们当前读取的这个字符.
  z->p = buff;
  return cast_uchar(*(z->p++));  //我理解是当你调用这个函数时候return一个字符串了.然后z存储的指针就移动一个.因为你返回一个字符了.debug看一下就是 是这样的.理解对的.
}

// 对zio进行初始哈  //被lua_load调用.
void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader, void *data) {
  z->L = L;
  z->reader = reader;
  z->data = data;
  z->n = 0;
  z->p = NULL;
}

// z里面读取数据n个到b里面.
/* --------------------------------------------------------------- read --- */
size_t luaZ_read (ZIO *z, void *b, size_t n) {
  while (n) {
    size_t m;
    if (z->n == 0) {  /* no bytes in buffer? */
      if (luaZ_fill(z) == EOZ)  /* try to read more */
        return n;  /* no more input; return number of missing bytes */
      else {//到这里肯定luaZ_fill读了一个字符.所以z->n++
        z->n++;  /* luaZ_fill consumed first byte; put it back */
        z->p--; //指针拉回来一个.
      }
    }
    m = (n <= z->n) ? n : z->n;  /* min. between n and z->n */
    memcpy(b, z->p, m);
    z->n -= m;
    z->p += m;
    b = (char *)b + m;
    n -= m;
  }
  return 0;
}

