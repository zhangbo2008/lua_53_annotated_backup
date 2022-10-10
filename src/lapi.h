/*
** $Id: lapi.h,v 2.9.1.1 2017/04/19 17:20:42 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"
//这个跟16行一样, 不能ci.top超过整个L.top
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}
// 如果nres==-1时候表示没指定返回值的数量,这时候调用栈可能会有bug.说明当前栈超过边界了.那么就重新设置边界ci.top
#define adjustresults(L,nres) \
    { if ((nres) == LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }
//查看站上是否有足够的数据. 也就是func+1开始刀top-1这些是最大数量的数据量.
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
