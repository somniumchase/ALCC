#ifndef ALCC_COMPAT_H
#define ALCC_COMPAT_H

#if defined(LUA_53) || defined(LUA_52)
  // Lua 5.3 and 5.2 compatibility
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

  #ifdef LUA_52
    // Missing 5.3 opcodes in 5.2
    #define OP_IDIV -15
    #define OP_BAND -16
    #define OP_BOR -17
    #define OP_BXOR -18
    #define OP_SHL -19
    #define OP_SHR -20
    #define OP_BNOT -21

    // Lua 5.2 number handling (only doubles)
    #define ttisinteger(o) 0
    #define ivalue(o) ((lua_Integer)nvalue(o))
    #define fltvalue(o) nvalue(o)
    #define setfltvalue(obj, x) setnvalue(obj, x)
    #define setivalue(obj, x) setnvalue(obj, x)
    #define tsslen(s) (((TString*)(s))->tsv.len)

    #define ALCC_LCLOSURE_T Closure
    #define ALCC_NEW_LCLOSURE(L, n) luaF_newLclosure(L, n)
    #define ALCC_SET_CL_PROTO(cl, proto) ((cl)->l.p = (proto))
  #else
    // Lua 5.3
    #define ALCC_LCLOSURE_T LClosure
    #define ALCC_NEW_LCLOSURE(L, n) luaF_newLclosure(L, n)
    #define ALCC_SET_CL_PROTO(cl, proto) ((cl)->p = (proto))
  #endif

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
  #define ALCC_TOP(L) (L->top)

  #ifndef isvararg
    #define isvararg(p) ((p)->is_vararg)
    #define ALCC_SET_VARARG(p, v) ((p)->is_vararg = (lu_byte)(v))
  #else
    #define ALCC_SET_VARARG(p, v) \
      do { if (v) (p)->flag |= (PF_VAHID | PF_VATAB); else (p)->flag &= ~(PF_VAHID | PF_VATAB); } while(0)
  #endif

  #define ALCC_UPVAL_KIND_GET(u) ((u)->kind)
  #define ALCC_UPVAL_KIND_SET(u, k) ((u)->kind = (k))

  #define ALCC_GET_JMP_OFFSET(i) (GETARG_sJ(i))

  // 5.4+ does not have RK
  #define ISK(x) 0
  #define INDEXK(x) (x)

  #define ALCC_LCLOSURE_T LClosure
  #define ALCC_NEW_LCLOSURE(L, n) luaF_newLclosure(L, n)
  #define ALCC_SET_CL_PROTO(cl, proto) ((cl)->p = (proto))
#endif

#ifdef LUA_52
  #define ALCC_LUA_DUMP(L, w, d, s) lua_dump(L, w, d)
#else
  #define ALCC_LUA_DUMP(L, w, d, s) lua_dump(L, w, d, s)
#endif

#endif
