/*
** $Id: lparser.h,v 1.76.1.1 2017/04/19 17:20:42 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/*
** Expression and variable descriptor.
** Code generation for variables and expressions can be delayed to allow
** optimizations; An 'expdesc' structure describes a potentially-delayed
** variable/expression. It has a description of its "main" value plus a
** list of conditional jumps that can also produce its value (generated
** by short-circuit operators 'and'/'or').
*/

/* kinds of variables/expressions */ //变量和表达式的类型.
typedef enum {
  VVOID,  /* when 'expdesc' describes the last expression a list,
             this kind means an empty list (so, no expression) */
  VNIL,  /* constant nil */
  VTRUE,  /* constant true */
  VFALSE,  /* constant false */
  VK,  /* constant in 'k'; info = index of constant in 'k' */ //字符型的const, info里面存的是他的索引.
  VKFLT,  /* floating constant; nval = numerical float value */
  VKINT,  /* integer constant; nval = numerical integer value */
  VNONRELOC,  /* expression has its value in a fixed register;
                 info = result register */ //已经放入寄存器了. info里面放的寄存器地址. 这个变量不用再分配了. 不可以再分配模式
  VLOCAL,  /* local variable; info = local register */ //放入寄存器了.
  VUPVAL,  /* upvalue variable; info = index of upvalue in 'upvalues' */
  VINDEXED,  /* indexed variable;
                ind.vt = whether 't' is register or upvalue;
                ind.t = table register or upvalue;
                ind.idx = key's R/K index */ //已经计算完索引的变量.
  VJMP,  /* expression is a test/comparison;
            info = pc of corresponding jump instruction */
  VRELOCABLE,  /* expression can put result in any register;
                  info = instruction pc */ //可以被分配的变量. 表示他还没有被分配.
  VCALL,  /* expression is a function call; info = instruction pc */
  VVARARG  /* vararg expression; info = instruction pc */
} expkind;


#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)	((k) == VNONRELOC || (k) == VLOCAL)
//表达式的具体描述. 也及时表达式的结构体. 存数据的.
typedef struct expdesc {
  expkind k; //类别.
  union {
    lua_Integer ival;    /* for VKINT */
    lua_Number nval;  /* for VKFLT */
    int info;  /* for generic use */
    struct {  /* for indexed variables (VINDEXED) */
      short idx;  /* index (R/K) */
      lu_byte t;  /* table (register or upvalue) */
      lu_byte vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
    } ind;   //ind : inner discription 内在结构.的意思.
  } u;   //用来保存他里面的值. 所以用一个union. 保存int float  ind用来保存变量.
  int t;  /* patch list of 'exit when true' */  //true时候的触发函数 //表达式等于true就会触发这里面的函数.
  int f;  /* patch list of 'exit when false' */
} expdesc;


/* description of active local variable *///局部变量.
typedef struct Vardesc {
  short idx;  /* variable index in stack */
} Vardesc;

//跳转和标签的描述.   //就是记录的pc和代码行数.
/* description of pending goto statements and label statements */
typedef struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */ //opt里面的索引
  int line;  /* line where it appeared */  //代码行号
  lu_byte nactvar;  /* local level where it appears in current block */ //当前level
} Labeldesc;


/* list of labels or gotos */ // 上面的Labeldesc组成的数组.所以是一个label的列表.
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
typedef struct Dyndata {  //一个parser需要维护, 所有的局部变量. gotos和lables
  struct {  /* list of active local variables */
    Vardesc *arr;
    int n;
    int size;
  } actvar;
  Labellist gt;  /* list of pending gotos */ // 空悬的goto语句. 在closegoto时候会回填他的值.
  Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */ //代码块的结构体
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */ //funcstate:用来描述当前函数的状态信息
typedef struct FuncState {
  Proto *f;  /* current function header */ //函数的元数据,也就是参数,upvalue等东西
  struct FuncState *prev;  /* enclosing function */ // 当前函数的外层函数
  struct LexState *ls;  /* lexical state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to 'ncode') */  //我根据代码这个的理解是pc值得是 opt码的计数. 表示下一条指令的opt计数. 指令计数器是用于存放下一条指令所在单元的地址的地方。
  int lasttarget;   /* 'label' of last 'jump label' */
  int jpc;  /* list of pending jumps to 'pc' */  // 其他jump到这个pc的jump 也是一个链表.
  int nk;  /* number of elements in 'k' */  //多少个常数
  int np;  /* number of elements in 'p' */  //多少个子函数
  int firstlocal;  /* index of first local var (in Dyndata array) */
  short nlocvars;  /* number of elements in 'f->locvars' */
  lu_byte nactvar;  /* number of active local variables */
  lu_byte nups;  /* number of upvalues */
  lu_byte freereg;  /* first free register */
} FuncState;


LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


#endif
