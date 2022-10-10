/*
** $Id: llex.h,v 1.79.1.1 2017/04/19 17:20:42 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


#define FIRST_RESERVED	257


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {// 对应具体的表是llex.c:40  为了方便我直接贴过来了"and", "break", "do", "else", "elseif","end", "false", "for", "function", "goto", "if","in", "local", "nil", "not", "or", "repeat","return", "then", "true", "until", "while",        / /* other terminal symbols *//   "//", "..", "...", "==", ">=", "<=", "~=","<<", ">>", "::", "<eof>","<number>", "<integer>", "<name>", "<string>"
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};//////////////////////////////////////////////////对应数字是: TK_FLT 290        TK_INT 291  TK_NAME292   TK_STRING 293

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))


typedef union {//一个共同体里面纯具体信息. 3种.  第一个是double, 第二个int 第三个字符串.
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  int token;     //token的类别
  SemInfo seminfo;//token的具体信息
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */
typedef struct LexState
{
  int current;          /* 解析字符指针 current character (charint) */
  int linenumber;       /* 行数计数器 input line counter */
  int lastline;         /* 最后一行 line of last token 'consumed' */
  Token t;              /* 当前Token current token */
  Token lookahead;      /* 头部Token look ahead token */
  struct FuncState *fs; /* 当前解析的函数 current function (parser) */
  struct lua_State *L;  // Lua栈
  ZIO *z;               /* io输入流 input stream */
  Mbuffer *buff;        /* buffer for tokens */
  Table *h;             /* to avoid collection/reuse strings */ //字符串缓存字典.
  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  TString *source;      /* 当前源名称 current source name */
  TString *envn;        /* 环境变量 environment variable name */
} LexState;

LUAI_FUNC void luaX_init(lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC int luaX_lookahead (LexState *ls);
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
