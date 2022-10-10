/*
** $Id: ldo.c,v 2.157.1.1 2017/04/19 17:20:42 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#define ldo_c
#define LUA_CORE

#include "lprefix.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"

#define errorstatus(s) ((s) > LUA_YIELD)

/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUAI_THROW/LUAI_TRY define how Lua does exception handling. By
** default, Lua handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUAI_THROW) /* { */

#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP) /* { */

/* C++ exceptions */  //调用cpp的try catch
#define LUAI_THROW(L, c) throw(c)
#define LUAI_TRY(L, c, a)     \
    try                       \
    {                         \
        a                     \
    }                         \
    catch (...)               \
    {                         \
        if ((c)->status == 0) \
            (c)->status = -1; \
    }                    // catch(...) 表示接受任何异常.status 状态码.
#define luai_jmpbuf int /* dummy variable */

#elif defined(LUA_USE_POSIX) /* }{ */

/* in POSIX, try _longjmp/_setjmp (more efficient) */
#define LUAI_THROW(L, c) _longjmp((c)->b, 1)
#define LUAI_TRY(L, c, a)     \
    if (_setjmp((c)->b) == 0) \
    {                         \
        a                     \
    }
#define luai_jmpbuf jmp_buf

#else /* }{ */

/* ISO C handling with long jumps */
#define LUAI_THROW(L, c) longjmp((c)->b, 1)
#define LUAI_TRY(L, c, a)    \
    if (setjmp((c)->b) == 0) \
    {                        \
        a                    \
    }
#define luai_jmpbuf jmp_buf

#endif /* } */

#endif /* } */

// setjump 讲解:https://blog.csdn.net/wangkeyen/article/details/50658998

/* chain list of long jump buffers */
struct lua_longjmp
{
    struct lua_longjmp *previous;
    luai_jmpbuf b;
    volatile int status; /* error code */
};
// 根据errcode,把错误信息写到oldtop上.然后栈顶设置为oldtop
static void seterrorobj(lua_State *L, int errcode, StkId oldtop)
{
    switch (errcode)
    {
    case LUA_ERRMEM:
    {                                            /* memory error? */
        setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
        break;
    }
    case LUA_ERRERR:
    {
        setsvalue2s(L, oldtop, luaS_newliteral(L, "error in error handling"));
        break;
    }
    default:
    {
        setobjs2s(L, oldtop, L->top - 1); /* error message on current top */
        break;
    }
    }
    L->top = oldtop + 1;
}

l_noret luaD_throw(lua_State *L, int errcode) //抛出错误然后设置错误码为errcode
{
    if (L->errorJmp)                                 //如果有错误之后的跳转函数.
    {                                                /* thread has an error handler? */
        L->errorJmp->status = errcode;               /* set status */
        LUAI_THROW(L, L->errorJmp); /* jump to it */ //跳转拿到setjump然后设置setjummp返回值为1
    }
    else
    { /* thread has no error handler */
        global_State *g = G(L);
        L->status = cast_byte(errcode); /* mark it as dead */ //首先更新全局状态为错误码.
        if (g->mainthread->errorJmp)
        {                                                   /* main thread has a handler? */ //查找主线程的错误函数.
            setobjs2s(L, g->mainthread->top++, L->top - 1); /* copy error obj. */
            luaD_throw(g->mainthread, errcode);             /* re-throw in main thread */
        }
        else
        { /* no handler at all; abort */
            if (g->panic)//找主线程的panic函数.
            {                                    /* panic function? */
                seterrorobj(L, errcode, L->top); /* assume EXTRA_STACK */
                if (L->ci->top < L->top)
                    L->ci->top = L->top; /* pushing msg. can break this invariant */
                lua_unlock(L);
                g->panic(L); /* call panic function (last chance to jump out) */
            }
            abort();
        }
    }
}
//保护的进行代码运行. 运行函数f, 参数是ud . 这里面f一定是lua 闭包函数.
int luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud)
{
    unsigned short oldnCcalls = L->nCcalls; //为什么要保存这个东西????来计算层数.后面再分析这个东西.因为164行调用函数会改变L里面这个参数.L在164行传入了.
    struct lua_longjmp lj;
    lj.status = LUA_OK;
    lj.previous = L->errorJmp; /* chain new error handler */ //保存跳跃到的代码点.
    L->errorJmp = &lj;
    LUAI_TRY(L, &lj,
             (*f)(L, ud););    //里面设置断点然后运行f函数. 状态返回给lj->b. 应该是这个函数里面会进行lj修改.
    L->errorJmp = lj.previous; /* restore old error handler */ //因为已经调用完f了.每一次调用f可能会修改栈里面的错误jump.所以这样设置最可靠.
    L->nCcalls = oldnCcalls;//同事ccall也恢复.因为又退回这个层级了.
    return lj.status; //会在f函数里面设置.因为163行已经把lj塞入L里面了.所以f会操作L进行设置status即可!!!!!!!!!
}

/* }====================================================== */

/* 下面是关于栈的一些函数.
** {==================================================================
** Stack reallocation
** ===================================================================
*/  // 重新分配栈之后，可能L->stack和oldstack为不同的地址，所以要矫正依赖于栈地址的其他数据.    oldstack是旧的栈基地址.  重新分配后, top相对基地址的偏移量是恒定的.不会变.利用这点. 把偏移量加到L->stack即可. 因为L->stack表示当前的基地址.
static void correctstack(lua_State *L, TValue *oldstack)
{
    CallInfo *ci;
    UpVal *up;
    L->top = (L->top - oldstack) + L->stack;
    for (up = L->openupval; up != NULL; up = up->u.open.next)
        up->v = (up->v - oldstack) + L->stack;
    for (ci = L->ci; ci != NULL; ci = ci->previous)
    {
        ci->top = (ci->top - oldstack) + L->stack;
        ci->func = (ci->func - oldstack) + L->stack;
        if (isLua(ci))
            ci->u.l.base = (ci->u.l.base - oldstack) + L->stack;
    }
}

/* some space for error handling */
#define ERRORSTACKSIZE (LUAI_MAXSTACK + 200) //新的大小如果到达最大值.就再加200保存错误信息.然后会在227行退出程序.

void luaD_reallocstack(lua_State *L, int newsize)
{
    TValue *oldstack = L->stack; //保存旧的战地信息.
    int lim = L->stacksize;
    lua_assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
    lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK);
    luaM_reallocvector(L, L->stack, L->stacksize, newsize, TValue);
    for (; lim < newsize; lim++)
        setnilvalue(L->stack + lim); /* erase new segment */
    L->stacksize = newsize;
    L->stack_last = L->stack + newsize - EXTRA_STACK;
    correctstack(L, oldstack);
}
//stack 增长n个空间.
void luaD_growstack(lua_State *L, int n)
{
    int size = L->stacksize;
    if (size > LUAI_MAXSTACK) /* error after extra size? */
        luaD_throw(L, LUA_ERRERR);
    else
    {
        int needed = cast_int(L->top - L->stack) + n + EXTRA_STACK;
        int newsize = 2 * size;
        if (newsize > LUAI_MAXSTACK)
            newsize = LUAI_MAXSTACK;
        if (newsize < needed)
            newsize = needed;
        if (newsize > LUAI_MAXSTACK) //新的超了,就报错.
        {                            /* stack overflow? */
            luaD_reallocstack(L, ERRORSTACKSIZE);
            luaG_runerror(L, "stack overflow");
        }
        else
            luaD_reallocstack(L, newsize);
    }
}
// 查看栈里面有多少个元素还在被函数使用. 函数会有很多两个,每一个对应一个ci
static int stackinuse(lua_State *L) //查询多少个还在使用.
{
    CallInfo *ci; // ci是L->stack的一个子集.
    StkId lim = L->top;//小鱼这个才是使用过的.所以我们要找所有ci里面的上界.
    for (ci = L->ci; ci != NULL; ci = ci->previous) //因为是已经使用过的.所以要使用previous往前找.看看多少个. ci是一个链表.找一下.
    {
        if (lim < ci->top)// ci->top表示最后一个使用的.因为每使用一个ci.top会--.
            lim = ci->top; // 最小不能小鱼ci-top,
    }
    lua_assert(lim <= L->stack_last);
    return cast_int(lim - L->stack) + 1; /* part of stack in use *///比lim小的count即可.
}
//stack压缩.节省空间.
void luaD_shrinkstack(lua_State *L)
{
    int inuse = stackinuse(L);
    int goodsize = inuse + (inuse / 8) + 2 * EXTRA_STACK; //把stack压缩得到inuse多一点即可.
    if (goodsize > LUAI_MAXSTACK)
        goodsize = LUAI_MAXSTACK;     /* respect stack limit */
    if (L->stacksize > LUAI_MAXSTACK) /* had been handling stack overflow? */
        luaE_freeCI(L);               /* free all CIs (list grew because of an error) */
    else
        luaE_shrinkCI(L); /* shrink list */
    /* if thread is currently not handling a stack overflow and its
       good size is smaller than current size, shrink its stack */
    if (inuse <= (LUAI_MAXSTACK - EXTRA_STACK) &&
        goodsize < L->stacksize)
        luaD_reallocstack(L, goodsize);
    else                          /* don't change stack */
        condmovestack(L, {}, {}); /* (change only for debugging) */
}

void luaD_inctop(lua_State *L)
{
    luaD_checkstack(L, 1); //让L能多放一个.然后top加加.
    L->top++;
}

/* }================================================================== */

/*  //hook和回调函数的理解:https://zhuanlan.zhihu.com/p/159615023
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which triggers this
** function, can be changed asynchronously by signals.)
*///调用hook函数的底层实现
void luaD_hook(lua_State *L, int event, int line)
{
    lua_Hook hook = L->hook;
    if (hook && L->allowhook)
    { /* make sure there is a hook */
        CallInfo *ci = L->ci;
        ptrdiff_t top = savestack(L, L->top);     //保存一个指针的差. 结果是L-top比L战地多了多少个.
        ptrdiff_t ci_top = savestack(L, ci->top); //同理记录callinfo函数调用栈用了多少.
        lua_Debug ar;                             //创建一个debug对象.然后把event数据写入.写入debug时候的行号.
        ar.event = event;
        ar.currentline = line;
        ar.i_ci = ci;                     // ci是调用函数.也写入.
        luaD_checkstack(L, LUA_MINSTACK); /* ensure minimum stack size */
        ci->top = L->top + LUA_MINSTACK;  // top先加上20个单位.
        lua_assert(ci->top <= L->stack_last);
        L->allowhook = 0; /* cannot call hooks inside a hook */
        ci->callstatus |= CIST_HOOKED;
        lua_unlock(L);
        (*hook)(L, &ar); //调用钩子
        lua_lock(L);
        lua_assert(!L->allowhook);
        L->allowhook = 1;
        ci->top = restorestack(L, ci_top);
        L->top = restorestack(L, top);
        ci->callstatus &= ~CIST_HOOKED;
    }
}

static void callhook(lua_State *L, CallInfo *ci)
{
    int hook = LUA_HOOKCALL;
    ci->u.l.savedpc++; /* hooks assume 'pc' is already incremented */ //这里面说的是hook也算一个pc指针.所以加加????????有点奇怪. 可能为了统一吧.然后318行再改回来.
    if (isLua(ci->previous) &&
        GET_OPCODE(*(ci->previous->u.l.savedpc - 1)) == OP_TAILCALL)
    {//获取上一个操作码,如果操作码对应tailcall
        ci->callstatus |= CIST_TAIL; //debug用.
        hook = LUA_HOOKTAILCALL;
    }
    luaD_hook(L, hook, -1);
    ci->u.l.savedpc--; /* correct 'pc' */ //310行多算一个.hook不算pc所以再改回正确的.
}
// actual 是入参实际传入的个数 p是函数袁术巨.
static StkId adjust_varargs(lua_State *L, Proto *p, int actual)
{
    int i;
    int nfixargs = p->numparams; // 应该传入的参数.
    StkId base, fixed;
    /* move fixed parameters to final position */
    fixed = L->top - actual; /* first fixed argument */ //现在第一个参数应该放置的位置.
    base = L->top;           /* final position of first argument */ //第一个参数应该放到的目标位置.
    for (i = 0; i < nfixargs && i < actual; i++)
    {
        setobjs2s(L, L->top++, fixed + i);
        setnilvalue(fixed + i); /* erase original copy (for GC) */
    }
    for (; i < nfixargs; i++)//多余的参数设置为nil. 应该要这些参数,但是传入的数量不够.nil补齐.
        setnilvalue(L->top++); /* complete missing arguments */
    return base;
}

/* 尝试func里面的tag method方法.
** Check whether __call metafield of 'func' is a function. If so, put
** it in stack below original 'func' so that 'luaD_precall' can call
** it. Raise an error if __call metafield is not a function.
*/
static void tryfuncTM(lua_State *L, StkId func)
{
    const TValue *tm = luaT_gettmbyobj(L, func, TM_CALL);
    StkId p;
    if (!ttisfunction(tm))
        luaG_typeerror(L, func, "call");
    /* Open a hole inside the stack at 'func' */
    for (p = L->top; p > func; p--)//都往上移动一个单位
        setobjs2s(L, p, p - 1);
    L->top++;              /* slot ensured by caller */
    setobj2s(L, func, tm); /* tag method is the new function to be called */ //把tm函数放到func上.
}

/*   吧firstResult 和他后面的结果放到res开始位置上. 这个里面这么设计的意思是这个:  首先res表示返回值应该存放的地址, firstresult表示返回值当前存放的地址. wanted表示原型里面要求的几个返回值. nres表示函数实际跑完出来几个返回值. lua比较自由. 可以函数跑出来的返回值跟原型定义的数量不同.  wanted 表示没指定返回几个. 所以根据nres来,表示返回几个都可以. 尽量的都接受即可.
** Given 'nres' results at 'firstResult', move 'wanted' of them to 'res'.
** Handle most typical cases (zero results for commands, one result for
** expressions, multiple results for tail calls/single parameters)
** separated. //把结果放到正确地方.
*/
static int moveresults(lua_State *L, const TValue *firstResult, StkId res,
                       int nres, int wanted)
{
    switch (wanted) //返回多少个值.
    {               /* handle typical cases separately */
    case 0:
        break; /* nothing to move */
    case 1:
    {                                     /* one result needed */
        if (nres == 0)                    /* no results? */
            firstResult = luaO_nilobject; /* adjust with nil */// 用nil来补齐.因为只有0个,但是要1个,所以只能返回nil
        setobjs2s(L, res, firstResult);   /* move it to proper place */
        break;                            //这个函数把firstResult 放到res上.
    }
    case LUA_MULTRET: //表示wanted 没指定返回几个. 所以根据nres来,表示返回几个都可以. 尽量的都接受即可.
    {
        int i;
        for (i = 0; i < nres; i++) /* move all results to correct place */
            setobjs2s(L, res + i, firstResult + i);
        L->top = res + nres; //top指向最后一个返回值再加一的位置.
        return 0; /* wanted == LUA_MULTRET */
    }
    default:  //这种情况说明wanted是一个大于1的整数.
    {
        int i;
        if (wanted <= nres)
        {                                /* enough results? */
            for (i = 0; i < wanted; i++) /* move wanted results to correct place */
                setobjs2s(L, res + i, firstResult + i);
        }
        else
        {                              /* not enough results; use all of them plus nils */
            for (i = 0; i < nres; i++) /* move all results to correct place */
                setobjs2s(L, res + i, firstResult + i);
            for (; i < wanted; i++) /* complete wanted number of results */
                setnilvalue(res + i);
        }
        break;
    }
    }
    L->top = res + wanted; /* top points after the last result */ //放置好返回值, top设置好位置. top始终指向一个第一个空位置.
    return 1;
}

/* 结束一个function call, 如果有hook就调用. hook就是一个触发. 这里面不只说前触发叫hook. 把firstresult放到正确的地方.
** Finishes a function call: calls hook if necessary, removes CallInfo,
** moves current number of results to proper place; returns 0 iff call
** wanted multiple (variable number of) results.
*/
int luaD_poscall(lua_State *L, CallInfo *ci, StkId firstResult, int nres)
{
    StkId res;
    int wanted = ci->nresults;
    if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) // hook相关
    {
        if (L->hookmask & LUA_MASKRET)
        {
            ptrdiff_t fr = savestack(L, firstResult); /* hook may change stack */  //保存firestresult的索引.
            luaD_hook(L, LUA_HOOKRET, -1); //调用hook函数.
            firstResult = restorestack(L, fr);
        }
        L->oldpc = ci->previous->u.l.savedpc; /* 'oldpc' for caller function */
    }
    res = ci->func;                            /* res == final position of 1st result */
    L->ci = ci->previous; /* back to caller */ //返回上一个ci对象. 因为这个函数是他的上级函数调用的.所以这个函数运行完,要吧ci退回即可.
    /* move results to proper place */
    return moveresults(L, firstResult, res, nres, wanted);
}
// ci进入下一个函数.
#define next_ci(L) (L->ci = (L->ci->next ? L->ci->next : luaE_extendCI(L)))

/* macro to check stack size, preserving 'p' */ // L空闲保证有n个,然后保证p位置存在.
#define checkstackp(L, n, p)                                                         \
    luaD_checkstackaux(L, n,                                                         \
                       ptrdiff_t t__ = savestack(L, p); /* save 'p' */               \
                       luaC_checkGC(L),                 /* stack grow uses 运行一次回收.memory */ \
                       p = restorestack(L, t__))        /* 'pos' part: restore 'p' */

/*
** Prepares a function call: checks the stack, creates a new CallInfo
** entry, fills in the relevant information, calls hook if needed.
** If function is a C function, does the call, too. (Otherwise, leave
** the execution ('luaV_execute') to the caller, to allow stackless
** calls.) Returns true iff function has been executed (C function).
*/   //如果是c函数那么进行处理,否则使用luaV_execute进行处理. 是c函数就返回1, 不是c函数就返回0
int luaD_precall(lua_State *L, StkId func, int nresults)
{
    lua_CFunction f;
    CallInfo *ci;
    switch (ttype(func)) // 因为lapi.c:595行进行的设置.
    {
    case LUA_TCCL: /* C closure */   // c闭包函数.也就是有upvalue的函数.
        f = clCvalue(func)->f;
        goto Cfunc;
    case LUA_TLCF: /* light C function */  //单纯的c函数, 这2种case都会进入下面的Cfunc部分.
        f = fvalue(func);
    Cfunc: //c函数的栈是自己维护的.所以不用我们管.他里面不需要做opt码.直接跑完即可.也不触发debug信息. 全交给c处理即可.
    {
        int n;                                          /* number of returns */
        checkstackp(L, LUA_MINSTACK, func);             /* ensure minimum stack size */
        ci = next_ci(L); /* now 'enter' new function */ //创建一个新的ci ,然后把它放入L里面存上.用来存储这个函数调用信息.
        ci->nresults = nresults;
        ci->func = func;
        ci->top = L->top + LUA_MINSTACK; //在栈上开20个单位给这个ci用. 注意这个栈是所有函数公用的. 每个函数ci都往上面放.
        lua_assert(ci->top <= L->stack_last);
        ci->callstatus = 0;
        if (L->hookmask & LUA_MASKCALL) //如果有hook函数.那么就先调用hook
            luaD_hook(L, LUA_HOOKCALL, -1);
        lua_unlock(L);
        n = (*f)(L); /* do the actual call */ // 调用函数本身.
        lua_lock(L);
        api_checknelems(L, n);
        luaD_poscall(L, ci, L->top - n, n); // L->top - n 这个地方为什么放的是返回值???????我理解是.返回值如果是n个,那么就会压入栈n次.top++n次.所以找回首地址就是-n  //如果你有返回值，在被执行的函数中通过lua_push*系列函数，将函数结果返回到栈顶部即可。外部可以通过lua_to*系列函数，获取当前调用栈的栈顶数据信息。 这个我还需要去代码里面找找印证一下. //找到了 lua.c:622行.
        return 1;
    }
    case LUA_TLCL:   // lua closure  //lua闭包函数需要自己调整vararg, 需要自己做opt码, 这里面不进行lua函数的运行, lua函数在之后的execute代码里面运行.
    { /* Lua function: prepare its call */
        StkId base;
        Proto *p = clLvalue(func)->p; //函数的元数据.
        int n = cast_int(L->top - func) - 1; /* number of real arguments */ // 一共有多少个参数
        int fsize = p->maxstacksize;         /* frame size */
        checkstackp(L, fsize, func);//保证函数索引位置前提下拓展栈的大小.
        if (p->is_vararg)//根据参数调整栈的空间和位置.
            base = adjust_varargs(L, p, n);
        else
        { /* non vararg function */
            for (; n < p->numparams; n++)
                setnilvalue(L->top++); /* complete missing arguments */
            base = func + 1;
        }
        ci = next_ci(L); /* now 'enter' new function */
        ci->nresults = nresults;
        ci->func = func;
        ci->u.l.base = base;
        L->top = ci->top = base + fsize;
        lua_assert(ci->top <= L->stack_last);
        ci->u.l.savedpc = p->code; /* starting point */ //pc归零. 本质就是 p->code[0]. 表示从0开始跑opt码.
        ci->callstatus = CIST_LUA;
        if (L->hookmask & LUA_MASKCALL)
            callhook(L, ci);
        return 0;
    }
    default://不是函数的话就在原方法里面找一下. func不是函数,那么调用他就需要找他里面的call方法.
    {                                           /* not a function */
        checkstackp(L, 1, func);                /* ensure space for metamethod */  //保证有一个空闲位置来调用元方法. 351行.需要一个位置来放方法.
        tryfuncTM(L, func);                     /* try to get '__call' metamethod */
        return luaD_precall(L, func, nresults); /* now it must be a function */
    }
    }
}

/*
** Check appropriate error for stack overflow ("regular" overflow or
** overflow while handling stack overflow). If 'nCalls' is larger than
** LUAI_MAXCCALLS (which means it is handling a "regular" overflow) but
** smaller than 9/8 of LUAI_MAXCCALLS, does not report an error (to
** allow overflow handling to work)
*/
static void stackerror(lua_State *L)
{
    if (L->nCcalls == LUAI_MAXCCALLS)
        luaG_runerror(L, "C stack overflow");
    else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS >> 3)))
        luaD_throw(L, LUA_ERRERR); /* error while handing stack error */
}

/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.  //D能是direct意思. 也就是像c函数那种调用栈来处理函数了.
*/
void luaD_call(lua_State *L, StkId func, int nResults)
{
    if (++L->nCcalls >= LUAI_MAXCCALLS) //查看内部调用c函数的数量.
        stackerror(L);
    if (!luaD_precall(L, func, nResults)) /* is a Lua function? */// 因为precall函数返回0表示他是lua函数.所以
        luaV_execute(L);                  /* call it */  //当func是lua函数时候我们调用这行来运行lua函数.
    L->nCcalls--;
}

/*
** Similar to 'luaD_call', but does not allow yields during the call  这个函数不允许被打断.
*/
void luaD_callnoyield(lua_State *L, StkId func, int nResults)
{//不是携程函数就需要用这个函数来调用.
    L->nny++; //nny noyield不是携程函数就需要用这个函数来调用.
    luaD_call(L, func, nResults);
    L->nny--;
}

/* c函数运行结束后,运行这个代码. 他会继续掉一个continue函数.做finish处理.
** Completes the execution of an interrupted C function, calling its
** continuation function.
*/
static void finishCcall(lua_State *L, int status)
{
    CallInfo *ci = L->ci;
    int n;
    /* must have a continuation and must be able to call it */
    lua_assert(ci->u.c.k != NULL && L->nny == 0);
    /* error status can only happen in a protected call */
    lua_assert((ci->callstatus & CIST_YPCALL) || status == LUA_YIELD);
    if (ci->callstatus & CIST_YPCALL)
    {                                     /* was inside a pcall? */
        ci->callstatus &= ~CIST_YPCALL;   /* continuation is also inside it */
        L->errfunc = ci->u.c.old_errfunc; /* with the same error function */
    }
    /* finish 'lua_callk'/'lua_pcall'; CIST_YPCALL and 'errfunc' already
       handled */
    adjustresults(L, ci->nresults);
    lua_unlock(L);
    n = (*ci->u.c.k)(L, status, ci->u.c.ctx); /* call continuation function */
    lua_lock(L);
    api_checknelems(L, n);
    luaD_poscall(L, ci, L->top - n, n); /* finish 'luaD_precall' */
}
//================下面几个函数都是携程相关的.
/* 这个是为了处理携程的东西. unroll铺开的意思. 一直运行后续函数直到站清空. //这个携程的东西之后在看看!!!!!!!!!!!!!!!!!!!
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop). If the coroutine is
** recovering from an error, 'ud' points to the error status, which must
** be passed to the first continuation function (otherwise the default
** status is LUA_YIELD).
*/
static void unroll(lua_State *L, void *ud)
{
    if (ud != NULL)                 /* error status? */
        finishCcall(L, *(int *)ud); /* finish 'lua_pcallk' callee */
    while (L->ci != &L->base_ci)
    {                                  /* something in the stack */
        if (!isLua(L->ci))             /* C function? */
            finishCcall(L, LUA_YIELD); /* complete its execution */
        else
        {                     /* Lua function */
            luaV_finishOp(L); /* finish interrupted instruction */
            luaV_execute(L);  /* execute down to higher C 'boundary' */
        }
    }
}

/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallInfo *findpcall(lua_State *L)
{
    CallInfo *ci;
    for (ci = L->ci; ci != NULL; ci = ci->previous)
    { /* search for a pcall */  //当前进程挂起就找之前暂停的,启动其他的即可.
        if (ci->callstatus & CIST_YPCALL)
            return ci;
    }
    return NULL; /* no pending pcall */
}

/* 有错误的话就停止这个携程,然后找其他携程启动.
** Recovers from an error in a coroutine. Finds a recover point (if
** there is one) and completes the execution of the interrupted
** 'luaD_pcall'. If there is no recover point, returns zero.
*/
static int recover(lua_State *L, int status)
{
    StkId oldtop;
    CallInfo *ci = findpcall(L);
    if (ci == NULL)
        return 0; /* no recovery point */
    /* "finish" luaD_pcall */
    oldtop = restorestack(L, ci->extra);//回复旧的栈top
    luaF_close(L, oldtop);
    seterrorobj(L, status, oldtop);
    L->ci = ci;
    L->allowhook = getoah(ci->callstatus); /* restore original 'allowhook' */
    L->nny = 0;                            /* should be zero to be yieldable */
    luaD_shrinkstack(L);
    L->errfunc = ci->u.c.old_errfunc;
    return 1; /* continue running the coroutine */
}

/*
** Signal an error in the call to 'lua_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error(lua_State *L, const char *msg, int narg)
{
    L->top -= narg;                           /* remove args from the stack */
    setsvalue2s(L, L->top, luaS_new(L, msg)); /* push error message */
    api_incr_top(L);
    lua_unlock(L);
    return LUA_ERRRUN;
}

/*
** Do the work for 'lua_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume(lua_State *L, void *ud)
{
    int n = *(cast(int *, ud));  /* number of arguments */
    StkId firstArg = L->top - n; /* first argument */
    CallInfo *ci = L->ci;
    if (L->status == LUA_OK)
    {                                                    /* starting a coroutine? */
        if (!luaD_precall(L, firstArg - 1, LUA_MULTRET)) /* Lua function? */
            luaV_execute(L);                             /* call it */
    }
    else
    { /* resuming from previous yield */
        lua_assert(L->status == LUA_YIELD);
        L->status = LUA_OK; /* mark that it is running (again) */
        ci->func = restorestack(L, ci->extra);
        if (isLua(ci))       /* yielded inside a hook? */
            luaV_execute(L); /* just continue running Lua code */
        else
        { /* 'common' yield */
            if (ci->u.c.k != NULL)
            { /* does it have a continuation function? */
                lua_unlock(L);
                n = (*ci->u.c.k)(L, LUA_YIELD, ci->u.c.ctx); /* call continuation */
                lua_lock(L);
                api_checknelems(L, n);
                firstArg = L->top - n; /* yield results come from continuation */
            }
            luaD_poscall(L, ci, firstArg, n); /* finish 'luaD_precall' */
        }
        unroll(L, NULL); /* run continuation */
    }
}

LUA_API int lua_resume(lua_State *L, lua_State *from, int nargs)
{
    int status;
    unsigned short oldnny = L->nny; /* save "number of non-yieldable" calls */
    lua_lock(L);
    if (L->status == LUA_OK)
    {                             /* may be starting a coroutine */
        if (L->ci != &L->base_ci) /* not in base level? */
            return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    }
    else if (L->status != LUA_YIELD)
        return resume_error(L, "cannot resume dead coroutine", nargs);
    L->nCcalls = (from) ? from->nCcalls + 1 : 1;
    if (L->nCcalls >= LUAI_MAXCCALLS)
        return resume_error(L, "C stack overflow", nargs);
    luai_userstateresume(L, nargs);
    L->nny = 0; /* allow yields */
    api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
    status = luaD_rawrunprotected(L, resume, &nargs);
    if (status == -1) /* error calling 'lua_resume'? */
        status = LUA_ERRRUN;
    else
    { /* continue running after recoverable errors */
        while (errorstatus(status) && recover(L, status))
        {
            /* unroll continuation */
            status = luaD_rawrunprotected(L, unroll, &status);
        }
        if (errorstatus(status))
        {                                   /* unrecoverable error? */
            L->status = cast_byte(status);  /* mark thread as 'dead' */
            seterrorobj(L, status, L->top); /* push error message */
            L->ci->top = L->top;
        }
        else
            lua_assert(status == L->status); /* normal end or yield */
    }
    L->nny = oldnny; /* restore 'nny' */
    L->nCcalls--;
    lua_assert(L->nCcalls == ((from) ? from->nCcalls : 0));
    lua_unlock(L);
    return status;
}

LUA_API int lua_isyieldable(lua_State *L)
{
    return (L->nny == 0);
}

LUA_API int lua_yieldk(lua_State *L, int nresults, lua_KContext ctx,
                       lua_KFunction k)
{
    CallInfo *ci = L->ci;
    luai_userstateyield(L, nresults);
    lua_lock(L);
    api_checknelems(L, nresults);
    if (L->nny > 0)
    {
        if (L != G(L)->mainthread)
            luaG_runerror(L, "attempt to yield across a C-call boundary");
        else
            luaG_runerror(L, "attempt to yield from outside a coroutine");
    }
    L->status = LUA_YIELD;
    ci->extra = savestack(L, ci->func); /* save current 'func' */
    if (isLua(ci))
    { /* inside a hook? */
        api_check(L, k == NULL, "hooks cannot continue after yielding");
    }
    else
    {
        if ((ci->u.c.k = k) != NULL)      /* is there a continuation? */
            ci->u.c.ctx = ctx;            /* save context */
        ci->func = L->top - nresults - 1; /* protect stack below results */
        luaD_throw(L, LUA_YIELD);
    }
    lua_assert(ci->callstatus & CIST_HOOKED); /* must be inside a hook */
    lua_unlock(L);
    return 0; /* return to 'luaD_hook' */
}
//整个函数调用的最顶层. 最用户态的api  ,old_top是这个func的函数位置,也就是他父函数的top. 注意top都是指放东西的最高层+1的位置!
int luaD_pcall(lua_State *L, Pfunc func, void *u,
               ptrdiff_t old_top, ptrdiff_t ef)
{
    int status;
    CallInfo *old_ci = L->ci;
    lu_byte old_allowhooks = L->allowhook;
    unsigned short old_nny = L->nny;
    ptrdiff_t old_errfunc = L->errfunc;
    L->errfunc = ef;
    status = luaD_rawrunprotected(L, func, u);//调用try catch 进行保护.
    if (status != LUA_OK)
    { /* an error occurred? */   //如果调用失败. 那么就进行站的恢复
        StkId oldtop = restorestack(L, old_top);
        luaF_close(L, oldtop); /* close possible pending closures */  // oldtop是调用这个函数之前的栈顶. 所以理解了top
        seterrorobj(L, status, oldtop);
        L->ci = old_ci;
        L->allowhook = old_allowhooks;
        L->nny = old_nny;
        luaD_shrinkstack(L);
    }
    L->errfunc = old_errfunc;
    return status;
}

/*
** Execute a protected parser.
*/
struct SParser
{ /* data to 'f_parser' */
    ZIO *z;
    Mbuffer buff; /* dynamic structure used by the scanner */
    Dyndata dyd;  /* dynamic structures used by the parser */
    const char *mode;
    const char *name;
};
//检查文件是不是这个mode
static void checkmode(lua_State *L, const char *mode, const char *x)
{
    if (mode && strchr(mode, x[0]) == NULL)//如果mode存在的话,查询mode里面是否有x[0]这个字符.
    {
        luaO_pushfstring(L,
                         "attempt to load a %s chunk (mode is '%s')", x, mode);
        luaD_throw(L, LUA_ERRSYNTAX);
    }
}

static void f_parser(lua_State *L, void *ud)
{
    LClosure *cl;
    struct SParser *p = cast(struct SParser *, ud);// ud估计是userdata表示传入的数据.
    int c = zgetc(p->z); /* read first character */  //zgetc get char函数.
    if (c == LUA_SIGNATURE[0])//从lua二进制解析, 已经是opt码.
    {
        checkmode(L, p->mode, "binary");
        cl = luaU_undump(L, p->z, p->name);
    }
    else
    {//从纯文本解析
        checkmode(L, p->mode, "text");
        cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
    }
    lua_assert(cl->nupvalues == cl->p->sizeupvalues);
    luaF_initupvals(L, cl);
}
//运行带保护的解析程序.
int luaD_protectedparser(lua_State *L, ZIO *z, const char *name,
                         const char *mode)
{
    struct SParser p;
    int status;
    L->nny++; /* cannot yield during parsing */
    p.z = z;
    p.name = name;
    p.mode = mode;
    p.dyd.actvar.arr = NULL;
    p.dyd.actvar.size = 0;
    p.dyd.gt.arr = NULL;
    p.dyd.gt.size = 0;
    p.dyd.label.arr = NULL;
    p.dyd.label.size = 0;
    luaZ_initbuffer(L, &p.buff);
    status = luaD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc); // 初始化一个parse穿进去.
    luaZ_freebuffer(L, &p.buff);
    luaM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
    luaM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
    luaM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
    L->nny--;
    return status;
}
