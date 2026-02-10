#ifndef ALCC_COMPAT_H
#define ALCC_COMPAT_H

#ifdef LUA_53
  // Lua 5.3 compatibility
  #define s2v(o) (o)
  #define setclLvalue2s(L, o, cl) setclLvalue(L, o, cl)
  #define ALCC_TOP(L) (L->top)

  // Missing 5.4+ opcodes (dummies)
  // Must be unique negative values to avoid duplicate case errors in switch
  #define OP_TBC -1
  #define OP_LOADI -2
  #define OP_LOADF -3
  #define OP_GETI -4
  #define OP_SETI -5
  #define OP_EQK -6
  #define OP_EQI -7
  #define OP_LTI -8
  #define OP_LEI -9
  #define OP_GTI -10
  #define OP_GEI -11
  #define OP_ADDI -12
  #define OP_GETFIELD -13
  #define OP_SETFIELD -14

  #define OFFSET_sC 0

  // Structure differences
  #define isvararg(p) ((p)->is_vararg)
  #define ALCC_SET_VARARG(p, v) ((p)->is_vararg = (lu_byte)(v))

  #define ALCC_UPVAL_KIND_GET(u) 0
  #define ALCC_UPVAL_KIND_SET(u, k) ((void)0)

  #define setbtvalue(obj) setbvalue(obj, 1)
  #define setbfvalue(obj) setbvalue(obj, 0)

  #define ttistrue(o) (bvalue(o))

  // Jump offset
  #define ALCC_GET_JMP_OFFSET(i) (GETARG_sBx(i))

  // RK macros are in lopcodes.h

#else
  // Lua 5.4+ compatibility
  #define ALCC_TOP(L) (L->top.p)

  // In 5.4+, p->flag holds is_vararg (bit 1)
  #define ALCC_SET_VARARG(p, v) ((p)->flag |= (lu_byte)(v))

  #define ALCC_UPVAL_KIND_GET(u) ((u)->kind)
  #define ALCC_UPVAL_KIND_SET(u, k) ((u)->kind = (k))

  #define ALCC_GET_JMP_OFFSET(i) (GETARG_sJ(i))

  // 5.4+ does not have RK
  #define ISK(x) 0
  #define INDEXK(x) (x)
#endif

#endif
