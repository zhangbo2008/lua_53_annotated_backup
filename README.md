https://github.com/hilarryxu/lua-5.3-annotated 原始地址.
首先是代码跑通:装好gcc make之后
 apt-get install libreadline-dev 
 make linux


首先需要吧jucs05.pdf看了. 里面是官方论文.
我们跟着这个教程走.
https://blog.csdn.net/initphp/category_9293184.html

还有lua设计与实现.pdf

还有这个外国教程:
https://poga.github.io/lua53-notes/value_tvalue.html
我也保存到项目里面了.


一些关键理解点我直接写这里面.

首先学一遍lua语法:
https://www.runoob.com/lua/lua-basic-syntax.html


0. 关羽项目如何debug方法.
    首先我在src里面写了test.lua
    然后在.vscode/launch.json里面配置了运行参数.
    之后加断点运行即可.这样就进入了按照c语言来调试这个项目.就能看到代码运行的细节了.
    想debug什么lua打地面就修改test.lua即可.
1. 我们来理解 #define getstr(ts) \
    check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString)) 这份代码.
    首先添加lua代码结合lua.c里面让他编译之后直接调试lua代码. 
    启动编译之后控制台输入代码.然后断点在lua.c里面main函数即可.
    首先理解一下整个流程:
    lua.c:416行加断点. lua:314行会进入读取一行命令.
    luaS_newlstr这个代码.进入lstring:192就会发现.字符串写入的位置就是这个getstr的位置.
    原来是这样.数据就是放在他的struct结构体后面紧跟数据即可.不用定义什么struct啥的!!!!!!!这样做确实非常节省内存.是最优的方法!

2. 调试代码之前watch里面最好清空,不然会有一些访问非法导致段错误.



总流程:
    首先教程先直接粗略读一遍做一个整体把握.然后跟着教程顺序研究源码.
    上来就是lua.h 研究里面头文件. 看个大概即可.不是c文件都看个大概.知道都有什么就完事.
    lmem.c   , lmem.h
    然后lstate.c 看个大概.
    lobject.h
    lstring.c
    ldo.c   lapi.c 可以先看看字符串, table的实现.这些数据结构比较简单.
    ltable.c
    代码的核心是虚拟机.
        这部分要看lua设计与实现.pdf因为内容很多,博客讲的更不细.
        debug点在lcode.c:293即可. test.lua里面写一个最简单的local a=1然后debug.

        再参考这个https://blog.csdn.net/liutianshx2012/article/details/77394832
        先调用pmain做一些预处理.然后研究handle_script代码即可.
        parser里面需要一直参考lopcodes.h 里面的操作表.


        虚拟机是核心.
        下面再研究一份lua代码.
        
        --Lua代码
        if a > b then
            age = a
        else
            age = b
        end

        贴入test.lua里面.进行debug  f_parser断点即可.
    
    debug走完,再从项目文件.整体读lvm.c 和ldo.c. 研究源码一种是debug一种是按照文件结构读.两种结合起来.
    几大关键函数.研究透彻其他就容易多了.关键函数要把细节记住.对于其他小函数会有帮助.
    pushclosure 很重要.理解闭包在站上的结构.

    luaD_pcall==lua_pcallk 这两个是保护模式运行代码.p表示protected 是函数调用的最外层!也就是用户能接触到的函数.这2个函数的研究非常重要!!!!!!!!!!可以整体把握vm.
         注意理解栈的top概念,永远都是当前函数调用的最高有内容的位置+1.所以top永远都是空内容.
         luaD_call ---->lua_callk ---> luaD_call  这些是为了上面那2个函数的细节处理.
    luaD_precall  c闭包,c函数,lua闭包,元方法.分别处理.前2种运行函数,后2种做准备.
    luaV_execute 运行刚才的后2中函数.他们在刚才做好了准备.
    luaF_close  
        注意理解top概念. ifunc.c:85行里面 (uv = L->openupval)->v >= level 不难理解.因为level值得是oldtop.父函数的top指针.所以大于等于top指针的都删除.才能清理环境退回到父函数环境!关于upvalue和函数闭包确实还有很多细节要扣.

    再一个复杂的结构就是注册表:lstate:180
    register是一个哈希表.
        里面有2个一个是key:LUA_RIDX_MAINTHREAD value是一个哈希表
                一个是key:LUA_RIDX_GLOBALS value是一个哈希表
    ldo.c:363 moveresults也挺重要.
    checkresults很重要.理解ci.top 和L.top
    luaL_setfuncs 从这里面知道 upvalue是比函数提前入栈的.
    lapi.c lauxlib.c lbaselib.c lbitlib.c 读完
    lzio.c 读取文件 : 基本逻辑是lua_load 调用 luaZ_init 
           f_parser来调用.调用luaZ_fill
    llex.c

    lparser.c 从luaY_parser函数开始作为入口.
        首先我们看newlocalvar新建之后得到他在localVar中的索引,这个索引也写入dyd.idx里面.(这里面写的比较粗糙.不是精细的结构体.这样更容易理解.)实际存储地址是localVar中放入索引就能拿到Var对象!

        然后就是理解lexstate的结构.很重要!!!!!!!!!!!
        这个结构底层是由一堆funcstate这些个函数链接起来的.例如:
        mainfunc-->func1--->fun2-------------......
        跟c语言一样.每一个函数他内部有LocVar* Upvaldesc* 2个链表. lobject.h:551
        第一个函数是mainfunc,他里面的变量叫全局变量.每一个函数有一个prev指针指向他的上一级函数.(例如func1->prev=mainfunc)
        理解这些的核心函数是singlevar函数.这个函数逻辑是base=0表示在当前函数上层函数(可能不止一级上层)作为基准来找.base=1表示当前函数作为基准来找.具体逻辑可以看函数注释.
        newupvalue函数是把一个upvalue写入一个函数的upvalues参数里面.

        vararg是变参的意思.表示...
    
    看看opt码的编码:
        lopcodes.h:76知道 最后形状是 B C A op 这么排列的一个二进制.
        分别对应长度是9986
        所有操作码都要参考lopcodes.h:204.   和lopcodes.c 
        例子lcodes:58 例子写的很清楚.
    opt吗的运行:
        lvm.c:786行.这些参考着互相看对于理解很有帮助.
    opt吗运行. lvm:756 会租个读取opt码.
    opt吗的一个说明<=>表示的是逻辑相同, lua设计与实现书里面写错了.这个是通过lvm.c:1113分析出来的.


    学lvm的一个工具是用字节码工具:
    https://blog.csdn.net/yanjun_1982/article/details/79584630


        具体方法就是 luac -l -p src/test.lua  即可
        打印出来的就是字节码.
        看懂这个,参考luac.c文件.





    2个最重要的概念:
        enclosing function: 当前闭包函数的外层函数.因为ing表示主动.主动包住别人的就是闭包函数.
        enclosure funtion: 闭包函数. 说明这个函数外面还有一层函数.
        这些东西是函数式编程的概念可以参考:https://blog.csdn.net/qq_44611586/article/details/106292110

最重要的解析命令jump系列分析!!!!!!!!!!这个命令贯穿整个代码的流程控制语句.
    首先我们来到condjump这个函数. 他负责2个条件跳转,也是全部的条件跳转.
        一个是test
        一个是大于小于这种比较符号.
    condjump底层是luaK_jump函数.用来负责跳转.这个函数新建立的跳转全部是No_jump.
        因为跳转的目的地需要计算的.是后续需要解析的代码的一个位置.
        例如if{exp}{代码块}end. 这行代码就是test exp 再 加上jump来表达.
        真正跳转到的位置是end,需要后续解析才能算出来,填入上面.这个技术叫回填.
    luaK_concat  getjump  fixjump 用来设置jump链表的.注意里面offset要-1.因为本身pc就自己++.
    luaK_getlabel 设置当前位置为一个跳转dest
    getjumpcontrol 返回jump控制语句的指令的地址.
    patchlistaux回填的核心函数. 没啥难度就是fixjump调用而已.
    pending jumps叫空悬列表!!!!!!!!!!. 这个列表里面的东西目的地都没设置.后续patchlistaux会遍历这个表,然后把每一个的next都设置好.所以空悬列表只是临时的把目的地相同给的东西用next串起来而已.
    


gc:
    lapi:245对于barrier里面引用对象的理解.
    luaC_barrier(L, clCvalue(L->ci->func), fr);
    什么叫引用.
    我们回收时候,clCvalue(L->ci->func)这个函数里面有一个属性是upvalues
    他里面的数据有新加入的fr对象.
    我们调用245行之前,如果gc已经扫描过这个clCvalue(L->ci->func)函数
    那么他颜色已经是灰色或者黑色.并且他里面的各个属性也都染色了.
    而我们新加入一个fr数据时候.这个fr数据肯定是白色的.
    但是clCvalue(L->ci->func)函数可以通过一种引用,也就是指针->upvalues来跳转到
    这个fr数值.这就矛盾了.所以barrier做法就是把函数重新编程白色.重新染色即可!

    如果反过来,新来的东西引用旧的,那是没问题的.因为新来的会在之后的gc中被扫描到.白色里面可以propogate出黑色,这种逻辑是可以的.可以继续传导的.

    lua设计与实现.pdf:144页写了.

















## Lua源码阅读顺序

### 1. **lmathlib.c, lstrlib.c:**
>get familiar with the external C API. Don't bother with the pattern matcher though. Just the easy functions.

### 2. **lapi.c:**
>Check how the API is implemented internally. Only skim this to get a feeling for the code. Cross-reference to *lua.h* and *luaconf.h* as needed.

### 3. **lobject.h:**
>tagged values and object representation. skim through this first. you'll want to keep a window with this file open all the time.

### 4. **lstate.h:**
>state objects. ditto.

### 5. **lopcodes.h:**
>bytecode instruction format and opcode definitions. easy.

### 6. **lvm.c:**
>scroll down to luaV_execute, the main interpreter loop. see how all of the instructions are implemented. skip the details for now. reread later.

### 7. **ldo.c:**
>calls, stacks, exceptions, coroutines. tough read.

### 8. **lstring.c:**
>string interning. cute, huh?

### 9. **ltable.c:**
>hash tables and arrays. tricky code.

### 10. **ltm.c:**
>metamethod handling, reread all of *lvm.c* now.

### 11. **lapi.c**

### 12. **ldebug.c:**
>surprise waiting for you. abstract interpretation is used to find object names for tracebacks. does bytecode verification, too.

### 13. **lparser.c, lcode.c:**
>recursive descent parser, targetting a register-based VM. start from chunk() and work your way through. read the expression parser and the code generator parts last.

### 14. **lgc.c:**
>incremental garbage collector. take your time.

---
Read all the other files as you see references to them. Don't let your stack get too deep though.

"# lua5-annotated" 



附录:token 表:ls->t.token的值来这里找他对应的类型.
debug时候根据
  TK_AND = FIRST_RESERVED  257, TK_BREAK 258,
  TK_DO 259, TK_ELSE 260, TK_ELSEIF 261, TK_END 262, TK_FALSE 263, TK_FOR 264, TK_FUNCTION 265,
  TK_GOTO  266, TK_IF  267, TK_IN 268, TK_LOCAL 269   , TK_NIL 270, TK_NOT 271, TK_OR 272, TK_REPEAT 273,
  TK_RETURN 274, TK_THEN 275, TK_TRUE 276, TK_UNTIL 277, TK_WHILE 278,
  /* other terminal symbols */
  TK_IDIV 279, TK_CONCAT  280, TK_DOTS 281, TK_EQ 282, TK_GE 283, TK_LE 284, TK_NE 285,
  TK_SHL 286, TK_SHR 287,
  TK_DBCOLON 288, TK_EOS 289,
  TK_FLT 290, TK_INT 291, TK_NAME 292, TK_STRING 293










附录:
这2个操作码表态常用所以直接贴这里好找:


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
OP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++		*/  ~= 是不等号的意思!!!!!!!!!!!!
OP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++		*/

OP_TEST,/*	A C	if not (R(A) <=> C) then pc++			*/  <==>表示的是两个逻辑符号相同逻辑等!!!!!!!!!!!!!!通过lvm:1113行证明的. pc++表示进入if陈宫的代码块,
OP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/ //如果rb和c相同那么进行rb=ra 否则pc加加.

OP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */   // 第一个情况当B不是0的时候 表示函数为ra, 参数为r(a+1),....r(a+b-1).一共b个参数.然后返回值有c-1个.  第二个情况b=0时候. 参数从a+1到栈顶.
OP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*///尾调用.表示函数调用然后返回这些值的操作. B-1个参数.
OP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/ // 所以B表示返回值的数量+1

OP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

OP_TFORCALL,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));	*/
OP_TFORLOOP,/*	A sBx	if R(A+1) ~= nil then { R(A)=R(A+1); pc += sBx }*/

OP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

OP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx])			*/ 创建一个函数对象,其中函数prototype信息放入Bx中,生成的函数对象房子R(A)里面.这个指令. KPROTO 表示的是  lobject.h:549 代表的子函数列表. 

OP_VARARG,/*	A B	R(A), R(A+1), ..., R(A+B-2) = vararg		*/ //变参.

OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*/
} OpCode;







/这些是上面opt码的补充说明*===========================================================================
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


OpArgN,  - * argument is not used *   
OpArgU,  * argument is used *   
OpArgR,   -* argument is a register or a jump offset *   
OpArgK     * argument is a constant or register/constant *  .


LUAI_DDEF const lu_byte luaP_opmodes[NUM_OPCODES] = {
/*       T  A    B       C     mode		   opcode	*/
  opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_MOVE */
 ,opmode(0, 1, OpArgK, OpArgN, iABx)		/* OP_LOADK */
 ,opmode(0, 1, OpArgN, OpArgN, iABx)		/* OP_LOADKX */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_LOADBOOL */
 ,opmode(0, 1, OpArgU, OpArgN, iABC)		/* OP_LOADNIL */
 ,opmode(0, 1, OpArgU, OpArgN, iABC)		/* OP_GETUPVAL */
 ,opmode(0, 1, OpArgU, OpArgK, iABC)		/* OP_GETTABUP */
 ,opmode(0, 1, OpArgR, OpArgK, iABC)		/* OP_GETTABLE */
 ,opmode(0, 0, OpArgK, OpArgK, iABC)		/* OP_SETTABUP */
 ,opmode(0, 0, OpArgU, OpArgN, iABC)		/* OP_SETUPVAL */
 ,opmode(0, 0, OpArgK, OpArgK, iABC)		/* OP_SETTABLE */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_NEWTABLE */
 ,opmode(0, 1, OpArgR, OpArgK, iABC)		/* OP_SELF */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_ADD */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_SUB */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_MUL */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_MOD */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_POW */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_DIV */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_IDIV */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_BAND */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_BOR */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_BXOR */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_SHL */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_SHR */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_UNM */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_BNOT */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_NOT */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_LEN */
 ,opmode(0, 1, OpArgR, OpArgR, iABC)		/* OP_CONCAT */
 ,opmode(0, 0, OpArgR, OpArgN, iAsBx)		/* OP_JMP */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_EQ */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_LT */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_LE */
 ,opmode(1, 0, OpArgN, OpArgU, iABC)		/* OP_TEST */
 ,opmode(1, 1, OpArgR, OpArgU, iABC)		/* OP_TESTSET */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_CALL */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_TAILCALL */
 ,opmode(0, 0, OpArgU, OpArgN, iABC)		/* OP_RETURN */
 ,opmode(0, 1, OpArgR, OpArgN, iAsBx)		/* OP_FORLOOP */
 ,opmode(0, 1, OpArgR, OpArgN, iAsBx)		/* OP_FORPREP */
 ,opmode(0, 0, OpArgN, OpArgU, iABC)		/* OP_TFORCALL */
 ,opmode(0, 1, OpArgR, OpArgN, iAsBx)		/* OP_TFORLOOP */
 ,opmode(0, 0, OpArgU, OpArgU, iABC)		/* OP_SETLIST */
 ,opmode(0, 1, OpArgU, OpArgN, iABx)		/* OP_CLOSURE */
 ,opmode(0, 1, OpArgU, OpArgN, iABC)		/* OP_VARARG */
 ,opmode(0, 0, OpArgU, OpArgU, iAx)		/* OP_EXTRAARG */
};




opt码实战:  src/luac -l -p src/test.lua  因为我修改了一些部分方便打印所以调用自己的luac
    0+ params, 2 slots, 1 upvalue, 0 locals, 4 constants, 0 functions
            1       [1]     SETTABUP        0 -1 -2 ; _ENV "b" 1         ; 第一行b在最外层所以是环境变量.设置env["b"]=1     -1表示第一个常数索引 -2表示第二个常数索引.  第一个数字1表示pc号,第二个[1]表示代码行号.
            2       [2]     GETTABUP        0 0 -1  ; _ENV "b"  //然后是取值, 拿到env["b"]
            3       [3]     TEST            0 0      //然后进行测试. test找opt码说明, 他里面说的是用的是R(a)和C进行比较. 246行, 如果Ra!=c那么就进行跳转.test提供的条件控制语句. 这里面就是R(0)跟0进行比较.跳过jump
            4       [3]     JMP             0 3     ; to 8 //这个地方写的0,3 表示ra=0, rb=3 向前走3个单位. 同时pc指令自己默认也加加.所以总共就是从pc4的位置,跑完之后到了8位置.
            5       [4]     GETTABUP        0 0 -3  ; _ENV "print" //之后获取环境变量里面print函数指针,
            6       [4]     LOADK           1 -4    ; "a==0 means true" //往寄存器a里面放入4号常数. 3号常数是上一行的print. 
            7       [4]     CALL            0 2 1  //调用指令call  2个入参, 0个返回值. 第一个入参是print函数指针, 第二个是字符串.
            8       [5]     RETURN          0 1
    对应代码是:
    1    b=1
    2    if(b)   //这行的逻辑是.调用test指令. test里面 寄存器r(a)=1 r(b)不使用, c=0,因为if里面表达式只有b一个变量,b对应的寄存器是r(a), 如果变量b 跟c同号,我们就进行跳过代码块.这个逻辑就通了.如果亿豪说明要进入代码块!!!!!!!
    3        then 
    4        print("a==0 means true")
    5        end
    对应的这个opt码,再进行debug,知道里面变量都怎么来的能更容易理解.




下一个研究的例子是goto语句,跑一边理解jump的运行
if 1 then
    goto label   -- a 小于 3 的时候跳转到标签 label
end
::label:: print("--- goto label ---")

上来还是先生成opt吗:  src/luac -l -p src/test.lua
        1       [1]     LOADK           0 -1    ; 1
        2       [1]     TEST            0 1;  这次R(0)=1 跟0 做逻辑比较是不同,所以就进入下一行逻辑. 跳过下行jump.
        3       [1]     JMP             0 0     ; to 4;;debug一下 0, 2; 
        4       [4]     GETTABUP        0 0 -2  ; _ENV "print"
        5       [4]     LOADK           1 -3    ; "--- goto label ---"
        6       [4]     CALL            0 2 1
        7       [4]     RETURN          0 1
进行debug:
    closegoto函数打断点.



最后我们从lparser.c:1537开始读各个token的解析部分即可.

lua里面elseif全都是上一行失败了才运行判断."# lua53_backup" 
"# lua_53_annotated_backup" 
