/*
** $Id: lcode.c,v 2.112.1.1 2017/04/19 17:20:42 roberto Exp $
** Code generator for Lua
** See Copyright Notice in lua.h
*///////////这个代码跟 lparser.c一起作为解析代码的用途.

#define lcode_c
#define LUA_CORE

#include "lprefix.h"


#include <math.h>
#include <stdlib.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/* Maximum number of registers in a Lua function (must fit in 8 bits) */
#define MAXREGS		255

//用来判断是一个变量还是常数.
#define hasjumps(e)	((e)->t != (e)->f)


/*//就是把e里面的数字类型数据放到v里面.
** If expression is a numeric constant, fills 'v' with its value
** and returns 1. Otherwise, returns 0.
*/
static int tonumeral(const expdesc *e, TValue *v) {
  if (hasjumps(e))
    return 0;  /* not a numeral */
  switch (e->k) {
    case VKINT:
      if (v) setivalue(v, e->u.ival);
      return 1;
    case VKFLT:
      if (v) setfltvalue(v, e->u.nval);
      return 1;
    default: return 0;
  }
}

//效果寄存器从from, 设置n个寄存器都是nil
/* //指令用来创建 nil值变量.   // lopcodes.h里面找到他的对应实现 是 	A B	R(A), R(A+1), ..., R(A+B) := nil  // 就以这个函数为例子来实现操作码.   首先我们知道指令模式有4种. 1: iABC  op A B C   2: iABx    op A Bx  3: iAsBx   op A sBx  4: iAx  op Ax 这里面x表示拓展. 例如Bx就表示BC合起来只表示B  sBx就是带符号的Bx.   那么我们的OP_LOADNIL对应哪一个指令呢.  去lopcodes.c里面查. 就是 opmode(0, 1, OpArgU, OpArgN, iABC)		/-- OP_LOADNIL--/ 这个模式里面的东西对应的说明是 :  OpArgN,  - * argument is not used *    OpArgU,  * argument is used *   OpArgR,   -* argument is a register or a jump offset *   OpArgK     * argument is a constant or register/constant *  . 对于这个里面就是说 0表示我们不做test, 1表示用了A, OpArgU表示用了B, OpArgN表示没用C, mod是iABC. 所以代码82行是luaK_codeABC 参数op: OP_LOADNIL  from表示nil起始开始复制的索引.所以就是opt吗里面的a.所以我们用from放入a位置.  c因为不实用所以填入0. b 参考R(A+1), ..., R(A+B) := nil 知道B表示的是个数-1, 所以64行函数n表示个数.所以b需要填入n-1即可. 这个例子就是基本的opt码生成原理,其他都类似可以自己参考分析.
** Create a OP_LOADNIL instruction, but try to optimize: if the previous
** instruction is also OP_LOADNIL and ranges are compatible, adjust
** range of previous instruction instead of emitting a new one. (For
** instance, 'local a; local b' will generate a single opcode.)
*/
void luaK_nil (FuncState *fs, int from, int n) {
  Instruction *previous;
  int l = from + n - 1;  /* last register to set nil */
  if (fs->pc > fs->lasttarget) {  /* no jumps to current position? */
    previous = &fs->f->code[fs->pc-1];
    if (GET_OPCODE(*previous) == OP_LOADNIL) {  /* previous is LOADNIL? */
      int pfrom = GETARG_A(*previous);  /* get previous range */
      int pl = pfrom + GETARG_B(*previous);
      if ((pfrom <= from && from <= pl + 1) ||
          (from <= pfrom && pfrom <= l + 1)) {  /* can connect both? */
        if (pfrom < from) from = pfrom;  /* from = min(from, pfrom) */
        if (pl > l) l = pl;  /* l = max(l, pl) */
        SETARG_A(*previous, from);
        SETARG_B(*previous, l - from);
        return;
      }
    }  /* else go through */
  }
  luaK_codeABC(fs, OP_LOADNIL, from, n - 1, 0);  /* else no optimization */
}

//====================下面几个函数是处理jump的.
/*//   分析这个: opmode(0, 0, OpArgR, OpArgN, iAsBx)		 -* OP_JMP - *   //首先还是贴上需要的东西. OP_JMP   *	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1)	* 就是 不用a,不是test, 然后pc+=sBx即可.
** Gets the destination address of a jump instruction. Used to traverse
** a list of jumps.
*/
static int getjump (FuncState *fs, int pc) {
  int offset = GETARG_sBx(fs->f->code[pc]);// 拿出指令码,算出offset
  if (offset == NO_JUMP)  /* point to itself represents end of list */
    return NO_JUMP;  /* end of list */
  else
    return (pc+1)+offset;  /* turn offset into absolute position */ //为什么要加一??????????????105行,设置时候偏移量就是-1的.我有点懂了.这个地方应该是这样的.我理解是 OP_JMP,/A sBx 	pc+=sBx; if (A) close all upvalues >= R(A - 1)	/ 这个注释写错了.应该是 pc+=sBx+1 ,因为NO_JUMP定义为-1表示不跳转.所以只能每一个偏移量再加一, 把-1变成0才是不跳转. 后面读代码再看看到底咋回事.感觉还是有坑.
}


/* 让pc的jump指零目的地是dest.
** Fix jump instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua)
*/
static void fixjump (FuncState *fs, int pc, int dest) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest - (pc + 1);//注意这个地方偏移量就是-1的!!!!!!我理解这个位置是因为pc本身也加加.所以offset多-1才对!
  lua_assert(dest != NO_JUMP);
  if (abs(offset) > MAXARG_sBx)
    luaX_syntaxerror(fs->ls, "control structure too long");
  SETARG_sBx(*jmp, offset);
}


/*//l2链表链接到l1的后面   
** Concatenate jump-list 'l2' into jump-list 'l1'
*/
void luaK_concat (FuncState *fs, int *l1, int l2) {
  if (l2 == NO_JUMP) return;  /* nothing to concatenate? */
  else if (*l1 == NO_JUMP)  /* no original list? */
    *l1 = l2;  /* 'l1' points to 'l2' */
  else {//如果2个链表都存在
    int list = *l1;
    int next;
    while ((next = getjump(fs, list)) != NO_JUMP)  /* find last element */
      list = next;
    fixjump(fs, list, l2);  /* last element links to 'l2' */
  }
}


/* 返回的是这个jump语句的pc号.设置当前pc是一个跳转指令!!!!!!!!
** Create a jump instruction and return its position, so its destination
** can be fixed later (with 'fixjump'). If there are jumps to
** this position (kept in 'jpc'), link them all together so that
** 'patchlistaux' will fix all them directly to the final destination.
*/
int luaK_jump (FuncState *fs) {
  int jpc = fs->jpc;  /* save list of jumps to here */ //因为后续这个jump的目的地可能会修改. 
  int j;
  fs->jpc = NO_JUMP;  /* no more jumps to here */
  j = luaK_codeAsBx(fs, OP_JMP, 0, NO_JUMP);//让j的指令为跳转到nojump, 返回的j是pc号.表示从j开始跳到-1.
  luaK_concat(fs, &j, jpc);  /* keep them on hold */ // 这样j后面就是jpc了.
  return j;
}


/*========突然写一个return. 之后继续jump系列.  first 表示返回的第一个寄存器索引, nret表示返回多少个值.
** Code a 'return' instruction: 每一个指令需要去lopcodes.h:204去找对应实现的码.  比如这个函数对应:  OP_RETURN,  A B	return R(A), ... ,R(A+B-2)	(see note)	//// 这里面A对应first nret+1对应B
*/
void luaK_ret (FuncState *fs, int first, int nret) {
  luaK_codeABC(fs, OP_RETURN, first, nret+1, 0);
}


/*=====继续看jump系列. 这个是非常核心的函数!!!!!!!!!!!!!!!!!!!!!对于if来说这就是底层函数.   op 一种是写入test, 一种是写入 大于小于等于这种.
** Code a "conditional jump", that is, a test or comparison opcode
** followed by a jump. Return jump position.
*/
static int condjump (FuncState *fs, OpCode op, int A, int B, int C) {
  luaK_codeABC(fs, op, A, B, C); //底层逻辑是,先拿op拼一个操作.写入pc指针里面.
  return luaK_jump(fs);// 紧接着就是jump,这个jump被上一行写入的op来控制,所以这2个联合起来就叫做condjump语句了.!!!!!!!
}


/* 设置当前为一个跳转目标.
** returns current 'pc' and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int luaK_getlabel (FuncState *fs) {
  fs->lasttarget = fs->pc;
  return fs->pc;
}


/*返回jump控制语句的指令的地址.
** Returns the position of the instruction "controlling" a given
** jump (that is, its condition), or the jump itself if it is
** unconditional.
*/
static Instruction *getjumpcontrol (FuncState *fs, int pc) {
  Instruction *pi = &fs->f->code[pc];  //pi是 指令码的指针.pc行的代码是jump
  if (pc >= 1 && testTMode(GET_OPCODE(*(pi-1))))// pi-1 也就是上面的&fs->f->code[pc-1] 也就是表示上一条指令 //如果上一条指令是测试.也就是f语句.
    return pi-1;
  else
    return pi; //不是if语句,说明jump是被自己控制的.
}


/* 回填测试的指令. testset是test加强版. patch是修改,打补丁的意思, test 表示修改testset指令, reg表示要修改里面的reg部分. 也就是ra,rb的值.
** Patch destination register for a TESTSET instruction.
** If instruction in position 'node' is not a TESTSET, return 0 ("fails").
** Otherwise, if 'reg' is not 'NO_REG', set it as the destination
** register. Otherwise, change instruction to a simple 'TEST' (produces
** no register value)
*/
static int patchtestreg (FuncState *fs, int node, int reg) {
  Instruction *i = getjumpcontrol(fs, node);
  if (GET_OPCODE(*i) != OP_TESTSET)
    return 0;  /* cannot patch other instructions */ //不是测试直接返回0,不归这个函数来处理.
  if (reg != NO_REG && reg != GETARG_B(*i)) //如果给了reg参数,表示一个寄存器,那么我们就需要设置reg为i的R(A)即可.
    SETARG_A(*i, reg);
  else {
     /* no register to put value or register already has the value;
        change instruction to simple test */
    *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i)); //说明不用set了.所以i就是普通test指令. ra填入 i的b参数. 因为199行否,所以reg=rb,  rc还是继续用i的c即可. 做好指令集,再写入196行的i即可.
  }
  return 1;  //返回1表示patch成功.修改成功.
}


/*  遍历一个jump链表.吧里面的节点的ra都置空.
** Traverse a list of tests ensuring no one produces a value
*/
static void removevalues (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list))
      patchtestreg(fs, list, NO_REG);
}


/* 回填跳转地址. 如果是testset指令那么用参数vtarget和reg, 如果是test指令那么用dtarget参数.
** Traverse a list of tests, patching their destination address and
** registers: tests producing values jump to 'vtarget' (and put their
** values in 'reg'), other tests jump to 'dtarget'.
*/
static void patchlistaux (FuncState *fs, int list, int vtarget, int reg,
                          int dtarget) {
  while (list != NO_JUMP) {
    int next = getjump(fs, list);
    if (patchtestreg(fs, list, reg))
      fixjump(fs, list, vtarget);
    else
      fixjump(fs, list, dtarget);  /* jump to default target */// 把链表里面每一个元素的next都设置为dtarget即可!!!!!!!!!
    list = next;
  }
}


/* 就是testset全退化成test,  discharge释放的意思. 释放掉jpc.因为jpc已经设置好跳到pc了.所以jpc释放掉里面的值.
** Ensure all pending jumps to current position are fixed (jumping
** to current position with no values) and reset list of pending
** jumps
*/
static void dischargejpc (FuncState *fs) {
  patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);//设置代码跳转的.
  fs->jpc = NO_JUMP;
}


/*  list中的变量,都让他添加到跳转到pc位置的空悬列表中.
** Add elements in 'list' to list of pending jumps to "here"
** (current position)
*/
void luaK_patchtohere (FuncState *fs, int list) {
  luaK_getlabel(fs);  /* mark "here" as a jump target */
  luaK_concat(fs, &fs->jpc, list);
}


/* list里面的全部jump都让他目标是target
** Path all jumps in 'list' to jump to 'target'.
** (The assert means that we cannot fix a jump to a forward address
** because we only know addresses once code is generated.)
*/
void luaK_patchlist (FuncState *fs, int list, int target) {
  if (target == fs->pc)  /* 'target' is current position? */
    luaK_patchtohere(fs, list);  /* add list to pending jumps */
  else {
    lua_assert(target < fs->pc);
    patchlistaux(fs, list, target, NO_REG, target);
  }
}


/*
** Path all jumps in 'list' to close upvalues up to given 'level'
** (The assertion checks that jumps either were closing nothing
** or were closing higher levels, from inner blocks.)
*/
void luaK_patchclose (FuncState *fs, int list, int level) {
  level++;  /* argument is +1 to reserve 0 as non-op */
  for (; list != NO_JUMP; list = getjump(fs, list)) {
    lua_assert(GET_OPCODE(fs->f->code[list]) == OP_JMP &&
                (GETARG_A(fs->f->code[list]) == 0 ||
                 GETARG_A(fs->f->code[list]) >= level));
    SETARG_A(fs->f->code[list], level); //每一个跳转指令的ra设置为level  ,  	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1). 设置jump的level,这样之后就可以关闭大于这个level的upvalue了.
  }
}


/* 指令i发送给pc, 然后pc返回!!!!.再++ 注意返回的是pc,不是opt码
** Emit instruction 'i', checking for array sizes and saving also its
** line information. Return 'i' position.
*/
static int luaK_code (FuncState *fs, Instruction i) {
  Proto *f = fs->f;
  dischargejpc(fs);  /* 'pc' will change */
  /* put new instruction in code array */
  luaM_growvector(fs->ls->L, f->code, fs->pc, f->sizecode, Instruction,
                  MAX_INT, "opcodes"); //拓展内存.让他足够放这个i指令. 扩容sizecode  fs.pc是当前代码位置.
  f->code[fs->pc] = i;
  /* save corresponding line information */
  luaM_growvector(fs->ls->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                  MAX_INT, "opcodes");
  f->lineinfo[fs->pc] = fs->ls->lastline;
  return fs->pc++; //opt指针继续走, 表示这个opt吗已经写完.下次写入新的opt吗.
}


/*
** Format and emit an 'iABC' instruction. (Assertions check consistency
** of parameters versus opcode.)
*/
int luaK_codeABC (FuncState *fs, OpCode o, int a, int b, int c) {
  lua_assert(getOpMode(o) == iABC);
  lua_assert(getBMode(o) != OpArgN || b == 0);
  lua_assert(getCMode(o) != OpArgN || c == 0);
  lua_assert(a <= MAXARG_A && b <= MAXARG_B && c <= MAXARG_C);
  return luaK_code(fs, CREATE_ABC(o, a, b, c));
}


/*
** Format and emit an 'iABx' instruction.
*/
int luaK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc) {
  lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
  lua_assert(getCMode(o) == OpArgN);
  lua_assert(a <= MAXARG_A && bc <= MAXARG_Bx);
  return luaK_code(fs, CREATE_ABx(o, a, bc));// CREATE_ABx 通过宏函数把数据拼接成指令instruction.
}


/*
** Emit an "extra argument" instruction (format 'iAx')
*/
static int codeextraarg (FuncState *fs, int a) {
  lua_assert(a <= MAXARG_Ax);
  return luaK_code(fs, CREATE_Ax(OP_EXTRAARG, a));
}


/*  lua_code是执行一个instruction指令的代码实现. 这个luaK_codek 执行一个加载常数的指令. k放入reg里面.
** Emit a "load constant" instruction, using either 'OP_LOADK'
** (if constant index 'k' fits in 18 bits) or an 'OP_LOADKX'
** instruction with "extra argument".
*/
int luaK_codek (FuncState *fs, int reg, int k) {
  if (k <= MAXARG_Bx)
    return luaK_codeABx(fs, OP_LOADK, reg, k);//bx表示后面2个部分都看做b. 也就是bc看做b就行了.
  else {
    int p = luaK_codeABx(fs, OP_LOADKX, reg, 0);
    codeextraarg(fs, k);
    return p;
  }
}


/* 寄存器大小加n
** Check register-stack level, keeping track of its maximum size
** in field 'maxstacksize'  //拓展栈的大小.
*/
void luaK_checkstack (FuncState *fs, int n) {
  int newstack = fs->freereg + n;
  if (newstack > fs->f->maxstacksize) {
    if (newstack >= MAXREGS)
      luaX_syntaxerror(fs->ls,
        "function or expression needs too many registers");
    fs->f->maxstacksize = cast_byte(newstack);
  }
}


/*
** Reserve 'n' registers in register stack 存储n个寄存器在寄存器栈里面.其实就是栈free指针+=n/ 就是拓展一个位置给free指针. 这样这个函数之后的函数就可以往free-n开始放东西了!!!!!!!!!!!!!原理还是跟栈的top指针一样. top始终指向的是一个空的,不被是用的东西.
*/
void luaK_reserveregs (FuncState *fs, int n) {
  luaK_checkstack(fs, n);
  fs->freereg += n;
}


/*
** Free register 'reg', if it is neither a constant index nor
** a local variable.  // 如果reg位置的不是一个常数也不是变量的话就析构掉了.
)
*/
static void freereg (FuncState *fs, int reg) {
  if (!ISK(reg) && reg >= fs->nactvar) {
    fs->freereg--;
    lua_assert(reg == fs->freereg);
  }
}


/*
** Free register used by expression 'e' (if any)
*/
static void freeexp (FuncState *fs, expdesc *e) {
  if (e->k == VNONRELOC)//如果保存在寄存器中了,就西狗叼寄存器.
    freereg(fs, e->u.info);
}


/*
** Free registers used by expressions 'e1' and 'e2' (if any) in proper
** order.
*/
static void freeexps (FuncState *fs, expdesc *e1, expdesc *e2) {
  int r1 = (e1->k == VNONRELOC) ? e1->u.info : -1;
  int r2 = (e2->k == VNONRELOC) ? e2->u.info : -1;
  if (r1 > r2) {
    freereg(fs, r1);
    freereg(fs, r2);
  }
  else {
    freereg(fs, r2);
    freereg(fs, r1);
  }
}


/* 吧常数v作为value,key作为key放到fs->ls->h这个哈希表里面.  addk,这个k表示const, 返回值是他在常数表里面的索引.
** Add constant 'v' to prototype's list of constants (field 'k').
** Use scanner's table to cache position of constants in constant list
** and try to reuse constants. Because some values should not be used
** as keys (nil cannot be a key, integer keys can collapse with float
** keys), the caller must provide a useful 'key' for indexing the cache.
*/  //add constant 所以叫addk
static int addk (FuncState *fs, TValue *key, TValue *v) {
  lua_State *L = fs->ls->L;
  Proto *f = fs->f;
  TValue *idx = luaH_set(L, fs->ls->h, key);  /* index scanner table */
  int k, oldsize;
  if (ttisinteger(idx)) {  /* is there an index there? */
    k = cast_int(ivalue(idx));
    /* correct value? (warning: must distinguish floats from integers!) */
    if (k < fs->nk && ttype(&f->k[k]) == ttype(v) &&
                      luaV_rawequalobj(&f->k[k], v))
      return k;  /* reuse index */
  }
  /* constant not found; create a new entry */
  oldsize = f->sizek;  //有多少个常数
  k = fs->nk;    // 有多少常数. 在2个地方都更新.
  /* numerical value does not need GC barrier;
     table has no metatable, so it does not need to invalidate cache */
  setivalue(idx, k);// 放入431行的缓存表里面.
  luaM_growvector(L, f->k, k, f->sizek, TValue, MAXARG_Ax, "constants");//常数数组进行拓展.
  while (oldsize < f->sizek) setnilvalue(&f->k[oldsize++]);
  setobj(L, &f->k[k], v);//把v加入常数数组里面.
  fs->nk++;  //fs也更新.
  luaC_barrier(L, f, v); //因为v放到了&f->k数组里面.所以我们这个数组要写上
  return k;
}


/* 把字符串放到字符串常量表里面.然后返回索引.
** Add a string to list of constants and return its index.
*/
int luaK_stringK (FuncState *fs, TString *s) {
  TValue o;
  setsvalue(fs->ls->L, &o, s); //s放入o里面.
  return addk(fs, &o, &o);  /* use string itself as key */
}


/*
** Add an integer to list of constants and return its index.
** Integers use userdata as keys to avoid collision with floats with
** same value; conversion to 'void*' is used only for hashing, so there
** are no "precision" problems. //把一个整数加入常数书里面然后返回这个整数在数组里面的索引.
*/
int luaK_intK (FuncState *fs, lua_Integer n) {
  TValue k, o;
  setpvalue(&k, cast(void*, cast(size_t, n)));
  setivalue(&o, n);  
  return addk(fs, &k, &o);
}

/* 添加常数r到常数表里面然后返回他的索引.
** Add a float to list of constants and return its index.
*/
static int luaK_numberK (FuncState *fs, lua_Number r) {
  TValue o;
  setfltvalue(&o, r);
  return addk(fs, &o, &o);  /* use number itself as key */
}


/* 添加布尔到常数表.返回索引
** Add a boolean to list of constants and return its index.
*/
static int boolK (FuncState *fs, int b) {
  TValue o;
  setbvalue(&o, b);
  return addk(fs, &o, &o);  /* use boolean itself as key */
}


/*
** Add nil to list of constants and return its index.
*/
static int nilK (FuncState *fs) {
  TValue k, v;
  setnilvalue(&v);
  /* cannot use nil as key; instead use table itself to represent nil */
  sethvalue(fs->ls->L, &k, fs->ls->h); //把哈希表设置为字典,记做k
  return addk(fs, &k, &v); //然后把k当做key  v设置为nil 加入常数表.
}


/*
** Fix an expression to return the number of results 'nresults'.
** Either 'e' is a multi-ret expression (function call or vararg)
** or 'nresults' is LUA_MULTRET (as any expression can satisfy that).
*/
void luaK_setreturns (FuncState *fs, expdesc *e, int nresults) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    SETARG_C(getinstruction(fs, e), nresults + 1);//因为vcall的话他的info是pc, 等于把i里面结果设置为nresults+1. 为什么是要加1,参考readme.md最后的附录.为什么这里面不用设置arga?????????????因为call指令设置好a了.
  }
  else if (e->k == VVARARG) {//如果是变参. 那么a是起始返回值.
    Instruction *pc = &getinstruction(fs, e);
    SETARG_B(*pc, nresults + 1);
    SETARG_A(*pc, fs->freereg);
    luaK_reserveregs(fs, 1);//因为上面设置了一个.所以free指针+=1 , 这个变参里面a没设置.需要
  }
  else lua_assert(nresults == LUA_MULTRET);
}


/*
** Fix an expression to return one result.
** If expression is not a multi-ret expression (function call or
** vararg), it already returns one result, so nothing needs to be done.
** Function calls become VNONRELOC expressions (as its result comes
** fixed in the base register of the call), while vararg expressions
** become VRELOCABLE (as OP_VARARG puts its results where it wants).
** (Calls are created returning one result, so that does not need
** to be fixed.)
*/
void luaK_setoneret (FuncState *fs, expdesc *e) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    /* already returns 1 value */
    lua_assert(GETARG_C(getinstruction(fs, e)) == 2);
    e->k = VNONRELOC;  /* result has fixed position */
    e->u.info = GETARG_A(getinstruction(fs, e));
  }
  else if (e->k == VVARARG) {
    SETARG_B(getinstruction(fs, e), 2);
    e->k = VRELOCABLE;  /* can relocate its simple result */
  }
}


/* 也就是把表达式里面的放到 寄存器里面.   discharge就是释放的意思, 其实本质就是把表达式的结果放到寄存器或者常量表里面.这样进行编码之后.opt吗才能利用这些量.
** Ensure that expression 'e' is not a variable.    让e不在是一个变量.
*/
void luaK_dischargevars (FuncState *fs, expdesc *e) {
  switch (e->k) {
    case VLOCAL: {  /* already in a register */ //说明这个变量已经声明过了.
      e->k = VNONRELOC;  /* becomes a non-relocatable value */
      break;
    }
    case VUPVAL: {  /* move value to some (pending) register */
      e->u.info = luaK_codeABC(fs, OP_GETUPVAL, 0, e->u.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    case VINDEXED: {//缓存常量都进入这个.
      OpCode op;
      freereg(fs, e->u.ind.idx);
      if (e->u.ind.vt == VLOCAL) {  /* is 't' in a register? */
        freereg(fs, e->u.ind.t);
        op = OP_GETTABLE;
      }
      else {
        lua_assert(e->u.ind.vt == VUPVAL);
        op = OP_GETTABUP;  /* 't' is in an upvalue */
      }
      e->u.info = luaK_codeABC(fs, op, 0, e->u.ind.t, e->u.ind.idx);//生成opt码写入e里面.
      e->k = VRELOCABLE;
      break;
    }
    case VVARARG: case VCALL: {
      luaK_setoneret(fs, e);
      break;
    }
    default: break;  /* there is one value available (somewhere) */
  }
}


/*
** Ensures expression value is in register 'reg' (and therefore
** 'e' will become a non-relocatable expression).
*/
static void discharge2reg (FuncState *fs, expdesc *e, int reg) {
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: {
      luaK_nil(fs, reg, 1);//把reg设置为nil
      break;
    }
    case VFALSE: case VTRUE: {
      luaK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
      break;
    }
    case VK: {
      luaK_codek(fs, reg, e->u.info);
      break;
    }
    case VKFLT: {
      luaK_codek(fs, reg, luaK_numberK(fs, e->u.nval));
      break;
    }
    case VKINT: {
      luaK_codek(fs, reg, luaK_intK(fs, e->u.ival));
      break;
    }
    case VRELOCABLE: {
      Instruction *pc = &getinstruction(fs, e);
      SETARG_A(*pc, reg);  /* instruction will put result in 'reg' */
      break;
    }
    case VNONRELOC: {
      if (reg != e->u.info)
        luaK_codeABC(fs, OP_MOVE, reg, e->u.info, 0);
      break;
    }
    default: {
      lua_assert(e->k == VJMP);
      return;  /* nothing to do... */
    }
  }
  e->u.info = reg;
  e->k = VNONRELOC;
}


/*把表达式的值放到寄存器上. 为了opt吗用.
** Ensures expression value is in any register.
*/
static void discharge2anyreg (FuncState *fs, expdesc *e) {
  if (e->k != VNONRELOC) {  /* no fixed register yet? */
    luaK_reserveregs(fs, 1);  /* get a register */// 这2行就是找一个寄存器,吧结果放进去而已.
    discharge2reg(fs, e, fs->freereg-1);  /* put value there */
  }
}


static int code_loadbool (FuncState *fs, int A, int b, int jump) {
  luaK_getlabel(fs);  /* those instructions may be jump targets */
  return luaK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}


/*
** check whether list has any jump that do not produce a value
** or produce an inverted value
*/
static int need_value (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list)) {
    Instruction i = *getjumpcontrol(fs, list);
    if (GET_OPCODE(i) != OP_TESTSET) return 1;
  }
  return 0;  /* not found */
}


/* 把表达式结果放到reg这个寄存器里面.
** Ensures final expression result (including results from its jump
** lists) is in register 'reg'.
** If expression has jumps, need to patch these jumps either to
** its final position or to "load" instructions (for those tests
** that do not produce values).
*/
static void exp2reg (FuncState *fs, expdesc *e, int reg) {
  discharge2reg(fs, e, reg);
  if (e->k == VJMP)  /* expression itself is a test? */
    luaK_concat(fs, &e->t, e->u.info);  /* put this jump in 't' list */
  if (hasjumps(e)) {
    int final;  /* position after whole expression */
    int p_f = NO_JUMP;  /* position of an eventual LOAD false */
    int p_t = NO_JUMP;  /* position of an eventual LOAD true */
    if (need_value(fs, e->t) || need_value(fs, e->f)) {
      int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);
      p_f = code_loadbool(fs, reg, 0, 1);
      p_t = code_loadbool(fs, reg, 1, 0);
      luaK_patchtohere(fs, fj);
    }
    final = luaK_getlabel(fs);
    patchlistaux(fs, e->f, final, reg, p_f);
    patchlistaux(fs, e->t, final, reg, p_t);
  }
  e->f = e->t = NO_JUMP;
  e->u.info = reg;
  e->k = VNONRELOC;
}


/* 核心函数,表达式e放到nextreg里面.
** Ensures final expression result (including results from its jump
** lists) is in next available register.
*/
void luaK_exp2nextreg (FuncState *fs, expdesc *e) {
  luaK_dischargevars(fs, e);
  freeexp(fs, e);
  luaK_reserveregs(fs, 1);
  exp2reg(fs, e, fs->freereg - 1);//表达式放入reg里面.
}


/*
** Ensures final expression result (including results from its jump
** lists) is in some (any) register and return that register.
*/
int luaK_exp2anyreg (FuncState *fs, expdesc *e) {
  luaK_dischargevars(fs, e);
  if (e->k == VNONRELOC) {  /* expression already has a register? */
    if (!hasjumps(e))  /* no jumps? */
      return e->u.info;  /* result is already in a register */
    if (e->u.info >= fs->nactvar) {  /* reg. is not a local? */
      exp2reg(fs, e, e->u.info);  /* put final result in it */
      return e->u.info;
    }
  }
  luaK_exp2nextreg(fs, e);  /* otherwise, use next available register */
  return e->u.info;
}


/*
** Ensures final expression result is either in a register or in an
** upvalue.
*/
void luaK_exp2anyregup (FuncState *fs, expdesc *e) {
  if (e->k != VUPVAL || hasjumps(e))
    luaK_exp2anyreg(fs, e);
}


/*
** Ensures final expression result is either in a register or it is
** a constant.
*/
void luaK_exp2val (FuncState *fs, expdesc *e) {
  if (hasjumps(e))
    luaK_exp2anyreg(fs, e);
  else
    luaK_dischargevars(fs, e);
}


/* 把表达式切换成RK模式.也就是表达式结果是一个寄存器索引或者常量索引. 然后返回这个索引值.
** Ensures final expression result is in a valid R/K index
** (that is, it is either in a register or in 'k' with an index
** in the range of R/K indices).
** Returns R/K index.
*/
int luaK_exp2RK (FuncState *fs, expdesc *e) {
  luaK_exp2val(fs, e);
  switch (e->k) {  /* move constants to 'k' */
    case VTRUE: e->u.info = boolK(fs, 1); goto vk;
    case VFALSE: e->u.info = boolK(fs, 0); goto vk;
    case VNIL: e->u.info = nilK(fs); goto vk;
    case VKINT: e->u.info = luaK_intK(fs, e->u.ival); goto vk;
    case VKFLT: e->u.info = luaK_numberK(fs, e->u.nval); goto vk;
    case VK:  //VK表示是一个常数.
     vk:
      e->k = VK;
      if (e->u.info <= MAXINDEXRK)  /* constant fits in 'argC'? */
        return RKASK(e->u.info);//如果索引能在B里面放下,那么就返回他的编码.
      else break;
    default: break;
  }
  /* not a constant in the right range: put it in a register */
  return luaK_exp2anyreg(fs, e);
}


/*
** Generate code to store result of expression 'ex' into variable 'var'.
*/
void luaK_storevar (FuncState *fs, expdesc *var, expdesc *ex) {
  switch (var->k) {
    case VLOCAL: {
      freeexp(fs, ex);
      exp2reg(fs, ex, var->u.info);  /* compute 'ex' into proper place */
      return;
    }
    case VUPVAL: {
      int e = luaK_exp2anyreg(fs, ex);
      luaK_codeABC(fs, OP_SETUPVAL, e, var->u.info, 0);
      break;
    }
    case VINDEXED: {
      OpCode op = (var->u.ind.vt == VLOCAL) ? OP_SETTABLE : OP_SETTABUP;
      int e = luaK_exp2RK(fs, ex);
      luaK_codeABC(fs, op, var->u.ind.t, var->u.ind.idx, e);
      break;
    }
    default: lua_assert(0);  /* invalid var kind to store */
  }
  freeexp(fs, ex);
}


/*
** Emit SELF instruction (convert expression 'e' into 'e:key(e,').
*/
void luaK_self (FuncState *fs, expdesc *e, expdesc *key) {
  int ereg;
  luaK_exp2anyreg(fs, e);
  ereg = e->u.info;  /* register where 'e' was placed */
  freeexp(fs, e);
  e->u.info = fs->freereg;  /* base register for op_self */
  e->k = VNONRELOC;  /* self expression has a fixed register */
  luaK_reserveregs(fs, 2);  /* function and 'self' produced by op_self */
  luaK_codeABC(fs, OP_SELF, e->u.info, ereg, luaK_exp2RK(fs, key));
  freeexp(fs, key);
}


/*
** Negate condition 'e' (where 'e' is a comparison).
*/
static void negatecondition (FuncState *fs, expdesc *e) {
  Instruction *pc = getjumpcontrol(fs, e->u.info);
  lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                           GET_OPCODE(*pc) != OP_TEST);
  SETARG_A(*pc, !(GETARG_A(*pc)));
}


/* 如果e的结果是cond ,那么进行跳转.
** Emit instruction to jump if 'e' is 'cond' (that is, if 'cond'
** is true, code will jump if 'e' is true.) Return jump position.
** Optimize when 'e' is 'not' something, inverting the condition
** and removing the 'not'.
*/
static int jumponcond (FuncState *fs, expdesc *e, int cond) {
  if (e->k == VRELOCABLE) {
    Instruction ie = getinstruction(fs, e);
    if (GET_OPCODE(ie) == OP_NOT) {
      fs->pc--;  /* remove previous OP_NOT */
      return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
    }
    /* else go through */
  }
  discharge2anyreg(fs, e);//这2行很神秘, 把值放到寄存器里面,然后再析构掉寄存器..........??????????????????????????????????????
  freeexp(fs, e);  //NO_REG 就是不使用A的意思.可能是下面一行没使用寄存器.
  return condjump(fs, OP_TESTSET, NO_REG, e->u.info, cond);
}

//if逻辑的核心函数.
/* 假的时候进行跳转, 真的时候继续运行. 
** Emit code to go through if 'e' is true, jump otherwise.
*/
void luaK_goiftrue (FuncState *fs, expdesc *e) {
  int pc;  /* pc of new jump */
  luaK_dischargevars(fs, e);
  switch (e->k) {// 这个switch语句找到false应该跳转的位置.
    case VJMP: {  /* condition? */     //如果是1<2这种语句会归为jump语句.
      negatecondition(fs, e);  /* jump when it is false */// 反转条件, 因为这种语句默认都是成功跳转,而我们这里需要失败跳转.把位置加入空悬列表里面.
      pc = e->u.info;  /* save jump position */
      break;
    }
    case VK: case VKFLT: case VKINT: case VTRUE: {
      pc = NO_JUMP;  /* always true; do nothing */ //注意了,在lua语言中任何数字都是true,0 也是true. 不可能进行false跳转,所以pc设置nojump即可.
      break;
    }
    default: {
      pc = jumponcond(fs, e, 0);  /* jump when false */
      break;
    }
  }//下面2行修改f和t2个跳转链表.
  luaK_concat(fs, &e->f, pc);  /* insert new jump in false list */ // 把新的跳转位置加入e->f里面. 因为他们都是失败时候触发.
  luaK_patchtohere(fs, e->t);  /* true list jumps to here (to go through) */ //true跳转的位置设置为当前pc, 注意fs->pc!=pc. 这里面pc变量只是起一个名字而已,不表示当前位置.当前位置是fs->pc. 成功时候,跳转到当前位置. 因为当前位置是代码块里面的内容!!!!!!!!!成功时候我们要触发代码块.  把e->t 加载到 jpc上即可. 因为jpc是跳转到当前位置的指针空悬表! 如果test失败会跳转到这里!!!!!!!
  e->t = NO_JUMP;
}


/* //e是表达式结果. 错的话不跳转, 对的话进行跳转.
** Emit code to go through if 'e' is false, jump otherwise.
*/
void luaK_goiffalse (FuncState *fs, expdesc *e) {
  int pc;  /* pc of new jump */
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VJMP: {
      pc = e->u.info;  /* already jump if true */
      break;
    }
    case VNIL: case VFALSE: {
      pc = NO_JUMP;  /* always false; do nothing */
      break;
    }
    default: {
      pc = jumponcond(fs, e, 1);  /* jump if true */
      break;
    }
  }
  luaK_concat(fs, &e->t, pc);  /* insert new jump in 't' list */
  luaK_patchtohere(fs, e->f);  /* false list jumps to here (to go through) */
  e->f = NO_JUMP;
}


/*
** Code 'not e', doing constant folding.
*/
static void codenot (FuncState *fs, expdesc *e) {
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: case VFALSE: {
      e->k = VTRUE;  /* true == not nil == not false */
      break;
    }
    case VK: case VKFLT: case VKINT: case VTRUE: {
      e->k = VFALSE;  /* false == not "x" == not 0.5 == not 1 == not true */
      break;
    }
    case VJMP: {
      negatecondition(fs, e);
      break;
    }
    case VRELOCABLE:
    case VNONRELOC: {
      discharge2anyreg(fs, e);
      freeexp(fs, e);
      e->u.info = luaK_codeABC(fs, OP_NOT, 0, e->u.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    default: lua_assert(0);  /* cannot happen */
  }
  /* interchange true and false lists */
  { int temp = e->f; e->f = e->t; e->t = temp; }
  removevalues(fs, e->f);  /* values are useless when negated */
  removevalues(fs, e->t);
}


/*
** Create expression 't[k]'. 't' must have its final result already in a
** register or upvalue.
*/
void luaK_indexed (FuncState *fs, expdesc *t, expdesc *k) {
  lua_assert(!hasjumps(t) && (vkisinreg(t->k) || t->k == VUPVAL));
  t->u.ind.t = t->u.info;  /* register or upvalue index */
  t->u.ind.idx = luaK_exp2RK(fs, k);  /* R/K index for key */
  t->u.ind.vt = (t->k == VUPVAL) ? VUPVAL : VLOCAL; //记录他是否是upval还是local value
  t->k = VINDEXED;
}


/*
** Return false if folding can raise an error.
** Bitwise operations need operands convertible to integers; division
** operations cannot have 0 as divisor.
*/
static int validop (int op, TValue *v1, TValue *v2) {
  switch (op) {
    case LUA_OPBAND: case LUA_OPBOR: case LUA_OPBXOR:
    case LUA_OPSHL: case LUA_OPSHR: case LUA_OPBNOT: {  /* conversion errors */
      lua_Integer i;
      return (tointeger(v1, &i) && tointeger(v2, &i));
    }
    case LUA_OPDIV: case LUA_OPIDIV: case LUA_OPMOD:  /* division by 0 */
      return (nvalue(v2) != 0);
    default: return 1;  /* everything else is valid */
  }
}


/* // 常数合并运算. 直接算完,然后把结果放到e1里面.
** Try to "constant-fold" an operation; return 1 iff successful.
** (In this case, 'e1' has the final result.)
*/
static int constfolding (FuncState *fs, int op, expdesc *e1,
                                                const expdesc *e2) {
  TValue v1, v2, res;
  if (!tonumeral(e1, &v1) || !tonumeral(e2, &v2) || !validop(op, &v1, &v2))
    return 0;  /* non-numeric operands or not safe to fold */
  luaO_arith(fs->ls->L, op, &v1, &v2, &res);  /* does operation */
  if (ttisinteger(&res)) {
    e1->k = VKINT;
    e1->u.ival = ivalue(&res);
  }
  else {  /* folds neither NaN nor 0.0 (to avoid problems with -0.0) */
    lua_Number n = fltvalue(&res);
    if (luai_numisnan(n) || n == 0)
      return 0;
    e1->k = VKFLT;
    e1->u.nval = n;
  }
  return 1;
}


/*
** Emit code for unary expressions that "produce values"
** (everything but 'not').
** Expression to produce final result will be encoded in 'e'.
*/
static void codeunexpval (FuncState *fs, OpCode op, expdesc *e, int line) {
  int r = luaK_exp2anyreg(fs, e);  /* opcodes operate only on registers */
  freeexp(fs, e);
  e->u.info = luaK_codeABC(fs, op, 0, r, 0);  /* generate opcode */
  e->k = VRELOCABLE;  /* all those operations are relocatable */
  luaK_fixline(fs, line);
}


/*
** Emit code for binary expressions that "produce values"
** (everything but logical operators 'and'/'or' and comparison
** operators).
** Expression to produce final result will be encoded in 'e1'.
** Because 'luaK_exp2RK' can free registers, its calls must be
** in "stack order" (that is, first on 'e2', which may have more
** recent registers to be released).
*/
static void codebinexpval (FuncState *fs, OpCode op,
                           expdesc *e1, expdesc *e2, int line) {
  int rk2 = luaK_exp2RK(fs, e2);  /* both operands are "RK" */
  int rk1 = luaK_exp2RK(fs, e1);
  freeexps(fs, e1, e2);
  e1->u.info = luaK_codeABC(fs, op, 0, rk1, rk2);  /* generate opcode */
  e1->k = VRELOCABLE;  /* all those operations are relocatable */
  luaK_fixline(fs, line);
}


/*
** Emit code for comparisons.
** 'e1' was already put in R/K form by 'luaK_infix'.
*/
static void codecomp (FuncState *fs, BinOpr opr, expdesc *e1, expdesc *e2) {
  int rk1 = (e1->k == VK) ? RKASK(e1->u.info)
                          : check_exp(e1->k == VNONRELOC, e1->u.info);
  int rk2 = luaK_exp2RK(fs, e2);
  freeexps(fs, e1, e2);
  switch (opr) {
    case OPR_NE: {  /* '(a ~= b)' ==> 'not (a == b)' */
      e1->u.info = condjump(fs, OP_EQ, 0, rk1, rk2);
      break;
    }
    case OPR_GT: case OPR_GE: {
      /* '(a > b)' ==> '(b < a)';  '(a >= b)' ==> '(b <= a)' */
      OpCode op = cast(OpCode, (opr - OPR_NE) + OP_EQ); //把这行带入opr=gt 那么结果是lt, 带入opr=ge那么op结果是le. 说白了就是符号反转, 就是1048注释内容.然后下面一行逆转操作数即可.
      e1->u.info = condjump(fs, op, 1, rk2, rk1);  /* invert operands */
      break;
    }
    default: {  /* '==', '<', '<=' use their own opcodes */
      OpCode op = cast(OpCode, (opr - OPR_EQ) + OP_EQ);
      e1->u.info = condjump(fs, op, 1, rk1, rk2);
      break;
    }
  }
  e1->k = VJMP;
}


/*
** Aplly prefix operation 'op' to expression 'e'.
*/
void luaK_prefix (FuncState *fs, UnOpr op, expdesc *e, int line) {
  static const expdesc ef = {VKINT, {0}, NO_JUMP, NO_JUMP};
  switch (op) {
    case OPR_MINUS: case OPR_BNOT:  /* use 'ef' as fake 2nd operand */
      if (constfolding(fs, op + LUA_OPUNM, e, &ef))
        break;
      /* FALLTHROUGH */
    case OPR_LEN:
      codeunexpval(fs, cast(OpCode, op + OP_UNM), e, line);
      break;
    case OPR_NOT: codenot(fs, e); break;
    default: lua_assert(0);
  }
}


/*
** Process 1st operand 'v' of binary operation 'op' before reading
** 2nd operand.  ///运行v的操作和第一个操作数.
*/
void luaK_infix (FuncState *fs, BinOpr op, expdesc *v) {
  switch (op) {
    case OPR_AND: {
      luaK_goiftrue(fs, v);  /* go ahead only if 'v' is true */
      break;
    }
    case OPR_OR: {
      luaK_goiffalse(fs, v);  /* go ahead only if 'v' is false */
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2nextreg(fs, v);  /* operand must be on the 'stack' */
      break;
    }
    case OPR_ADD: case OPR_SUB:
    case OPR_MUL: case OPR_DIV: case OPR_IDIV:
    case OPR_MOD: case OPR_POW:
    case OPR_BAND: case OPR_BOR: case OPR_BXOR:
    case OPR_SHL: case OPR_SHR: {
      if (!tonumeral(v, NULL))
        luaK_exp2RK(fs, v);
      /* else keep numeral, which may be folded with 2nd operand */
      break;
    }
    default: {
      luaK_exp2RK(fs, v);
      break;
    }
  }
}


/*
** Finalize code for binary operation, after reading 2nd operand.
** For '(a .. b .. c)' (which is '(a .. (b .. c))', because
** concatenation is right associative), merge second CONCAT into first
** one.
*/
void luaK_posfix (FuncState *fs, BinOpr op,
                  expdesc *e1, expdesc *e2, int line) {
  switch (op) {
    case OPR_AND: {
      lua_assert(e1->t == NO_JUMP);  /* list closed by 'luK_infix' */
      luaK_dischargevars(fs, e2);
      luaK_concat(fs, &e2->f, e1->f);
      *e1 = *e2;
      break;
    }
    case OPR_OR: {
      lua_assert(e1->f == NO_JUMP);  /* list closed by 'luK_infix' */
      luaK_dischargevars(fs, e2);
      luaK_concat(fs, &e2->t, e1->t);
      *e1 = *e2;
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2val(fs, e2);
      if (e2->k == VRELOCABLE &&
          GET_OPCODE(getinstruction(fs, e2)) == OP_CONCAT) {
        lua_assert(e1->u.info == GETARG_B(getinstruction(fs, e2))-1);
        freeexp(fs, e1);
        SETARG_B(getinstruction(fs, e2), e1->u.info);
        e1->k = VRELOCABLE; e1->u.info = e2->u.info;
      }
      else {
        luaK_exp2nextreg(fs, e2);  /* operand must be on the 'stack' */
        codebinexpval(fs, OP_CONCAT, e1, e2, line);
      }
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_IDIV: case OPR_MOD: case OPR_POW:
    case OPR_BAND: case OPR_BOR: case OPR_BXOR:
    case OPR_SHL: case OPR_SHR: {
      if (!constfolding(fs, op + LUA_OPADD, e1, e2))
        codebinexpval(fs, cast(OpCode, op + OP_ADD), e1, e2, line);
      break;
    }
    case OPR_EQ: case OPR_LT: case OPR_LE:
    case OPR_NE: case OPR_GT: case OPR_GE: {
      codecomp(fs, op, e1, e2);
      break;
    }
    default: lua_assert(0);
  }
}


/*
** Change line information associated with current position.
*/
void luaK_fixline (FuncState *fs, int line) {
  fs->f->lineinfo[fs->pc - 1] = line;
}


/*
** Emit a SETLIST instruction.
** 'base' is register that keeps table;
** 'nelems' is #table plus those to be stored now;
** 'tostore' is number of values (in registers 'base + 1',...) to add to
** table (or LUA_MULTRET to add up to stack top).
*/
void luaK_setlist (FuncState *fs, int base, int nelems, int tostore) {
  int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
  int b = (tostore == LUA_MULTRET) ? 0 : tostore;
  lua_assert(tostore != 0 && tostore <= LFIELDS_PER_FLUSH);
  if (c <= MAXARG_C)
    luaK_codeABC(fs, OP_SETLIST, base, b, c);
  else if (c <= MAXARG_Ax) {
    luaK_codeABC(fs, OP_SETLIST, base, b, 0);
    codeextraarg(fs, c);
  }
  else
    luaX_syntaxerror(fs->ls, "constructor too long");
  fs->freereg = base + 1;  /* free registers with list values */
}

