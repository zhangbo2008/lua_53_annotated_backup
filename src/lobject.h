/*
** $Id: lobject.h,v 2.117.1.1 2017/04/19 17:39:34 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/
#include <stdio.h>

#ifndef lobject_h
#define lobject_h

#include <stdarg.h>

#include "llimits.h"
#include "lua.h"

/*
** Extra tags for non-values
*/
#define LUA_TPROTO LUA_NUMTAGS         /* function prototypes */
#define LUA_TDEADKEY (LUA_NUMTAGS + 1) /* removed keys in tables */

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS (LUA_TPROTO + 2)

/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

//---------------------------------------------------------------------
// 值类型标记说明：
// 0-3 位：父类型
// 4-5位：子类型
// 6位：是否可垃圾回收
//---------------------------------------------------------------------

/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */

//---------------------------------------------------------------------
// 0 - Lua 闭包
// 1 - 轻量的C函数
// 2 - C 闭包
//---------------------------------------------------------------------
//这里面<<4.是因为类型一共有8个.正好二进制是1000,所以<<4是最优的不干扰其他数据的偏移量.
#define LUA_TLCL (LUA_TFUNCTION | (0 << 4)) /* Lua closure */
#define LUA_TLCF (LUA_TFUNCTION | (1 << 4)) /* light C function */
#define LUA_TCCL (LUA_TFUNCTION | (2 << 4)) /* C closure */

/* Variant tags for strings */

//---------------------------------------------------------------------
// 0 - 短字符串
// 1 - 长字符串
//---------------------------------------------------------------------
#define LUA_TSHRSTR (LUA_TSTRING | (0 << 4)) /* short strings */
#define LUA_TLNGSTR (LUA_TSTRING | (1 << 4)) /* long strings */

/* Variant tags for numbers */

//---------------------------------------------------------------------
// 0 - 浮点数
// 1 - 整数
//---------------------------------------------------------------------
#define LUA_TNUMFLT (LUA_TNUMBER | (0 << 4)) /* float numbers */
#define LUA_TNUMINT (LUA_TNUMBER | (1 << 4)) /* integer numbers */

/* Bit mark for collectable types */
// 可垃圾回收位
#define BIT_ISCOLLECTABLE (1 << 6)

/* mark a tag as collectable */
// 标记为可回收
#define ctb(t) ((t) | BIT_ISCOLLECTABLE)

/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;

/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
#define CommonHeader \
  GCObject *next;    \
  lu_byte tt;        \
  lu_byte marked

/*
** Common type has only the common header
*/
//这个注释里面是等价的写法.
//---------------------------------------------------------------------
// struct GCObject {
//   GCObject  *next;       // 指向下一个GC对象，形成链表，GC会用到
//   lu_bype    tt;         // 对象类型
//   lu_byte    marked;     // 对象标记，GC会用到
// };
//---------------------------------------------------------------------
//我们推到一下上面这个结构体如何生成的
// #define CommonHeader GCObject *next; lu_byte tt; lu_byte marked 因为这个宏定义.所以我们把这个带入下面一行中.我们就得到了结果. 这份代码里面大量利用了这种中间变量宏定义写法比较绕. 推荐还是自己推导成最习惯的写法来看.也就是上面注释里面那种.
struct GCObject
{
  CommonHeader;
};

/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/*
** Union of all Lua values
*/
typedef union Value // 联合体, 这些东西都共享一个内存. 每次只能存一种东西.
{
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
} Value;

#define TValuefields \
  Value value_;      \
  int tt_

//---------------------------------------------------------------------
// TValue {
//   Value value_;  // union 类型的值
//   int tt_;       // 类型标记
// };
//---------------------------------------------------------------------
typedef struct lua_TValue
{
  TValuefields;
} TValue; // 最终lua里面类型封装是 数据加一个类型标记!

/* macro defining a nil value */
// 初始化一个 nil 值
#define NILCONSTANT {NULL}, LUA_TNIL

// 获取值
#define val_(o) ((o)->value_)

/* raw type tag of a TValue */
// 获取类型
#define rttype(o) ((o)->tt_) // tt 是type tag的缩写.

/* tag with no variants (bits 0-3) */
#define novariant(x) ((x)&0x0F) //获取tag的基础版本, 因为变化版本bits是4位以上. 看56行.

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
// 父类型 + 子类型
#define ttype(o) (rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
// 父类型
#define ttnov(o) (novariant(rttype(o)))

/* Macros to test type */

//---------------------------------------------------------------------
// 测试类型的宏
// 垃圾回收类型有：
//   string
//    短字符串
//    长字符串
//   userdata
//   closure
//     lua 闭包
//     C 闭包（包含上值的 C 函数）
//   表
//   proto
//   thread
//---------------------------------------------------------------------
#define checktag(o, t) (rttype(o) == (t))
#define checktype(o, t) (ttnov(o) == (t))
#define ttisnumber(o) checktype((o), LUA_TNUMBER)
#define ttisfloat(o) checktag((o), LUA_TNUMFLT)
#define ttisinteger(o) checktag((o), LUA_TNUMINT)
#define ttisnil(o) checktag((o), LUA_TNIL)
#define ttisboolean(o) checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o) checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o) checktype((o), LUA_TSTRING)
#define ttisshrstring(o) checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o) checktag((o), ctb(LUA_TLNGSTR))
#define ttistable(o) checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o) checktype(o, LUA_TFUNCTION)
#define ttisclosure(o) ((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o) checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o) checktag((o), ctb(LUA_TLCL))
#define ttislcf(o) checktag((o), LUA_TLCF)
#define ttisfulluserdata(o) checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o) checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o) checktag((o), LUA_TDEADKEY)

/* Macros to access values */
// 检测类型并读取实际内部的值
#define ivalue(o) check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o) check_exp(ttisfloat(o), val_(o).n)
#define nvalue(o) check_exp(ttisnumber(o), \
                            (ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
#define gcvalue(o) check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o) check_exp(ttislightuserdata(o), val_(o).p)
#define tsvalue(o) check_exp(ttisstring(o), gco2ts(val_(o).gc))
#define uvalue(o) check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o) check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o) check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o) check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
#define fvalue(o) check_exp(ttislcf(o), val_(o).f)
#define hvalue(o) check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o) check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o) check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o) check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

#define l_isfalse(o) (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

// 是否为可回收对象
#define iscollectable(o) (rttype(o) & BIT_ISCOLLECTABLE)

/* Macros for internal tests */
#define righttt(obj) (ttype(obj) == gcvalue(obj)->tt)

#define checkliveness(L, obj)           \
  lua_longassert(!iscollectable(obj) || \
                 (righttt(obj) && (L == NULL || !isdead(G(L), gcvalue(obj)))))

/* Macros to set values */
#define settt_(o, t) ((o)->tt_ = (t))

#define setfltvalue(obj, x)  \
  {                          \
    TValue *io = (obj);      \
    val_(io).n = (x);        \
    settt_(io, LUA_TNUMFLT); \
  }

#define chgfltvalue(obj, x)    \
  {                            \
    TValue *io = (obj);        \
    lua_assert(ttisfloat(io)); \
    val_(io).n = (x);          \
  }

#define setivalue(obj, x)    \
  {                          \
    TValue *io = (obj);      \
    val_(io).i = (x);        \
    settt_(io, LUA_TNUMINT); \
  }

#define chgivalue(obj, x)        \
  {                              \
    TValue *io = (obj);          \
    lua_assert(ttisinteger(io)); \
    val_(io).i = (x);            \
  }

#define setnilvalue(obj) settt_(obj, LUA_TNIL)
//先把右边obj这个地址,重命名为io.然后设置io的val 和 tt 即可. tt是linghtfunction    /*//设置value这个obj里面的函数field的值为x*/
#define setfvalue(obj, x) \   
  {                       \
    TValue *io = (obj);   \               
    val_(io).f = (x);     \           
    settt_(io, LUA_TLCF); \             
  }  


#define setpvalue(obj, x)           \
  {                                 \
    TValue *io = (obj);             \
    val_(io).p = (x);               \
    settt_(io, LUA_TLIGHTUSERDATA); \
  }

#define setbvalue(obj, x)     \
  {                           \
    TValue *io = (obj);       \
    val_(io).b = (x);         \
    settt_(io, LUA_TBOOLEAN); \
  }

#define setgcovalue(L, obj, x) \
  {                            \
    TValue *io = (obj);        \
    GCObject *i_g = (x);       \
    val_(io).gc = i_g;         \
    settt_(io, ctb(i_g->tt));  \
  }

#define setsvalue(L, obj, x)   \
  {                            \
    TValue *io = (obj);        \
    TString *x_ = (x);         \
    val_(io).gc = obj2gco(x_); \
    settt_(io, ctb(x_->tt));   \
    checkliveness(L, io);      \
  }

#define setuvalue(L, obj, x)        \
  {                                 \
    TValue *io = (obj);             \
    Udata *x_ = (x);                \
    val_(io).gc = obj2gco(x_);      \
    settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L, io);           \
  }

#define setthvalue(L, obj, x)     \
  {                               \
    TValue *io = (obj);           \
    lua_State *x_ = (x);          \
    val_(io).gc = obj2gco(x_);    \
    settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L, io);         \
  }

#define setclLvalue(L, obj, x) \
  {                            \
    TValue *io = (obj);        \
    LClosure *x_ = (x);        \
    val_(io).gc = obj2gco(x_); \
    settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L, io);      \
  }
//x转化为c闭包,结果是obj
#define setclCvalue(L, obj, x) \
  {                            \
    TValue *io = (obj);        \
    CClosure *x_ = (x);        \
    val_(io).gc = obj2gco(x_); \
    settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L, io);      \
  }
//x转化为哈希表,转化后的结果记做io
#define sethvalue(L, obj, x)     \
  {                              \
    TValue *io = (obj);          \
    Table *x_ = (x);             \
    val_(io).gc = obj2gco(x_);   \
    settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L, io);        \
  }

#define setdeadvalue(obj) settt_(obj, LUA_TDEADKEY)

#define setobj(L, obj1, obj2) \
  {                           \
    TValue *io1 = (obj1);     \
    *io1 = *(obj2);           \
    (void)L;                  \
    checkliveness(L, io1);    \
  }

/*
** different types of assignments, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s setobj
/* to stack (not from same stack) */
#define setobj2s setobj
#define setsvalue2s setsvalue
#define sethvalue2s sethvalue
#define setptvalue2s setptvalue
/* from table to same table */
#define setobjt2t setobj
/* to new object */
#define setobj2n setobj
#define setsvalue2n setsvalue

/* to table (define it as an expression to be used in macros) */
#define setobj2t(L, o1, o2) ((void)L, *(o1) = *(o2), checkliveness(L, (o1)))

/*
** {======================================================
** types and prototypes
** =======================================================
*/

typedef TValue *StkId; /* index to stack elements */
//   TValue 的指针就是stkid 也就是栈的索引. 指向栈里面的内容的地址.
/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next). 这个结构存放string的第一个字母的地址.
*/

//---------------------------------------------------------------------
// 字符串 {
//   UTString       // 头部
//   char[]         // 实际存放字符串数据
// }
//---------------------------------------------------------------------
typedef struct TString
{
  CommonHeader;
  lu_byte extra; /* reserved words for short strings; "has hash" for longs */
  // 短字符串长度，最大长度为256
  lu_byte shrlen; /* length for short strings */
  unsigned int hash;
  union
  {
    size_t lnglen;         /* length for long strings */
    struct TString *hnext; /* linked list for hash table */
  } u;
} TString;

/*
** Ensures that address after this type is always fully aligned.
*/
// 内存对齐用，其他几节也有类似定义,只是用于计算下面sizeof的.
typedef union UTString
{
  L_Umaxalign dummy; /* ensures maximum alignment for strings */ //保证了cpu读取足够长的内容.加速了读取.因为使用了union来包这个.所以长度取最大长度.也就是统一8字节. 64位的指针是8字节.8字节=64位. 一个字节=8位.
  TString tsv;
} UTString;

/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/

// 获取字符串数据部分
#define getstr(ts) \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))

/* get the actual string (array of bytes) from a Lua value */
#define svalue(o) getstr(tsvalue(o))

/* get string length from 'TString *s' */
// 获取字符串长度
#define tsslen(s) ((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
#define vslen(o) tsslen(tsvalue(o))

/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/

//---------------------------------------------------------------------
// userdata {
//   UUdata         // 头部
//   u_char[]       // 实际数据部分
// }
//---------------------------------------------------------------------
typedef struct Udata // 对上面Value又包装了一层.加了一个分配表.加了一个元表.
{
  CommonHeader;
  lu_byte ttuv_; /* user value's tag */
  // 元表
  struct Table *metatable;
  // 分配的内存大小
  size_t len; /* number of bytes */
  // 额外的 ud，类型由 ttuv_ 指出
  union Value user_; /* user value */
} Udata;

/*
** Ensures that address after this type is always fully aligned. //跟上面字符串一样.还是为了保证内存对齐!
*/
typedef union UUdata
{
  L_Umaxalign dummy; /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;

/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
//跟上面string理解一样.
#define getudatamem(u) \
  check_exp(sizeof((u)->ttuv_), (cast(char *, (u)) + sizeof(UUdata)))

#define setuservalue(L, u, o) \
  {                           \
    const TValue *io = (o);   \
    Udata *iu = (u);          \
    iu->user_ = io->value_;   \
    iu->ttuv_ = rttype(io);   \
    checkliveness(L, io);     \
  }
//把u给o
#define getuservalue(L, u, o) \
  {                           \
    TValue *io = (o);         \
    const Udata *iu = (u);    \
    io->value_ = iu->user_;   \
    settt_(io, iu->ttuv_);    \
    checkliveness(L, io);     \
  }

/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc
{
  TString *name;   /* upvalue name (for debug information) */
  lu_byte instack; /* whether it is in stack (register) */
  lu_byte idx;     /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;

/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar
{
  TString *varname;
  int startpc; /* first point where variable is active */ //作用域. 这个pc表示第几行代码这个变量开始使用
  int endpc;   /* first point where variable is dead */  //这个表示第几行代码这个变量开始无效了.
} LocVar;

/*
** Function Prototypes   函数的元数据.
*/
typedef struct Proto
{
  CommonHeader;
  lu_byte numparams; /* number of fixed parameters */
  lu_byte is_vararg; //函数是否有变参.也就是...
  lu_byte maxstacksize; /* number of registers needed by this function */
  int sizeupvalues;     /* size of 'upvalues' */
  int sizek;            /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep; /* size of 'p' */ // 表示里面有多少个子proto.对应549行的内容数量. size在这个套源码里面表示的是容量的意思. 也就是最大存储多少.
  int sizelocvars;
  int linedefined;        /* debug information  */
  int lastlinedefined;    /* debug information  */
  TValue *k;              /* constants used by the function */ //函数使用的常数表!
  Instruction *code;      /* opcodes */  // opt码都放到这个数组里面
  struct Proto **p;       /* functions defined inside the function */ // 这个表示他的子函数们, 用的proto组成的数组!!!!闭包创建时候需要使用这个.
  int *lineinfo;          /* map from opcodes to source lines (debug information) */
  LocVar *locvars;        /* information about local variables (debug information) */
  Upvaldesc *upvalues;    /* upvalue information */
  struct LClosure *cache; /* last-created closure with this prototype */
  TString *source;        /* used for debug information */
  GCObject *gclist;
} Proto;

/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;

/*
** Closures
*/

#define ClosureHeader \
  CommonHeader;       \
  lu_byte nupvalues;  \
  GCObject *gclist
//c 闭包函数.
typedef struct CClosure
{
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1]; /* list of upvalues */
} CClosure;
// lua 闭包函数
typedef struct LClosure
{
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1]; /* list of upvalues */   // *的级别比[]要高,所以有些都写作int* a[1], 表示一个数组.里面每一个元素都是int类型指针
} LClosure;

typedef union Closure
{
  CClosure c;
  LClosure l;
} Closure;

#define isLfunction(o) ttisLclosure(o)

#define getproto(o) (clLvalue(o)->p)

/*
** Tables
*/

//---------------------------------------------------------------------
// TValue | TValue + next      说明他里面可以存Tvalue又可以存Tvalue+next
//---------------------------------------------------------------------
typedef union TKey
{
  struct
  {
    TValuefields;
    int next; /* for chaining (offset for next node) */// next存的是偏移量.
  } nk;
  TValue tvk;
} TKey;

/* copy a value into a key without messing up field 'next' */
#define setnodekey(L, key, obj)  \
  {                              \
    TKey *k_ = (key);            \
    const TValue *io_ = (obj);   \
    k_->nk.value_ = io_->value_; \
    k_->nk.tt_ = io_->tt_;       \
    (void)L;                     \
    checkliveness(L, io_);       \
  }

typedef struct Node
{
  TValue i_val;
  TKey i_key;
} Node;

typedef struct Table
{
  CommonHeader;
  lu_byte flags;          /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;      /* log2 of size of 'node' array */
  unsigned int sizearray; /* size of 'array' array */
  TValue *array;          /* array part */
  Node *node;
  Node *lastfree; /* any free position is before this position */
  struct Table *metatable;
  GCObject *gclist;
} Table;

/*
** 'module' operation for hashing (size is always a power of 2)
*/
#define lmod(s, size) \
  (check_exp((size & (size - 1)) == 0, (cast(int, (s) & ((size)-1)))))

#define twoto(x) (1 << (x))
#define sizenode(t) (twoto((t)->lsizenode))

/*
** (address of) a fixed nil value
*/
#define luaO_nilobject (&luaO_nilobject_)

LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ 8

LUAI_FUNC int luaO_int2fb(unsigned int x);
LUAI_FUNC int luaO_fb2int(int x);
LUAI_FUNC int luaO_utf8esc(char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2(unsigned int x);
LUAI_FUNC void luaO_arith(lua_State *L, int op, const TValue *p1,
                          const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num(const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue(int c);
LUAI_FUNC void luaO_tostring(lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring(lua_State *L, const char *fmt,
                                        va_list argp);
LUAI_FUNC const char *luaO_pushfstring(lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid(char *out, const char *source, size_t len);

#endif
