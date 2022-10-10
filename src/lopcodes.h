/*
** $Id: lopcodes.h,v 1.149.1.1 2017/04/19 17:20:42 roberto Exp $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "llimits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.  // 指令 是一个无符号32位整数.
  All instructions have an opcode in the first 6 bits.  所以是 6+8+9+9=32
  Instructions can have the following fields:
	'A' : 8 bits
	'B' : 9 bits
	'C' : 9 bits
	'Ax' : 26 bits ('A', 'B', and 'C' together)
	'Bx' : 18 bits ('B' and 'C' together)
	'sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
===========================================================================*/



//---------------------------------------------------------------------
// 4 种指令模式
//
// iABC
//   op A B C
// iABx
//   op A Bx
// iAsBx
//   op A sBx
// iAx
//   op Ax
//---------------------------------------------------------------------
enum OpMode {iABC, iABx, iAsBx, iAx};  /* basic instruction format */


/*
** size and position of opcode arguments.
*/

//---------------------------------------------------------------------
// 操作数所占比特位数
//---------------------------------------------------------------------
#define SIZE_C		9
#define SIZE_B		9
#define SIZE_Bx		(SIZE_C + SIZE_B)
#define SIZE_A		8
#define SIZE_Ax		(SIZE_C + SIZE_B + SIZE_A)


//---------------------------------------------------------------------
// 操作码占 6 位
//---------------------------------------------------------------------
#define SIZE_OP		6


//---------------------------------------------------------------------
// 对应起始位
//---------------------------------------------------------------------
#define POS_OP 0 // SIZE_OP =6
#define POS_A (POS_OP + SIZE_OP) // POS_A=6
#define POS_C (POS_A + SIZE_A)   // POS_C=14
#define POS_B (POS_C + SIZE_C)   // POS_B=23
#define POS_Bx POS_C             // POS_Bx=14
#define POS_Ax POS_A             // POS_Ax=6   对应的图看 https://initphp.blog.csdn.net/article/details/105247775 即可.   //   最后形状就是    B C A op 这么排列的一个二进制.

/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign) 我们用带符号整数来表示参数.
*/
#if SIZE_Bx < LUAI_BITSINT-1
#define MAXARG_Bx        ((1<<SIZE_Bx)-1) //因为带符号所以最大值是再-1
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* 'sBx' is signed *///  131,071
#else
#define MAXARG_Bx        MAX_INT
#define MAXARG_sBx        MAX_INT
#endif

#if SIZE_Ax < LUAI_BITSINT-1
#define MAXARG_Ax	((1<<SIZE_Ax)-1)
#else
#define MAXARG_Ax	MAX_INT
#endif


#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)

// ((~(        a       <<(n)))<<(p))

// a= (~     (Instruction)      0) 
// a=11111111111111111111111
//   a       <<(n))=11111111000000000000  n个0
// 表示从p位置开始前面放n个1bit
/* creates a mask with 'n' 1 bits at position 'p' */
#define MASK1(n,p)	((~((~(Instruction)0)<<(n)))<<(p))

/* creates a mask with 'n' 0 bits at position 'p' */
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

//=====================================================================
// 一些操作宏函数
//=====================================================================  MASK1(SIZE_OP,0) = 00000000111111

#define GET_OPCODE(i)	(cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0))) //也就是取出最后六位.也就是optcode.
#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))  //把o的操作码放i上.

#define getarg(i,pos,size)	(cast(int, ((i)>>pos) & MASK1(size,0)))  //获取pos位开始size大小的部分的数值.
#define setarg(i,v,pos,size)	((i) = (((i)&MASK0(size,pos)) | \
                ((cast(Instruction, v)<<pos)&MASK1(size,pos))))
//这个set算法很容易, 第一个部分((i)&MASK0(size,pos)) 就是其他参数v部分其他, 第二部分就是把参数v的位置部分设置为v. 两个部分取并即可.设置了心智.
#define GETARG_A(i)	getarg(i, POS_A, SIZE_A)//这类get ,set都 一样. 就是获取i里面参数位置的值, 或者设置参数位置的值.
#define SETARG_A(i,v)	setarg(i, v, POS_A, SIZE_A)

#define GETARG_B(i)	getarg(i, POS_B, SIZE_B)
#define SETARG_B(i,v)	setarg(i, v, POS_B, SIZE_B)

#define GETARG_C(i)	getarg(i, POS_C, SIZE_C)
#define SETARG_C(i,v)	setarg(i, v, POS_C, SIZE_C)

#define GETARG_Bx(i)	getarg(i, POS_Bx, SIZE_Bx)
#define SETARG_Bx(i,v)	setarg(i, v, POS_Bx, SIZE_Bx)

#define GETARG_Ax(i)	getarg(i, POS_Ax, SIZE_Ax)
#define SETARG_Ax(i,v)	setarg(i, v, POS_Ax, SIZE_Ax)

#define GETARG_sBx(i)	(GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b)	SETARG_Bx((i),cast(unsigned int, (b)+MAXARG_sBx))


#define CREATE_ABC(o,a,b,c)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, b)<<POS_B) \
			| (cast(Instruction, c)<<POS_C))  //这个宏把 o操作符, a,b,c寄存器值. 分别输入进来,然后返回返回指令code是一个32位整数.叫指令.叫操作指令.

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, bc)<<POS_Bx))

#define CREATE_Ax(o,a)		((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_Ax))


/*
** Macros to operate RK indices
*/


#define BITRK		(1 << (SIZE_B - 1))

/* test whether value is a constant */  /* this bit 1 means constant (0 means register) */
#define ISK(x)		((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)	((int)(r) & ~BITRK)

#if !defined(MAXINDEXRK)  /* (for debugging only) */
#define MAXINDEXRK	(BITRK - 1)
#endif

/* code a constant index as a RK value */
#define RKASK(x)	((x) | BITRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG		MAXARG_A  //255表示非寄存器.


/*指令炒作的说明.
** R(x) 表示 register
** Kst(x) 表示 constant (in constant table)   kst表示const的意思.
** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x)    Rk表示可以是寄存器也可以是常数  ISK 表示is constant 是一个常数. 这行意思是如果是常数那么RK表示的是索引.否则就是寄存器. 其中B和C都是可以表示寄存器或者常数.
*/


/*
** grep "ORDER OP" if you change these enums
*/


//---------------------------------------------------------------------
// 所有操作码
//---------------------------------------------------------------------
typedef enum {// 
/*----------------------------------------------------------------------
name		args	description
------------------------------------------------------------------------*/
OP_MOVE,/*	A B	R(A) := R(B)					*/    
OP_LOADK,/*	A Bx	R(A) := Kst(Bx)					*/ 
OP_LOADKX,/*	A 	R(A) := Kst(extra arg)				*/
OP_LOADBOOL,/*	A B C	R(A) := (Bool)B; if (C) pc++			*/
OP_LOADNIL,/*	A B	R(A), R(A+1), ..., R(A+B) := nil		*/ //从A到A+B全赋值为nil
OP_GETUPVAL,/*	A B	R(A) := UpValue[B]				*/  // 把B这个upvalue放到a里面 ,b来提供索引

OP_GETTABUP,/*	A B C	R(A) := UpValue[B][RK(C)]			*/  //upvalue表中索引b,取出来一个字典,然后C作为key来得到value. 
OP_GETTABLE,/*	A B C	R(A) := R(B)[RK(C)]				*/   // 参考lua设计与实现.pdf 里面48页.的讲解. 以Rk(C)为索引 R(B)为数据表.取出来的数据geiR(A)

OP_SETTABUP,/*	A B C	UpValue[A][RK(B)] := RK(C)			*/
OP_SETUPVAL,/*	A B	UpValue[B] := R(A)				*/
OP_SETTABLE,/*	A B C	R(A)[RK(B)] := RK(C)				*/

OP_NEWTABLE,/*	A B C	R(A) := {} (size = B,C)				*/

OP_SELF,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/

OP_ADD,/*	A B C	R(A) := RK(B) + RK(C)				*/
OP_SUB,/*	A B C	R(A) := RK(B) - RK(C)				*/
OP_MUL,/*	A B C	R(A) := RK(B) * RK(C)				*/
OP_MOD,/*	A B C	R(A) := RK(B) % RK(C)				*/
OP_POW,/*	A B C	R(A) := RK(B) ^ RK(C)				*/
OP_DIV,/*	A B C	R(A) := RK(B) / RK(C)				*/
OP_IDIV,/*	A B C	R(A) := RK(B) // RK(C)				*/
OP_BAND,/*	A B C	R(A) := RK(B) & RK(C)				*/
OP_BOR,/*	A B C	R(A) := RK(B) | RK(C)				*/
OP_BXOR,/*	A B C	R(A) := RK(B) ~ RK(C)				*/
OP_SHL,/*	A B C	R(A) := RK(B) << RK(C)				*/
OP_SHR,/*	A B C	R(A) := RK(B) >> RK(C)				*/
OP_UNM,/*	A B	R(A) := -R(B)					*/
OP_BNOT,/*	A B	R(A) := ~R(B)					*/
OP_NOT,/*	A B	R(A) := not R(B)				*/
OP_LEN,/*	A B	R(A) := length of R(B)				*/

OP_CONCAT,/*	A B C	R(A) := R(B).. ... ..R(C)			*/

OP_JMP,/*	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1)	*/
OP_EQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
OP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++		*/
OP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++		*/

OP_TEST,/*	A C	if not (R(A) <=> C) then pc++			*/  //如果ra不等于C,那么pc指针加加.也就是跳过吓一跳指令.  <=>是不等号!
OP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/ //如果rb和c不相同那么进行rb=ra 否则pc加加.

OP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */   // 第一个情况当B不是0的时候 表示函数为ra, 参数为r(a+1),....r(a+b-1).一共b个参数.然后返回值有c-1个.  第二个情况b=0时候. 参数从a+1到栈顶.
OP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*///尾调用.表示函数调用然后返回这些值的操作. B-1个参数.
OP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/

OP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

OP_TFORCALL,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));	*/
OP_TFORLOOP,/*	A sBx	if R(A+1) ~= nil then { R(A)=R(A+1); pc += sBx }*/

OP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

OP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx])			*/

OP_VARARG,/*	A B	R(A), R(A+1), ..., R(A+B-2) = vararg		*/

OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*/
} OpCode;


// 操作码总个数（47 个）
#define NUM_OPCODES	(cast(int, OP_EXTRAARG) + 1)



/*===========================================================================
  Notes:
  (*) In OP_CALL, if (B == 0) then B = top. If (C == 0), then 'top' is
  set to last_result+1, so next open instruction (OP_CALL, OP_RETURN,
  OP_SETLIST) may use 'top'.

  (*) In OP_VARARG, if (B == 0) then use actual number of varargs and
  set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to 'top'.

  (*) In OP_SETLIST, if (B == 0) then B = 'top'; if (C == 0) then next
  'instruction' is EXTRAARG(real C).

  (*) In OP_LOADKX, the next 'instruction' is always EXTRAARG.

  (*) For comparisons, A specifies what condition the test should accept
  (true or false).

  (*) All 'skips' (pc++) assume that next instruction is a jump.

===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-1: op mode
** bits 2-3: C arg mode
** bits 4-5: B arg mode
** bit 6: instruction set register A
** bit 7: operator is a test (next instruction must be a jump)
*/

enum OpArgMask {
  OpArgN,  /* argument is not used */
  OpArgU,  /* argument is used */
  OpArgR,  /* argument is a register or a jump offset */
  OpArgK   /* argument is a constant or register/constant */
}; // 总共使用2个bit

// 操作码模式表, 下面的再和这些3,1<<6,1<<7做计算.得到mod
LUAI_DDEC const lu_byte luaP_opmodes[NUM_OPCODES];
//我们来具体看一下下面5个宏做什么.  也就是证明一下他的算法.  首先我们有 #define opmode(t,a,b,c,m) (((t)<<7) | ((a)<<6) | ((b)<<4) | ((c)<<2) | (m)) 这个定义.然后 缩写对应的是  T  A    B       C     mode.   比如我们要计算  luaP_opmodes[m]= opmode(0, 1, OpArgR, OpArgN, iABC)		/--* OP_MOVE --* /把这个指令里面的5个部分都拆分出来. 那么 这个码跟3做&我们得到.的就是最低的2个bit. 也就是显然是m的码. 同理cb都一样. a是得到a位置是否有值所以叫testAmode. t是最高位.计算得到也是01概念, 用来判断这个opt是不是做测试用的.
#define getOpMode(m)	(cast(enum OpMode, luaP_opmodes[m] & 3))
#define getBMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 4) & 3))
#define getCMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 2) & 3))
#define testAMode(m)	(luaP_opmodes[m] & (1 << 6))
#define testTMode(m)	(luaP_opmodes[m] & (1 << 7))


// 操作码名称表
LUAI_DDEC const char *const luaP_opnames[NUM_OPCODES+1];  /* opcode names */


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH	50


#endif
