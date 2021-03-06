/* Copyright (C) 2001, 2009, 2010, 2011, 2012, 2013,
 *   2014, 2015 Free Software Foundation, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/* This file is included in vm.c multiple times.  */


#define UNPACK_8_8_8(op,a,b,c)            \
  do                                      \
    {                                     \
      a = (op >> 8) & 0xff;               \
      b = (op >> 16) & 0xff;              \
      c = op >> 24;                       \
    }                                     \
  while (0)

#define UNPACK_8_16(op,a,b)               \
  do                                      \
    {                                     \
      a = (op >> 8) & 0xff;               \
      b = op >> 16;                       \
    }                                     \
  while (0)

#define UNPACK_16_8(op,a,b)               \
  do                                      \
    {                                     \
      a = (op >> 8) & 0xffff;             \
      b = op >> 24;                       \
    }                                     \
  while (0)

#define UNPACK_12_12(op,a,b)              \
  do                                      \
    {                                     \
      a = (op >> 8) & 0xfff;              \
      b = op >> 20;                       \
    }                                     \
  while (0)

#define UNPACK_24(op,a)                   \
  do                                      \
    {                                     \
      a = op >> 8;                        \
    }                                     \
  while (0)


/* Assign some registers by hand.  There used to be a bigger list here,
   but it was never tested, and in the case of x86-32, was a source of
   compilation failures.  It can be revived if it's useful, but my naive
   hope is that simply annotating the locals with "register" will be a
   sufficient hint to the compiler.  */
#ifdef __GNUC__
# if defined __x86_64__
/* GCC 4.6 chooses %rbp for IP_REG and %rbx for SP_REG, which works
   well.  Tell it to keep the jump table in a r12, which is
   callee-saved.  */
#  define JT_REG asm ("r12")
# endif
#endif

#ifndef IP_REG
# define IP_REG
#endif
#ifndef FP_REG
# define FP_REG
#endif
#ifndef JT_REG
# define JT_REG
#endif

#define VM_ASSERT(condition, handler)     \
  do {                                    \
    if (SCM_UNLIKELY (!(condition)))      \
      {                                   \
        SYNC_IP();                        \
        handler;                          \
      }                                   \
  } while (0)

#ifdef VM_ENABLE_ASSERTIONS
# define ASSERT(condition) VM_ASSERT (condition, abort())
#else
# define ASSERT(condition)
#endif

#if VM_USE_HOOKS
#define RUN_HOOK(exp)                                   \
  do {                                                  \
    if (SCM_UNLIKELY (vp->trace_level > 0))             \
      {                                                 \
        SYNC_IP ();                                     \
        exp;                                            \
        CACHE_FP ();                                    \
      }                                                 \
  } while (0)
#else
#define RUN_HOOK(exp)
#endif
#define RUN_HOOK0(h)      RUN_HOOK (vm_dispatch_##h##_hook (vp))
#define RUN_HOOK1(h, arg) RUN_HOOK (vm_dispatch_##h##_hook (vp, arg))

#define APPLY_HOOK()                            \
  RUN_HOOK0 (apply)
#define PUSH_CONTINUATION_HOOK()                \
  RUN_HOOK0 (push_continuation)
#define POP_CONTINUATION_HOOK(old_fp)           \
  RUN_HOOK1 (pop_continuation, old_fp)
#define NEXT_HOOK()                             \
  RUN_HOOK0 (next)
#define ABORT_CONTINUATION_HOOK()               \
  RUN_HOOK0 (abort)

#define VM_HANDLE_INTERRUPTS                     \
  SCM_ASYNC_TICK_WITH_GUARD_CODE (thread, SYNC_IP (), CACHE_FP ())


/* Virtual Machine

   The VM has three state bits: the instruction pointer (IP), the frame
   pointer (FP), and the top-of-stack pointer (SP).  We cache the first
   two of these in machine registers, local to the VM, because they are
   used extensively by the VM.  As the SP is used more by code outside
   the VM than by the VM itself, we don't bother caching it locally.

   Since the FP changes infrequently, relative to the IP, we keep vp->fp
   in sync with the local FP.  This would be a big lose for the IP,
   though, so instead of updating vp->ip all the time, we call SYNC_IP
   whenever we would need to know the IP of the top frame.  In practice,
   we need to SYNC_IP whenever we call out of the VM to a function that
   would like to walk the stack, perhaps as the result of an
   exception.

   One more thing.  We allow the stack to move, when it expands.
   Therefore if you call out to a C procedure that could call Scheme
   code, or otherwise push anything on the stack, you will need to
   CACHE_FP afterwards to restore the possibly-changed FP. */

#define SYNC_IP() vp->ip = (ip)

#define CACHE_FP() fp = (vp->fp)
#define CACHE_REGISTER()                        \
  do {                                          \
    ip = vp->ip;                                \
    fp = vp->fp;                                \
  } while (0)


/* Reserve stack space for a frame.  Will check that there is sufficient
   stack space for N locals, including the procedure.  Invoke after
   preparing the new frame and setting the fp and ip.

   If there is not enough space for this frame, we try to expand the
   stack, possibly relocating it somewhere else in the address space.
   Because of the possible relocation, no pointer into the stack besides
   FP is valid across an ALLOC_FRAME call.  Be careful!  */
#define ALLOC_FRAME(n)                                              \
  do {                                                              \
    SCM *new_sp = LOCAL_ADDRESS (n - 1);                            \
    if (new_sp > vp->sp_max_since_gc)                               \
      {                                                             \
        if (SCM_UNLIKELY (new_sp >= vp->stack_limit))               \
          {                                                         \
            SYNC_IP ();                                             \
            vm_expand_stack (vp, new_sp);                           \
            CACHE_FP ();                                            \
          }                                                         \
        else                                                        \
          vp->sp_max_since_gc = vp->sp = new_sp;                    \
      }                                                             \
    else                                                            \
      vp->sp = new_sp;                                              \
  } while (0)

/* Reset the current frame to hold N locals.  Used when we know that no
   stack expansion is needed.  */
#define RESET_FRAME(n)                                              \
  do {                                                              \
    vp->sp = LOCAL_ADDRESS (n - 1);                                 \
    if (vp->sp > vp->sp_max_since_gc)                               \
      vp->sp_max_since_gc = vp->sp;                                 \
  } while (0)

/* Compute the number of locals in the frame.  At a call, this is equal
   to the number of actual arguments when a function is first called,
   plus one for the function.  */
#define FRAME_LOCALS_COUNT_FROM(slot)           \
  (vp->sp + 1 - LOCAL_ADDRESS (slot))
#define FRAME_LOCALS_COUNT() \
  FRAME_LOCALS_COUNT_FROM (0)

/* Restore registers after returning from a frame.  */
#define RESTORE_FRAME()                                             \
  do {                                                              \
  } while (0)


#ifdef HAVE_LABELS_AS_VALUES
# define BEGIN_DISPATCH_SWITCH /* */
# define END_DISPATCH_SWITCH /* */
# define NEXT(n)                                \
  do                                            \
    {                                           \
      ip += n;                                  \
      NEXT_HOOK ();                             \
      op = *ip;                                 \
      goto *jump_table[op & 0xff];              \
    }                                           \
  while (0)
# define VM_DEFINE_OP(opcode, tag, name, meta)  \
  op_##tag:
#else
# define BEGIN_DISPATCH_SWITCH                  \
  vm_start:                                     \
    NEXT_HOOK ();                               \
    op = *ip;                                   \
  switch (op & 0xff)                            \
    {
# define END_DISPATCH_SWITCH                    \
    }
# define NEXT(n)                                \
  do                                            \
    {                                           \
      ip += n;                                  \
      goto vm_start;                            \
    }                                           \
  while (0)
# define VM_DEFINE_OP(opcode, tag, name, meta)  \
  op_##tag:                                     \
  case opcode:
#endif

#define LOCAL_ADDRESS(i)	(&SCM_FRAME_LOCAL (fp, i))
#define LOCAL_REF(i)		SCM_FRAME_LOCAL (fp, i)
#define LOCAL_SET(i,o)		SCM_FRAME_LOCAL (fp, i) = o

#define VARIABLE_REF(v)		SCM_VARIABLE_REF (v)
#define VARIABLE_SET(v,o)	SCM_VARIABLE_SET (v, o)
#define VARIABLE_BOUNDP(v)      (!scm_is_eq (VARIABLE_REF (v), SCM_UNDEFINED))

#define RETURN_ONE_VALUE(ret)                           \
  do {                                                  \
    SCM val = ret;                                      \
    SCM *old_fp;                                        \
    VM_HANDLE_INTERRUPTS;                               \
    ALLOC_FRAME (2);					\
    old_fp = fp;                                        \
    ip = SCM_FRAME_RETURN_ADDRESS (fp);                 \
    fp = vp->fp = SCM_FRAME_DYNAMIC_LINK (fp);          \
    /* Clear frame. */                                  \
    old_fp[-1] = SCM_BOOL_F;                            \
    old_fp[-2] = SCM_BOOL_F;                            \
    /* Leave proc. */                                   \
    SCM_FRAME_LOCAL (old_fp, 1) = val;                  \
    vp->sp = &SCM_FRAME_LOCAL (old_fp, 1);              \
    POP_CONTINUATION_HOOK (old_fp);                     \
    NEXT (0);                                           \
  } while (0)

/* While we could generate the list-unrolling code here, it's fine for
   now to just tail-call (apply values vals).  */
#define RETURN_VALUE_LIST(vals_)                        \
  do {                                                  \
    SCM vals = vals_;                                   \
    VM_HANDLE_INTERRUPTS;                               \
    ALLOC_FRAME (3);					\
    fp[0] = vm_builtin_apply;                           \
    fp[1] = vm_builtin_values;                          \
    fp[2] = vals;                                       \
    ip = (scm_t_uint32 *) vm_builtin_apply_code;        \
    goto op_tail_apply;                                 \
  } while (0)

#define BR_NARGS(rel)                           \
  scm_t_uint32 expected;                        \
  UNPACK_24 (op, expected);                     \
  if (FRAME_LOCALS_COUNT() rel expected)        \
    {                                           \
      scm_t_int32 offset = ip[1];               \
      offset >>= 8; /* Sign-extending shift. */ \
      NEXT (offset);                            \
    }                                           \
  NEXT (2)

#define BR_UNARY(x, exp)                        \
  scm_t_uint32 test;                            \
  SCM x;                                        \
  UNPACK_24 (op, test);                         \
  x = LOCAL_REF (test);                         \
  if ((ip[1] & 0x1) ? !(exp) : (exp))           \
    {                                           \
      scm_t_int32 offset = ip[1];               \
      offset >>= 8; /* Sign-extending shift. */ \
      if (offset <= 0)                          \
        VM_HANDLE_INTERRUPTS;                   \
      NEXT (offset);                            \
    }                                           \
  NEXT (2)

#define BR_BINARY(x, y, exp)                    \
  scm_t_uint16 a, b;                            \
  SCM x, y;                                     \
  UNPACK_12_12 (op, a, b);                      \
  x = LOCAL_REF (a);                            \
  y = LOCAL_REF (b);                            \
  if ((ip[1] & 0x1) ? !(exp) : (exp))           \
    {                                           \
      scm_t_int32 offset = ip[1];               \
      offset >>= 8; /* Sign-extending shift. */ \
      if (offset <= 0)                          \
        VM_HANDLE_INTERRUPTS;                   \
      NEXT (offset);                            \
    }                                           \
  NEXT (2)

#define BR_ARITHMETIC(crel,srel)                                        \
  {                                                                     \
    scm_t_uint16 a, b;                                                  \
    SCM x, y;                                                           \
    UNPACK_12_12 (op, a, b);                                            \
    x = LOCAL_REF (a);                                                  \
    y = LOCAL_REF (b);                                                  \
    if (SCM_I_INUMP (x) && SCM_I_INUMP (y))                             \
      {                                                                 \
        scm_t_signed_bits x_bits = SCM_UNPACK (x);                      \
        scm_t_signed_bits y_bits = SCM_UNPACK (y);                      \
        if ((ip[1] & 0x1) ? !(x_bits crel y_bits) : (x_bits crel y_bits)) \
          {                                                             \
            scm_t_int32 offset = ip[1];                                 \
            offset >>= 8; /* Sign-extending shift. */                   \
            if (offset <= 0)                                            \
              VM_HANDLE_INTERRUPTS;                                     \
            NEXT (offset);                                              \
          }                                                             \
        NEXT (2);                                                       \
      }                                                                 \
    else                                                                \
      {                                                                 \
        SCM res;                                                        \
        SYNC_IP ();                                                     \
        res = srel (x, y);                                              \
        CACHE_FP ();                                                    \
        if ((ip[1] & 0x1) ? scm_is_false (res) : scm_is_true (res))     \
          {                                                             \
            scm_t_int32 offset = ip[1];                                 \
            offset >>= 8; /* Sign-extending shift. */                   \
            if (offset <= 0)                                            \
              VM_HANDLE_INTERRUPTS;                                     \
            NEXT (offset);                                              \
          }                                                             \
        NEXT (2);                                                       \
      }                                                                 \
  }

#define ARGS1(a1)                               \
  scm_t_uint16 dst, src;                        \
  SCM a1;                                       \
  UNPACK_12_12 (op, dst, src);                  \
  a1 = LOCAL_REF (src)
#define ARGS2(a1, a2)                           \
  scm_t_uint8 dst, src1, src2;                  \
  SCM a1, a2;                                   \
  UNPACK_8_8_8 (op, dst, src1, src2);           \
  a1 = LOCAL_REF (src1);                        \
  a2 = LOCAL_REF (src2)
#define RETURN(x)                               \
  do { LOCAL_SET (dst, x); NEXT (1); } while (0)
#define RETURN_EXP(exp)                         \
  do { SCM __x; SYNC_IP (); __x = exp; CACHE_FP (); RETURN (__x); } while (0)

/* The maximum/minimum tagged integers.  */
#define INUM_MAX  \
  ((scm_t_signed_bits) SCM_UNPACK (SCM_I_MAKINUM (SCM_MOST_POSITIVE_FIXNUM)))
#define INUM_MIN  \
  ((scm_t_signed_bits) SCM_UNPACK (SCM_I_MAKINUM (SCM_MOST_NEGATIVE_FIXNUM)))
#define INUM_STEP                                \
  ((scm_t_signed_bits) SCM_UNPACK (SCM_INUM1)    \
   - (scm_t_signed_bits) SCM_UNPACK (SCM_INUM0))

#define BINARY_INTEGER_OP(CFUNC,SFUNC)                                      \
  {                                                             \
    ARGS2 (x, y);						\
    if (SCM_I_INUMP (x) && SCM_I_INUMP (y))                     \
      {                                                         \
        scm_t_int64 n = SCM_I_INUM (x) CFUNC SCM_I_INUM (y);    \
        if (SCM_FIXABLE (n))                                    \
          RETURN (SCM_I_MAKINUM (n));                           \
      }                                                         \
    RETURN_EXP (SFUNC (x, y));                                  \
  }

#define VM_VALIDATE_PAIR(x, proc)		\
  VM_ASSERT (scm_is_pair (x), vm_error_not_a_pair (proc, x))
  
#define VM_VALIDATE_STRUCT(obj, proc)           \
  VM_ASSERT (SCM_STRUCTP (obj), vm_error_not_a_pair (proc, obj))

#define VM_VALIDATE_BYTEVECTOR(x, proc)		\
  VM_ASSERT (SCM_BYTEVECTOR_P (x), vm_error_not_a_bytevector (proc, x))

/* Return true (non-zero) if PTR has suitable alignment for TYPE.  */
#define ALIGNED_P(ptr, type)			\
  ((scm_t_uintptr) (ptr) % alignof_type (type) == 0)

static SCM
VM_NAME (scm_i_thread *thread, struct scm_vm *vp,
         scm_i_jmp_buf *registers, int resume)
{
  /* Instruction pointer: A pointer to the opcode that is currently
     running.  */
  register scm_t_uint32 *ip IP_REG;

  /* Frame pointer: A pointer into the stack, off of which we index
     arguments and local variables.  Pushed at function calls, popped on
     returns.  */
  register SCM *fp FP_REG;

  /* Current opcode: A cache of *ip.  */
  register scm_t_uint32 op;

#ifdef HAVE_LABELS_AS_VALUES
  static const void *jump_table_[256] = {
#define LABEL_ADDR(opcode, tag, name, meta) &&op_##tag,
      FOR_EACH_VM_OPERATION(LABEL_ADDR)
#undef LABEL_ADDR
  };
  register const void **jump_table JT_REG;
  /* Attempt to keep JUMP_TABLE_POINTER in a register.  This saves one
     load instruction at each instruction dispatch.  */
  jump_table = jump_table_;
#endif

  /* Load VM registers. */
  CACHE_REGISTER ();

  VM_HANDLE_INTERRUPTS;

  /* Usually a call to the VM happens on application, with the boot
     continuation on the next frame.  Sometimes it happens after a
     non-local exit however; in that case the VM state is all set up,
     and we have but to jump to the next opcode.  */
  if (SCM_UNLIKELY (resume))
    NEXT (0);

 apply:
  while (!SCM_PROGRAM_P (LOCAL_REF (0)))
    {
      SCM proc = LOCAL_REF (0);

      if (SCM_STRUCTP (proc) && SCM_STRUCT_APPLICABLE_P (proc))
        {
          LOCAL_SET (0, SCM_STRUCT_PROCEDURE (proc));
          continue;
        }
      if (SCM_HAS_TYP7 (proc, scm_tc7_smob) && SCM_SMOB_APPLICABLE_P (proc))
        {
          scm_t_uint32 n = FRAME_LOCALS_COUNT();

          /* Shuffle args up. */
          RESET_FRAME (n + 1);
          while (n--)
            LOCAL_SET (n + 1, LOCAL_REF (n));

          LOCAL_SET (0, SCM_SMOB_DESCRIPTOR (proc).apply_trampoline);
          continue;
        }

      SYNC_IP();
      vm_error_wrong_type_apply (proc);
    }

  /* Let's go! */
  ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

  APPLY_HOOK ();

  NEXT (0);

  BEGIN_DISPATCH_SWITCH;
  

  

  /*
   * Call and return
   */

  /* halt _:24
   *
   * Bring the VM to a halt, returning all the values from the stack.
   */
  VM_DEFINE_OP (0, halt, "halt", OP1 (U8_X24))
    {
      /* Boot closure in r0, empty frame in r1/r2, proc in r3, values from r4.  */

      scm_t_uint32 nvals = FRAME_LOCALS_COUNT_FROM (4);
      SCM ret;

      if (nvals == 1)
        ret = LOCAL_REF (4);
      else
        {
          scm_t_uint32 n;
          ret = SCM_EOL;
          for (n = nvals; n > 0; n--)
            ret = scm_inline_cons (thread, LOCAL_REF (4 + n - 1), ret);
          ret = scm_values (ret);
        }

      vp->ip = SCM_FRAME_RETURN_ADDRESS (fp);
      vp->sp = SCM_FRAME_PREVIOUS_SP (fp);
      vp->fp = SCM_FRAME_DYNAMIC_LINK (fp);

      return ret;
    }

  /* call proc:24 _:8 nlocals:24
   *
   * Call a procedure.  PROC is the local corresponding to a procedure.
   * The two values below PROC will be overwritten by the saved call
   * frame data.  The new frame will have space for NLOCALS locals: one
   * for the procedure, and the rest for the arguments which should
   * already have been pushed on.
   *
   * When the call returns, execution proceeds with the next
   * instruction.  There may be any number of values on the return
   * stack; the precise number can be had by subtracting the address of
   * PROC from the post-call SP.
   */
  VM_DEFINE_OP (1, call, "call", OP2 (U8_U24, X8_U24))
    {
      scm_t_uint32 proc, nlocals;
      SCM *old_fp;

      UNPACK_24 (op, proc);
      UNPACK_24 (ip[1], nlocals);

      VM_HANDLE_INTERRUPTS;

      PUSH_CONTINUATION_HOOK ();

      old_fp = fp;
      fp = vp->fp = old_fp + proc;
      SCM_FRAME_SET_DYNAMIC_LINK (fp, old_fp);
      SCM_FRAME_SET_RETURN_ADDRESS (fp, ip + 2);

      RESET_FRAME (nlocals);

      if (SCM_UNLIKELY (!SCM_PROGRAM_P (LOCAL_REF (0))))
        goto apply;

      ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

      APPLY_HOOK ();

      NEXT (0);
    }

  /* call-label proc:24 _:8 nlocals:24 label:32
   *
   * Call a procedure in the same compilation unit.
   *
   * This instruction is just like "call", except that instead of
   * dereferencing PROC to find the call target, the call target is
   * known to be at LABEL, a signed 32-bit offset in 32-bit units from
   * the current IP.  Since PROC is not dereferenced, it may be some
   * other representation of the closure.
   */
  VM_DEFINE_OP (2, call_label, "call-label", OP3 (U8_U24, X8_U24, L32))
    {
      scm_t_uint32 proc, nlocals;
      scm_t_int32 label;
      SCM *old_fp;

      UNPACK_24 (op, proc);
      UNPACK_24 (ip[1], nlocals);
      label = ip[2];

      VM_HANDLE_INTERRUPTS;

      PUSH_CONTINUATION_HOOK ();

      old_fp = fp;
      fp = vp->fp = old_fp + proc;
      SCM_FRAME_SET_DYNAMIC_LINK (fp, old_fp);
      SCM_FRAME_SET_RETURN_ADDRESS (fp, ip + 3);

      RESET_FRAME (nlocals);

      ip += label;

      APPLY_HOOK ();

      NEXT (0);
    }

  /* tail-call nlocals:24
   *
   * Tail-call a procedure.  Requires that the procedure and all of the
   * arguments have already been shuffled into position.  Will reset the
   * frame to NLOCALS.
   */
  VM_DEFINE_OP (3, tail_call, "tail-call", OP1 (U8_U24))
    {
      scm_t_uint32 nlocals;
      
      UNPACK_24 (op, nlocals);

      VM_HANDLE_INTERRUPTS;

      RESET_FRAME (nlocals);

      if (SCM_UNLIKELY (!SCM_PROGRAM_P (LOCAL_REF (0))))
        goto apply;

      ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

      APPLY_HOOK ();

      NEXT (0);
    }

  /* tail-call-label nlocals:24 label:32
   *
   * Tail-call a known procedure.  As call is to call-label, tail-call
   * is to tail-call-label.
   */
  VM_DEFINE_OP (4, tail_call_label, "tail-call-label", OP2 (U8_U24, L32))
    {
      scm_t_uint32 nlocals;
      scm_t_int32 label;
      
      UNPACK_24 (op, nlocals);
      label = ip[1];

      VM_HANDLE_INTERRUPTS;

      RESET_FRAME (nlocals);

      ip += label;

      APPLY_HOOK ();

      NEXT (0);
    }

  /* tail-call/shuffle from:24
   *
   * Tail-call a procedure.  The procedure should already be set to slot
   * 0.  The rest of the args are taken from the frame, starting at
   * FROM, shuffled down to start at slot 0.  This is part of the
   * implementation of the call-with-values builtin.
   */
  VM_DEFINE_OP (5, tail_call_shuffle, "tail-call/shuffle", OP1 (U8_U24))
    {
      scm_t_uint32 n, from, nlocals;

      UNPACK_24 (op, from);

      VM_HANDLE_INTERRUPTS;

      VM_ASSERT (from > 0, abort ());
      nlocals = FRAME_LOCALS_COUNT ();

      for (n = 0; from + n < nlocals; n++)
        LOCAL_SET (n + 1, LOCAL_REF (from + n));

      RESET_FRAME (n + 1);

      if (SCM_UNLIKELY (!SCM_PROGRAM_P (LOCAL_REF (0))))
        goto apply;

      ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

      APPLY_HOOK ();

      NEXT (0);
    }

  /* receive dst:12 proc:12 _:8 nlocals:24
   *
   * Receive a single return value from a call whose procedure was in
   * PROC, asserting that the call actually returned at least one
   * value.  Afterwards, resets the frame to NLOCALS locals.
   */
  VM_DEFINE_OP (6, receive, "receive", OP2 (U8_U12_U12, X8_U24) | OP_DST)
    {
      scm_t_uint16 dst, proc;
      scm_t_uint32 nlocals;
      UNPACK_12_12 (op, dst, proc);
      UNPACK_24 (ip[1], nlocals);
      VM_ASSERT (FRAME_LOCALS_COUNT () > proc + 1, vm_error_no_values ());
      LOCAL_SET (dst, LOCAL_REF (proc + 1));
      RESET_FRAME (nlocals);
      NEXT (2);
    }

  /* receive-values proc:24 allow-extra?:1 _:7 nvalues:24
   *
   * Receive a return of multiple values from a call whose procedure was
   * in PROC.  If fewer than NVALUES values were returned, signal an
   * error.  Unless ALLOW-EXTRA? is true, require that the number of
   * return values equals NVALUES exactly.  After receive-values has
   * run, the values can be copied down via `mov'.
   */
  VM_DEFINE_OP (7, receive_values, "receive-values", OP2 (U8_U24, B1_X7_U24))
    {
      scm_t_uint32 proc, nvalues;
      UNPACK_24 (op, proc);
      UNPACK_24 (ip[1], nvalues);
      if (ip[1] & 0x1)
        VM_ASSERT (FRAME_LOCALS_COUNT () > proc + nvalues,
                   vm_error_not_enough_values ());
      else
        VM_ASSERT (FRAME_LOCALS_COUNT () == proc + 1 + nvalues,
                   vm_error_wrong_number_of_values (nvalues));
      NEXT (2);
    }

  /* return src:24
   *
   * Return a value.
   */
  VM_DEFINE_OP (8, return, "return", OP1 (U8_U24))
    {
      scm_t_uint32 src;
      UNPACK_24 (op, src);
      RETURN_ONE_VALUE (LOCAL_REF (src));
    }

  /* return-values _:24
   *
   * Return a number of values from a call frame.  This opcode
   * corresponds to an application of `values' in tail position.  As
   * with tail calls, we expect that the values have already been
   * shuffled down to a contiguous array starting at slot 1.
   * We also expect the frame has already been reset.
   */
  VM_DEFINE_OP (9, return_values, "return-values", OP1 (U8_X24))
    {
      SCM *old_fp;

      VM_HANDLE_INTERRUPTS;

      old_fp = fp;
      ip = SCM_FRAME_RETURN_ADDRESS (fp);
      fp = vp->fp = SCM_FRAME_DYNAMIC_LINK (fp);

      /* Clear stack frame.  */
      old_fp[-1] = SCM_BOOL_F;
      old_fp[-2] = SCM_BOOL_F;

      POP_CONTINUATION_HOOK (old_fp);

      NEXT (0);
    }


  

  /*
   * Specialized call stubs
   */

  /* subr-call ptr-idx:24
   *
   * Call a subr, passing all locals in this frame as arguments.  Fetch
   * the foreign pointer from PTR-IDX, a free variable.  Return from the
   * calling frame.  This instruction is part of the trampolines
   * created in gsubr.c, and is not generated by the compiler.
   */
  VM_DEFINE_OP (10, subr_call, "subr-call", OP1 (U8_U24))
    {
      scm_t_uint32 ptr_idx;
      SCM pointer, ret;
      SCM (*subr)();

      UNPACK_24 (op, ptr_idx);

      pointer = SCM_PROGRAM_FREE_VARIABLE_REF (LOCAL_REF (0), ptr_idx);
      subr = SCM_POINTER_VALUE (pointer);

      SYNC_IP ();

      switch (FRAME_LOCALS_COUNT_FROM (1))
        {
        case 0:
          ret = subr ();
          break;
        case 1:
          ret = subr (fp[1]);
          break;
        case 2:
          ret = subr (fp[1], fp[2]);
          break;
        case 3:
          ret = subr (fp[1], fp[2], fp[3]);
          break;
        case 4:
          ret = subr (fp[1], fp[2], fp[3], fp[4]);
          break;
        case 5:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5]);
          break;
        case 6:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5], fp[6]);
          break;
        case 7:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], fp[7]);
          break;
        case 8:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], fp[7], fp[8]);
          break;
        case 9:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], fp[7], fp[8], fp[9]);
          break;
        case 10:
          ret = subr (fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], fp[7], fp[8], fp[9], fp[10]);
          break;
        default:
          abort ();
        }

      CACHE_FP ();

      if (SCM_UNLIKELY (SCM_VALUESP (ret)))
        /* multiple values returned to continuation */
        RETURN_VALUE_LIST (scm_struct_ref (ret, SCM_INUM0));
      else
        RETURN_ONE_VALUE (ret);
    }

  /* foreign-call cif-idx:12 ptr-idx:12
   *
   * Call a foreign function.  Fetch the CIF and foreign pointer from
   * CIF-IDX and PTR-IDX, both free variables.  Return from the calling
   * frame.  Arguments are taken from the stack.  This instruction is
   * part of the trampolines created by the FFI, and is not generated by
   * the compiler.
   */
  VM_DEFINE_OP (11, foreign_call, "foreign-call", OP1 (U8_U12_U12))
    {
      scm_t_uint16 cif_idx, ptr_idx;
      SCM closure, cif, pointer, ret;

      UNPACK_12_12 (op, cif_idx, ptr_idx);

      closure = LOCAL_REF (0);
      cif = SCM_PROGRAM_FREE_VARIABLE_REF (closure, cif_idx);
      pointer = SCM_PROGRAM_FREE_VARIABLE_REF (closure, ptr_idx);

      SYNC_IP ();

      // FIXME: separate args
      ret = scm_i_foreign_call (scm_inline_cons (thread, cif, pointer),
                                LOCAL_ADDRESS (1));

      CACHE_FP ();

      if (SCM_UNLIKELY (SCM_VALUESP (ret)))
        /* multiple values returned to continuation */
        RETURN_VALUE_LIST (scm_struct_ref (ret, SCM_INUM0));
      else
        RETURN_ONE_VALUE (ret);
    }

  /* continuation-call contregs:24
   *
   * Return to a continuation, nonlocally.  The arguments to the
   * continuation are taken from the stack.  CONTREGS is a free variable
   * containing the reified continuation.  This instruction is part of
   * the implementation of undelimited continuations, and is not
   * generated by the compiler.
   */
  VM_DEFINE_OP (12, continuation_call, "continuation-call", OP1 (U8_U24))
    {
      SCM contregs;
      scm_t_uint32 contregs_idx;

      UNPACK_24 (op, contregs_idx);

      contregs =
        SCM_PROGRAM_FREE_VARIABLE_REF (LOCAL_REF (0), contregs_idx);

      SYNC_IP ();
      scm_i_check_continuation (contregs);
      vm_return_to_continuation (scm_i_contregs_vp (contregs),
                                 scm_i_contregs_vm_cont (contregs),
                                 FRAME_LOCALS_COUNT_FROM (1),
                                 LOCAL_ADDRESS (1));
      scm_i_reinstate_continuation (contregs);

      /* no NEXT */
      abort ();
    }

  /* compose-continuation cont:24
   *
   * Compose a partial continution with the current continuation.  The
   * arguments to the continuation are taken from the stack.  CONT is a
   * free variable containing the reified continuation.  This
   * instruction is part of the implementation of partial continuations,
   * and is not generated by the compiler.
   */
  VM_DEFINE_OP (13, compose_continuation, "compose-continuation", OP1 (U8_U24))
    {
      SCM vmcont;
      scm_t_uint32 cont_idx;

      UNPACK_24 (op, cont_idx);
      vmcont = SCM_PROGRAM_FREE_VARIABLE_REF (LOCAL_REF (0), cont_idx);

      SYNC_IP ();
      VM_ASSERT (SCM_VM_CONT_REWINDABLE_P (vmcont),
                 vm_error_continuation_not_rewindable (vmcont));
      vm_reinstate_partial_continuation (vp, vmcont, FRAME_LOCALS_COUNT_FROM (1),
                                         LOCAL_ADDRESS (1),
                                         &thread->dynstack,
                                         registers);
      CACHE_REGISTER ();
      NEXT (0);
    }

  /* tail-apply _:24
   *
   * Tail-apply the procedure in local slot 0 to the rest of the
   * arguments.  This instruction is part of the implementation of
   * `apply', and is not generated by the compiler.
   */
  VM_DEFINE_OP (14, tail_apply, "tail-apply", OP1 (U8_X24))
    {
      int i, list_idx, list_len, nlocals;
      SCM list;

      VM_HANDLE_INTERRUPTS;

      nlocals = FRAME_LOCALS_COUNT ();
      // At a minimum, there should be apply, f, and the list.
      VM_ASSERT (nlocals >= 3, abort ());
      list_idx = nlocals - 1;
      list = LOCAL_REF (list_idx);
      list_len = scm_ilength (list);

      VM_ASSERT (list_len >= 0, vm_error_apply_to_non_list (list));

      nlocals = nlocals - 2 + list_len;
      ALLOC_FRAME (nlocals);

      for (i = 1; i < list_idx; i++)
        LOCAL_SET (i - 1, LOCAL_REF (i));

      /* Null out these slots, just in case there are less than 2 elements
         in the list. */
      LOCAL_SET (list_idx - 1, SCM_UNDEFINED);
      LOCAL_SET (list_idx, SCM_UNDEFINED);

      for (i = 0; i < list_len; i++, list = SCM_CDR (list))
        LOCAL_SET (list_idx - 1 + i, SCM_CAR (list));

      if (SCM_UNLIKELY (!SCM_PROGRAM_P (LOCAL_REF (0))))
        goto apply;

      ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

      APPLY_HOOK ();

      NEXT (0);
    }

  /* call/cc _:24
   *
   * Capture the current continuation, and tail-apply the procedure in
   * local slot 1 to it.  This instruction is part of the implementation
   * of `call/cc', and is not generated by the compiler.
   */
  VM_DEFINE_OP (15, call_cc, "call/cc", OP1 (U8_X24))
    {
      SCM vm_cont, cont;
      scm_t_dynstack *dynstack;
      int first;

      VM_HANDLE_INTERRUPTS;

      SYNC_IP ();
      dynstack = scm_dynstack_capture_all (&thread->dynstack);
      vm_cont = scm_i_vm_capture_stack (vp->stack_base,
                                        SCM_FRAME_DYNAMIC_LINK (fp),
                                        SCM_FRAME_PREVIOUS_SP (fp),
                                        SCM_FRAME_RETURN_ADDRESS (fp),
                                        dynstack,
                                        0);
      /* FIXME: Seems silly to capture the registers here, when they are
         already captured in the registers local, which here we are
         copying out to the heap; and likewise, the setjmp(&registers)
         code already has the non-local return handler.  But oh
         well!  */
      cont = scm_i_make_continuation (&first, vp, vm_cont);

      if (first)
        {
          LOCAL_SET (0, LOCAL_REF (1));
          LOCAL_SET (1, cont);
          RESET_FRAME (2);

          if (SCM_UNLIKELY (!SCM_PROGRAM_P (LOCAL_REF (0))))
            goto apply;

          ip = SCM_PROGRAM_CODE (LOCAL_REF (0));

          APPLY_HOOK ();

          NEXT (0);
        }
      else
        {
          CACHE_REGISTER ();
          ABORT_CONTINUATION_HOOK ();
          NEXT (0);
        }
    }

  /* abort _:24
   *
   * Abort to a prompt handler.  The tag is expected in r1, and the rest
   * of the values in the frame are returned to the prompt handler.
   * This corresponds to a tail application of abort-to-prompt.
   */
  VM_DEFINE_OP (16, abort, "abort", OP1 (U8_X24))
    {
      scm_t_uint32 nlocals = FRAME_LOCALS_COUNT ();

      ASSERT (nlocals >= 2);
      /* FIXME: Really we should capture the caller's registers.  Until
         then, manually advance the IP so that when the prompt resumes,
         it continues with the next instruction.  */
      ip++;
      SYNC_IP ();
      vm_abort (vp, LOCAL_REF (1), nlocals - 2, LOCAL_ADDRESS (2),
                SCM_EOL, LOCAL_ADDRESS (0), registers);

      /* vm_abort should not return */
      abort ();
    }

  /* builtin-ref dst:12 idx:12
   *
   * Load a builtin stub by index into DST.
   */
  VM_DEFINE_OP (17, builtin_ref, "builtin-ref", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, idx;

      UNPACK_12_12 (op, dst, idx);
      LOCAL_SET (dst, scm_vm_builtin_ref (idx));

      NEXT (1);
    }


  

  /*
   * Function prologues
   */

  /* br-if-nargs-ne expected:24 _:8 offset:24
   * br-if-nargs-lt expected:24 _:8 offset:24
   * br-if-nargs-gt expected:24 _:8 offset:24
   *
   * If the number of actual arguments is not equal, less than, or greater
   * than EXPECTED, respectively, add OFFSET, a signed 24-bit number, to
   * the current instruction pointer.
   */
  VM_DEFINE_OP (18, br_if_nargs_ne, "br-if-nargs-ne", OP2 (U8_U24, X8_L24))
    {
      BR_NARGS (!=);
    }
  VM_DEFINE_OP (19, br_if_nargs_lt, "br-if-nargs-lt", OP2 (U8_U24, X8_L24))
    {
      BR_NARGS (<);
    }
  VM_DEFINE_OP (20, br_if_nargs_gt, "br-if-nargs-gt", OP2 (U8_U24, X8_L24))
    {
      BR_NARGS (>);
    }

  /* assert-nargs-ee expected:24
   * assert-nargs-ge expected:24
   * assert-nargs-le expected:24
   *
   * If the number of actual arguments is not ==, >=, or <= EXPECTED,
   * respectively, signal an error.
   */
  VM_DEFINE_OP (21, assert_nargs_ee, "assert-nargs-ee", OP1 (U8_U24))
    {
      scm_t_uint32 expected;
      UNPACK_24 (op, expected);
      VM_ASSERT (FRAME_LOCALS_COUNT () == expected,
                 vm_error_wrong_num_args (LOCAL_REF (0)));
      NEXT (1);
    }
  VM_DEFINE_OP (22, assert_nargs_ge, "assert-nargs-ge", OP1 (U8_U24))
    {
      scm_t_uint32 expected;
      UNPACK_24 (op, expected);
      VM_ASSERT (FRAME_LOCALS_COUNT () >= expected,
                 vm_error_wrong_num_args (LOCAL_REF (0)));
      NEXT (1);
    }
  VM_DEFINE_OP (23, assert_nargs_le, "assert-nargs-le", OP1 (U8_U24))
    {
      scm_t_uint32 expected;
      UNPACK_24 (op, expected);
      VM_ASSERT (FRAME_LOCALS_COUNT () <= expected,
                 vm_error_wrong_num_args (LOCAL_REF (0)));
      NEXT (1);
    }

  /* alloc-frame nlocals:24
   *
   * Ensure that there is space on the stack for NLOCALS local variables,
   * setting them all to SCM_UNDEFINED, except those nargs values that
   * were passed as arguments and procedure.
   */
  VM_DEFINE_OP (24, alloc_frame, "alloc-frame", OP1 (U8_U24))
    {
      scm_t_uint32 nlocals, nargs;
      UNPACK_24 (op, nlocals);

      nargs = FRAME_LOCALS_COUNT ();
      ALLOC_FRAME (nlocals);
      while (nlocals-- > nargs)
        LOCAL_SET (nlocals, SCM_UNDEFINED);

      NEXT (1);
    }

  /* reset-frame nlocals:24
   *
   * Like alloc-frame, but doesn't check that the stack is big enough.
   * Used to reset the frame size to something less than the size that
   * was previously set via alloc-frame.
   */
  VM_DEFINE_OP (25, reset_frame, "reset-frame", OP1 (U8_U24))
    {
      scm_t_uint32 nlocals;
      UNPACK_24 (op, nlocals);
      RESET_FRAME (nlocals);
      NEXT (1);
    }

  /* assert-nargs-ee/locals expected:12 nlocals:12
   *
   * Equivalent to a sequence of assert-nargs-ee and reserve-locals.  The
   * number of locals reserved is EXPECTED + NLOCALS.
   */
  VM_DEFINE_OP (26, assert_nargs_ee_locals, "assert-nargs-ee/locals", OP1 (U8_U12_U12))
    {
      scm_t_uint16 expected, nlocals;
      UNPACK_12_12 (op, expected, nlocals);
      VM_ASSERT (FRAME_LOCALS_COUNT () == expected,
                 vm_error_wrong_num_args (LOCAL_REF (0)));
      ALLOC_FRAME (expected + nlocals);
      while (nlocals--)
        LOCAL_SET (expected + nlocals, SCM_UNDEFINED);

      NEXT (1);
    }

  /* br-if-npos-gt nreq:24 _:8 npos:24 _:8 offset:24
   *
   * Find the first positional argument after NREQ.  If it is greater
   * than NPOS, jump to OFFSET.
   *
   * This instruction is only emitted for functions with multiple
   * clauses, and an earlier clause has keywords and no rest arguments.
   * See "Case-lambda" in the manual, for more on how case-lambda
   * chooses the clause to apply.
   */
  VM_DEFINE_OP (27, br_if_npos_gt, "br-if-npos-gt", OP3 (U8_U24, X8_U24, X8_L24))
    {
      scm_t_uint32 nreq, npos;

      UNPACK_24 (op, nreq);
      UNPACK_24 (ip[1], npos);

      /* We can only have too many positionals if there are more
         arguments than NPOS.  */
      if (FRAME_LOCALS_COUNT() > npos)
        {
          scm_t_uint32 n;
          for (n = nreq; n < npos; n++)
            if (scm_is_keyword (LOCAL_REF (n)))
              break;
          if (n == npos && !scm_is_keyword (LOCAL_REF (n)))
            {
              scm_t_int32 offset = ip[2];
              offset >>= 8; /* Sign-extending shift. */
              NEXT (offset);
            }
        }
      NEXT (3);
    }

  /* bind-kwargs nreq:24 flags:8 nreq-and-opt:24 _:8 ntotal:24 kw-offset:32
   *
   * flags := allow-other-keys:1 has-rest:1 _:6
   *
   * Find the last positional argument, and shuffle all the rest above
   * NTOTAL.  Initialize the intervening locals to SCM_UNDEFINED.  Then
   * load the constant at KW-OFFSET words from the current IP, and use it
   * to bind keyword arguments.  If HAS-REST, collect all shuffled
   * arguments into a list, and store it in NREQ-AND-OPT.  Finally, clear
   * the arguments that we shuffled up.
   *
   * A macro-mega-instruction.
   */
  VM_DEFINE_OP (28, bind_kwargs, "bind-kwargs", OP4 (U8_U24, U8_U24, X8_U24, N32))
    {
      scm_t_uint32 nreq, nreq_and_opt, ntotal, npositional, nkw, n, nargs;
      scm_t_int32 kw_offset;
      scm_t_bits kw_bits;
      SCM kw;
      char allow_other_keys, has_rest;

      UNPACK_24 (op, nreq);
      allow_other_keys = ip[1] & 0x1;
      has_rest = ip[1] & 0x2;
      UNPACK_24 (ip[1], nreq_and_opt);
      UNPACK_24 (ip[2], ntotal);
      kw_offset = ip[3];
      kw_bits = (scm_t_bits) (ip + kw_offset);
      VM_ASSERT (!(kw_bits & 0x7), abort());
      kw = SCM_PACK (kw_bits);

      nargs = FRAME_LOCALS_COUNT ();

      /* look in optionals for first keyword or last positional */
      /* starting after the last required positional arg */
      npositional = nreq;
      while (/* while we have args */
             npositional < nargs
             /* and we still have positionals to fill */
             && npositional < nreq_and_opt
             /* and we haven't reached a keyword yet */
             && !scm_is_keyword (LOCAL_REF (npositional)))
        /* bind this optional arg (by leaving it in place) */
        npositional++;
      nkw = nargs - npositional;
      /* shuffle non-positional arguments above ntotal */
      ALLOC_FRAME (ntotal + nkw);
      n = nkw;
      while (n--)
        LOCAL_SET (ntotal + n, LOCAL_REF (npositional + n));
      /* and fill optionals & keyword args with SCM_UNDEFINED */
      n = npositional;
      while (n < ntotal)
        LOCAL_SET (n++, SCM_UNDEFINED);

      VM_ASSERT (has_rest || (nkw % 2) == 0,
                 vm_error_kwargs_length_not_even (LOCAL_REF (0)));

      /* Now bind keywords, in the order given.  */
      for (n = 0; n < nkw; n++)
        if (scm_is_keyword (LOCAL_REF (ntotal + n)))
          {
            SCM walk;
            for (walk = kw; scm_is_pair (walk); walk = SCM_CDR (walk))
              if (scm_is_eq (SCM_CAAR (walk), LOCAL_REF (ntotal + n)))
                {
                  SCM si = SCM_CDAR (walk);
                  LOCAL_SET (SCM_I_INUMP (si) ? SCM_I_INUM (si) : scm_to_uint32 (si),
                             LOCAL_REF (ntotal + n + 1));
                  break;
                }
            VM_ASSERT (scm_is_pair (walk) || allow_other_keys,
                       vm_error_kwargs_unrecognized_keyword (LOCAL_REF (0),
                                                             LOCAL_REF (ntotal + n)));
            n++;
          }
        else
          VM_ASSERT (has_rest, vm_error_kwargs_invalid_keyword (LOCAL_REF (0),
                                                                LOCAL_REF (ntotal + n)));

      if (has_rest)
        {
          SCM rest = SCM_EOL;
          n = nkw;
          while (n--)
            rest = scm_inline_cons (thread, LOCAL_REF (ntotal + n), rest);
          LOCAL_SET (nreq_and_opt, rest);
        }

      RESET_FRAME (ntotal);

      NEXT (4);
    }

  /* bind-rest dst:24
   *
   * Collect any arguments at or above DST into a list, and store that
   * list at DST.
   */
  VM_DEFINE_OP (29, bind_rest, "bind-rest", OP1 (U8_U24) | OP_DST)
    {
      scm_t_uint32 dst, nargs;
      SCM rest = SCM_EOL;

      UNPACK_24 (op, dst);
      nargs = FRAME_LOCALS_COUNT ();

      if (nargs <= dst)
        {
          ALLOC_FRAME (dst + 1);
          while (nargs < dst)
            LOCAL_SET (nargs++, SCM_UNDEFINED);
        }
      else
        {
          while (nargs-- > dst)
            {
              rest = scm_inline_cons (thread, LOCAL_REF (nargs), rest);
              LOCAL_SET (nargs, SCM_UNDEFINED);
            }

          RESET_FRAME (dst + 1);
        }

      LOCAL_SET (dst, rest);

      NEXT (1);
    }


  

  /*
   * Branching instructions
   */

  /* br offset:24
   *
   * Add OFFSET, a signed 24-bit number, to the current instruction
   * pointer.
   */
  VM_DEFINE_OP (30, br, "br", OP1 (U8_L24))
    {
      scm_t_int32 offset = op;
      offset >>= 8; /* Sign-extending shift. */
      if (offset <= 0)
        VM_HANDLE_INTERRUPTS;
      NEXT (offset);
    }

  /* br-if-true test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is true for the purposes of Scheme, add
   * OFFSET, a signed 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (31, br_if_true, "br-if-true", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, scm_is_true (x));
    }

  /* br-if-null test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is the end-of-list or Lisp nil, add OFFSET, a
   * signed 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (32, br_if_null, "br-if-null", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, scm_is_null (x));
    }

  /* br-if-nil test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is false to Lisp, add OFFSET, a signed 24-bit
   * number, to the current instruction pointer.
   */
  VM_DEFINE_OP (33, br_if_nil, "br-if-nil", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, scm_is_lisp_false (x));
    }

  /* br-if-pair test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is a pair, add OFFSET, a signed 24-bit number,
   * to the current instruction pointer.
   */
  VM_DEFINE_OP (34, br_if_pair, "br-if-pair", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, scm_is_pair (x));
    }

  /* br-if-struct test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is a struct, add OFFSET, a signed 24-bit
   * number, to the current instruction pointer.
   */
  VM_DEFINE_OP (35, br_if_struct, "br-if-struct", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, SCM_STRUCTP (x));
    }

  /* br-if-char test:24 invert:1 _:7 offset:24
   *
   * If the value in TEST is a char, add OFFSET, a signed 24-bit number,
   * to the current instruction pointer.
   */
  VM_DEFINE_OP (36, br_if_char, "br-if-char", OP2 (U8_U24, B1_X7_L24))
    {
      BR_UNARY (x, SCM_CHARP (x));
    }

  /* br-if-tc7 test:24 invert:1 tc7:7 offset:24
   *
   * If the value in TEST has the TC7 given in the second word, add
   * OFFSET, a signed 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (37, br_if_tc7, "br-if-tc7", OP2 (U8_U24, B1_U7_L24))
    {
      BR_UNARY (x, SCM_HAS_TYP7 (x, (ip[1] >> 1) & 0x7f));
    }

  /* br-if-eq a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is eq? to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (38, br_if_eq, "br-if-eq", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_BINARY (x, y, scm_is_eq (x, y));
    }

  /* br-if-eqv a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is eqv? to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (39, br_if_eqv, "br-if-eqv", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_BINARY (x, y,
                 scm_is_eq (x, y)
                 || (SCM_NIMP (x) && SCM_NIMP (y)
                     && scm_is_true (scm_eqv_p (x, y))));
    }

  // FIXME: remove, have compiler inline eqv test instead
  /* br-if-equal a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is equal? to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  // FIXME: Should sync_ip before calling out and cache_fp before coming
  // back!  Another reason to remove this opcode!
  VM_DEFINE_OP (40, br_if_equal, "br-if-equal", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_BINARY (x, y,
                 scm_is_eq (x, y)
                 || (SCM_NIMP (x) && SCM_NIMP (y)
                     && scm_is_true (scm_equal_p (x, y))));
    }

  /* br-if-= a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is = to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (41, br_if_ee, "br-if-=", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_ARITHMETIC (==, scm_num_eq_p);
    }

  /* br-if-< a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is < to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (42, br_if_lt, "br-if-<", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_ARITHMETIC (<, scm_less_p);
    }

  /* br-if-<= a:12 b:12 invert:1 _:7 offset:24
   *
   * If the value in A is <= to the value in B, add OFFSET, a signed
   * 24-bit number, to the current instruction pointer.
   */
  VM_DEFINE_OP (43, br_if_le, "br-if-<=", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_ARITHMETIC (<=, scm_leq_p);
    }


  

  /*
   * Lexical binding instructions
   */

  /* mov dst:12 src:12
   *
   * Copy a value from one local slot to another.
   */
  VM_DEFINE_OP (44, mov, "mov", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst;
      scm_t_uint16 src;

      UNPACK_12_12 (op, dst, src);
      LOCAL_SET (dst, LOCAL_REF (src));

      NEXT (1);
    }

  /* long-mov dst:24 _:8 src:24
   *
   * Copy a value from one local slot to another.
   */
  VM_DEFINE_OP (45, long_mov, "long-mov", OP2 (U8_U24, X8_U24) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_uint32 src;

      UNPACK_24 (op, dst);
      UNPACK_24 (ip[1], src);
      LOCAL_SET (dst, LOCAL_REF (src));

      NEXT (2);
    }

  /* box dst:12 src:12
   *
   * Create a new variable holding SRC, and place it in DST.
   */
  VM_DEFINE_OP (46, box, "box", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;
      UNPACK_12_12 (op, dst, src);
      LOCAL_SET (dst, scm_inline_cell (thread, scm_tc7_variable,
                                       SCM_UNPACK (LOCAL_REF (src))));
      NEXT (1);
    }

  /* box-ref dst:12 src:12
   *
   * Unpack the variable at SRC into DST, asserting that the variable is
   * actually bound.
   */
  VM_DEFINE_OP (47, box_ref, "box-ref", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;
      SCM var;
      UNPACK_12_12 (op, dst, src);
      var = LOCAL_REF (src);
      VM_ASSERT (SCM_VARIABLEP (var),
                 vm_error_not_a_variable ("variable-ref", var));
      VM_ASSERT (VARIABLE_BOUNDP (var), vm_error_unbound (var));
      LOCAL_SET (dst, VARIABLE_REF (var));
      NEXT (1);
    }

  /* box-set! dst:12 src:12
   *
   * Set the contents of the variable at DST to SET.
   */
  VM_DEFINE_OP (48, box_set, "box-set!", OP1 (U8_U12_U12))
    {
      scm_t_uint16 dst, src;
      SCM var;
      UNPACK_12_12 (op, dst, src);
      var = LOCAL_REF (dst);
      VM_ASSERT (SCM_VARIABLEP (var),
                 vm_error_not_a_variable ("variable-set!", var));
      VARIABLE_SET (var, LOCAL_REF (src));
      NEXT (1);
    }

  /* make-closure dst:24 offset:32 _:8 nfree:24
   *
   * Make a new closure, and write it to DST.  The code for the closure
   * will be found at OFFSET words from the current IP.  OFFSET is a
   * signed 32-bit integer.  Space for NFREE free variables will be
   * allocated.
   */
  VM_DEFINE_OP (49, make_closure, "make-closure", OP3 (U8_U24, L32, X8_U24) | OP_DST)
    {
      scm_t_uint32 dst, nfree, n;
      scm_t_int32 offset;
      SCM closure;

      UNPACK_24 (op, dst);
      offset = ip[1];
      UNPACK_24 (ip[2], nfree);

      // FIXME: Assert range of nfree?
      closure = scm_inline_words (thread, scm_tc7_program | (nfree << 16),
                                  nfree + 2);
      SCM_SET_CELL_WORD_1 (closure, ip + offset);
      // FIXME: Elide these initializations?
      for (n = 0; n < nfree; n++)
        SCM_PROGRAM_FREE_VARIABLE_SET (closure, n, SCM_BOOL_F);
      LOCAL_SET (dst, closure);
      NEXT (3);
    }

  /* free-ref dst:12 src:12 _:8 idx:24
   *
   * Load free variable IDX from the closure SRC into local slot DST.
   */
  VM_DEFINE_OP (50, free_ref, "free-ref", OP2 (U8_U12_U12, X8_U24) | OP_DST)
    {
      scm_t_uint16 dst, src;
      scm_t_uint32 idx;
      UNPACK_12_12 (op, dst, src);
      UNPACK_24 (ip[1], idx);
      /* CHECK_FREE_VARIABLE (src); */
      LOCAL_SET (dst, SCM_PROGRAM_FREE_VARIABLE_REF (LOCAL_REF (src), idx));
      NEXT (2);
    }

  /* free-set! dst:12 src:12 _:8 idx:24
   *
   * Set free variable IDX from the closure DST to SRC.
   */
  VM_DEFINE_OP (51, free_set, "free-set!", OP2 (U8_U12_U12, X8_U24))
    {
      scm_t_uint16 dst, src;
      scm_t_uint32 idx;
      UNPACK_12_12 (op, dst, src);
      UNPACK_24 (ip[1], idx);
      /* CHECK_FREE_VARIABLE (src); */
      SCM_PROGRAM_FREE_VARIABLE_SET (LOCAL_REF (dst), idx, LOCAL_REF (src));
      NEXT (2);
    }


  

  /*
   * Immediates and statically allocated non-immediates
   */

  /* make-short-immediate dst:8 low-bits:16
   *
   * Make an immediate whose low bits are LOW-BITS, and whose top bits are
   * 0.
   */
  VM_DEFINE_OP (52, make_short_immediate, "make-short-immediate", OP1 (U8_U8_I16) | OP_DST)
    {
      scm_t_uint8 dst;
      scm_t_bits val;

      UNPACK_8_16 (op, dst, val);
      LOCAL_SET (dst, SCM_PACK (val));
      NEXT (1);
    }

  /* make-long-immediate dst:24 low-bits:32
   *
   * Make an immediate whose low bits are LOW-BITS, and whose top bits are
   * 0.
   */
  VM_DEFINE_OP (53, make_long_immediate, "make-long-immediate", OP2 (U8_U24, I32) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_bits val;

      UNPACK_24 (op, dst);
      val = ip[1];
      LOCAL_SET (dst, SCM_PACK (val));
      NEXT (2);
    }

  /* make-long-long-immediate dst:24 high-bits:32 low-bits:32
   *
   * Make an immediate with HIGH-BITS and LOW-BITS.
   */
  VM_DEFINE_OP (54, make_long_long_immediate, "make-long-long-immediate", OP3 (U8_U24, A32, B32) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_bits val;

      UNPACK_24 (op, dst);
#if SIZEOF_SCM_T_BITS > 4
      val = ip[1];
      val <<= 32;
      val |= ip[2];
#else
      ASSERT (ip[1] == 0);
      val = ip[2];
#endif
      LOCAL_SET (dst, SCM_PACK (val));
      NEXT (3);
    }

  /* make-non-immediate dst:24 offset:32
   *
   * Load a pointer to statically allocated memory into DST.  The
   * object's memory is will be found OFFSET 32-bit words away from the
   * current instruction pointer.  OFFSET is a signed value.  The
   * intention here is that the compiler would produce an object file
   * containing the words of a non-immediate object, and this
   * instruction creates a pointer to that memory, effectively
   * resurrecting that object.
   *
   * Whether the object is mutable or immutable depends on where it was
   * allocated by the compiler, and loaded by the loader.
   */
  VM_DEFINE_OP (55, make_non_immediate, "make-non-immediate", OP2 (U8_U24, N32) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_int32 offset;
      scm_t_uint32* loc;
      scm_t_bits unpacked;

      UNPACK_24 (op, dst);
      offset = ip[1];
      loc = ip + offset;
      unpacked = (scm_t_bits) loc;

      VM_ASSERT (!(unpacked & 0x7), abort());

      LOCAL_SET (dst, SCM_PACK (unpacked));

      NEXT (2);
    }

  /* static-ref dst:24 offset:32
   *
   * Load a SCM value into DST.  The SCM value will be fetched from
   * memory, OFFSET 32-bit words away from the current instruction
   * pointer.  OFFSET is a signed value.
   *
   * The intention is for this instruction to be used to load constants
   * that the compiler is unable to statically allocate, like symbols.
   * These values would be initialized when the object file loads.
   */
  VM_DEFINE_OP (56, static_ref, "static-ref", OP2 (U8_U24, S32) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_int32 offset;
      scm_t_uint32* loc;
      scm_t_uintptr loc_bits;

      UNPACK_24 (op, dst);
      offset = ip[1];
      loc = ip + offset;
      loc_bits = (scm_t_uintptr) loc;
      VM_ASSERT (ALIGNED_P (loc, SCM), abort());

      LOCAL_SET (dst, *((SCM *) loc_bits));

      NEXT (2);
    }

  /* static-set! src:24 offset:32
   *
   * Store a SCM value into memory, OFFSET 32-bit words away from the
   * current instruction pointer.  OFFSET is a signed value.
   */
  VM_DEFINE_OP (57, static_set, "static-set!", OP2 (U8_U24, LO32))
    {
      scm_t_uint32 src;
      scm_t_int32 offset;
      scm_t_uint32* loc;

      UNPACK_24 (op, src);
      offset = ip[1];
      loc = ip + offset;
      VM_ASSERT (ALIGNED_P (loc, SCM), abort());

      *((SCM *) loc) = LOCAL_REF (src);

      NEXT (2);
    }

  /* static-patch! _:24 dst-offset:32 src-offset:32
   *
   * Patch a pointer at DST-OFFSET to point to SRC-OFFSET.  Both offsets
   * are signed 32-bit values, indicating a memory address as a number
   * of 32-bit words away from the current instruction pointer.
   */
  VM_DEFINE_OP (58, static_patch, "static-patch!", OP3 (U8_X24, LO32, L32))
    {
      scm_t_int32 dst_offset, src_offset;
      void *src;
      void** dst_loc;

      dst_offset = ip[1];
      src_offset = ip[2];

      dst_loc = (void **) (ip + dst_offset);
      src = ip + src_offset;
      VM_ASSERT (ALIGNED_P (dst_loc, void*), abort());

      *dst_loc = src;

      NEXT (3);
    }

  

  /*
   * Mutable top-level bindings
   */

  /* There are three slightly different ways to resolve toplevel
     variables.

     1. A toplevel reference outside of a function.  These need to be
        looked up when the expression is evaluated -- no later, and no
        before.  They are looked up relative to the module that is
        current when the expression is evaluated.  For example:

          (if (foo) a b)

        The "resolve" instruction resolves the variable (box), and then
        access is via box-ref or box-set!.

     2. A toplevel reference inside a function.  These are looked up
        relative to the module that was current when the function was
        defined.  Unlike code at the toplevel, which is usually run only
        once, these bindings benefit from memoized lookup, in which the
        variable resulting from the lookup is cached in the function.

          (lambda () (if (foo) a b))

        The toplevel-box instruction is equivalent to "resolve", but
        caches the resulting variable in statically allocated memory.

     3. A reference to an identifier with respect to a particular
        module.  This can happen for primitive references, and
        references residualized by macro expansions.  These can always
        be cached.  Use module-box for these.
     */

  /* current-module dst:24
   *
   * Store the current module in DST.
   */
  VM_DEFINE_OP (59, current_module, "current-module", OP1 (U8_U24) | OP_DST)
    {
      scm_t_uint32 dst;

      UNPACK_24 (op, dst);

      SYNC_IP ();
      LOCAL_SET (dst, scm_current_module ());

      NEXT (1);
    }

  /* resolve dst:24 bound?:1 _:7 sym:24
   *
   * Resolve SYM in the current module, and place the resulting variable
   * in DST.
   */
  VM_DEFINE_OP (60, resolve, "resolve", OP2 (U8_U24, B1_X7_U24) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_uint32 sym;
      SCM var;

      UNPACK_24 (op, dst);
      UNPACK_24 (ip[1], sym);

      SYNC_IP ();
      var = scm_lookup (LOCAL_REF (sym));
      CACHE_FP ();
      if (ip[1] & 0x1)
        VM_ASSERT (VARIABLE_BOUNDP (var), vm_error_unbound (LOCAL_REF (sym)));
      LOCAL_SET (dst, var);

      NEXT (2);
    }

  /* define! sym:12 val:12
   *
   * Look up a binding for SYM in the current module, creating it if
   * necessary.  Set its value to VAL.
   */
  VM_DEFINE_OP (61, define, "define!", OP1 (U8_U12_U12))
    {
      scm_t_uint16 sym, val;
      UNPACK_12_12 (op, sym, val);
      SYNC_IP ();
      scm_define (LOCAL_REF (sym), LOCAL_REF (val));
      CACHE_FP ();
      NEXT (1);
    }

  /* toplevel-box dst:24 var-offset:32 mod-offset:32 sym-offset:32 bound?:1 _:31
   *
   * Load a SCM value.  The SCM value will be fetched from memory,
   * VAR-OFFSET 32-bit words away from the current instruction pointer.
   * VAR-OFFSET is a signed value.  Up to here, toplevel-box is like
   * static-ref.
   *
   * Then, if the loaded value is a variable, it is placed in DST, and control
   * flow continues.
   *
   * Otherwise, we have to resolve the variable.  In that case we load
   * the module from MOD-OFFSET, just as we loaded the variable.
   * Usually the module gets set when the closure is created.  The name
   * is an offset to a symbol.
   *
   * We use the module and the symbol to resolve the variable, placing it in
   * DST, and caching the resolved variable so that we will hit the cache next
   * time.
   */
  VM_DEFINE_OP (62, toplevel_box, "toplevel-box", OP5 (U8_U24, S32, S32, N32, B1_X31) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_int32 var_offset;
      scm_t_uint32* var_loc_u32;
      SCM *var_loc;
      SCM var;

      UNPACK_24 (op, dst);
      var_offset = ip[1];
      var_loc_u32 = ip + var_offset;
      VM_ASSERT (ALIGNED_P (var_loc_u32, SCM), abort());
      var_loc = (SCM *) var_loc_u32;
      var = *var_loc;

      if (SCM_UNLIKELY (!SCM_VARIABLEP (var)))
        {
          SCM mod, sym;
          scm_t_int32 mod_offset = ip[2]; /* signed */
          scm_t_int32 sym_offset = ip[3]; /* signed */
          scm_t_uint32 *mod_loc = ip + mod_offset;
          scm_t_uint32 *sym_loc = ip + sym_offset;
          
          SYNC_IP ();

          VM_ASSERT (ALIGNED_P (mod_loc, SCM), abort());
          VM_ASSERT (ALIGNED_P (sym_loc, SCM), abort());

          mod = *((SCM *) mod_loc);
          sym = *((SCM *) sym_loc);

          /* If the toplevel scope was captured before modules were
             booted, use the root module.  */
          if (scm_is_false (mod))
            mod = scm_the_root_module ();

          var = scm_module_lookup (mod, sym);
          CACHE_FP ();
          if (ip[4] & 0x1)
            VM_ASSERT (VARIABLE_BOUNDP (var), vm_error_unbound (sym));

          *var_loc = var;
        }

      LOCAL_SET (dst, var);
      NEXT (5);
    }

  /* module-box dst:24 var-offset:32 mod-offset:32 sym-offset:32 bound?:1 _:31
   *
   * Like toplevel-box, except MOD-OFFSET points at the name of a module
   * instead of the module itself.
   */
  VM_DEFINE_OP (63, module_box, "module-box", OP5 (U8_U24, S32, N32, N32, B1_X31) | OP_DST)
    {
      scm_t_uint32 dst;
      scm_t_int32 var_offset;
      scm_t_uint32* var_loc_u32;
      SCM *var_loc;
      SCM var;

      UNPACK_24 (op, dst);
      var_offset = ip[1];
      var_loc_u32 = ip + var_offset;
      VM_ASSERT (ALIGNED_P (var_loc_u32, SCM), abort());
      var_loc = (SCM *) var_loc_u32;
      var = *var_loc;

      if (SCM_UNLIKELY (!SCM_VARIABLEP (var)))
        {
          SCM modname, sym;
          scm_t_int32 modname_offset = ip[2]; /* signed */
          scm_t_int32 sym_offset = ip[3]; /* signed */
          scm_t_uint32 *modname_words = ip + modname_offset;
          scm_t_uint32 *sym_loc = ip + sym_offset;

          SYNC_IP ();

          VM_ASSERT (!(((scm_t_uintptr) modname_words) & 0x7), abort());
          VM_ASSERT (ALIGNED_P (sym_loc, SCM), abort());

          modname = SCM_PACK ((scm_t_bits) modname_words);
          sym = *((SCM *) sym_loc);

          if (!scm_module_system_booted_p)
            {
              ASSERT (scm_is_true
                      scm_equal_p (modname,
                                   scm_list_2
                                   (SCM_BOOL_T,
                                    scm_from_utf8_symbol ("guile"))));
              var = scm_lookup (sym);
            }
          else if (scm_is_true (SCM_CAR (modname)))
            var = scm_public_lookup (SCM_CDR (modname), sym);
          else
            var = scm_private_lookup (SCM_CDR (modname), sym);

          CACHE_FP ();

          if (ip[4] & 0x1)
            VM_ASSERT (VARIABLE_BOUNDP (var), vm_error_unbound (sym));

          *var_loc = var;
        }

      LOCAL_SET (dst, var);
      NEXT (5);
    }

  

  /*
   * The dynamic environment
   */

  /* prompt tag:24 escape-only?:1 _:7 proc-slot:24 _:8 handler-offset:24
   *
   * Push a new prompt on the dynamic stack, with a tag from TAG and a
   * handler at HANDLER-OFFSET words from the current IP.  The handler
   * will expect a multiple-value return as if from a call with the
   * procedure at PROC-SLOT.
   */
  VM_DEFINE_OP (64, prompt, "prompt", OP3 (U8_U24, B1_X7_U24, X8_L24))
    {
      scm_t_uint32 tag, proc_slot;
      scm_t_int32 offset;
      scm_t_uint8 escape_only_p;
      scm_t_dynstack_prompt_flags flags;

      UNPACK_24 (op, tag);
      escape_only_p = ip[1] & 0x1;
      UNPACK_24 (ip[1], proc_slot);
      offset = ip[2];
      offset >>= 8; /* Sign extension */
  
      /* Push the prompt onto the dynamic stack. */
      flags = escape_only_p ? SCM_F_DYNSTACK_PROMPT_ESCAPE_ONLY : 0;
      scm_dynstack_push_prompt (&thread->dynstack, flags,
                                LOCAL_REF (tag),
                                fp - vp->stack_base,
                                LOCAL_ADDRESS (proc_slot) - vp->stack_base,
                                ip + offset,
                                registers);
      NEXT (3);
    }

  /* wind winder:12 unwinder:12
   *
   * Push wind and unwind procedures onto the dynamic stack. Note that
   * neither are actually called; the compiler should emit calls to wind
   * and unwind for the normal dynamic-wind control flow.  Also note that
   * the compiler should have inserted checks that they wind and unwind
   * procs are thunks, if it could not prove that to be the case.
   */
  VM_DEFINE_OP (65, wind, "wind", OP1 (U8_U12_U12))
    {
      scm_t_uint16 winder, unwinder;
      UNPACK_12_12 (op, winder, unwinder);
      scm_dynstack_push_dynwind (&thread->dynstack,
                                 LOCAL_REF (winder), LOCAL_REF (unwinder));
      NEXT (1);
    }

  /* unwind _:24
   *
   * A normal exit from the dynamic extent of an expression. Pop the top
   * entry off of the dynamic stack.
   */
  VM_DEFINE_OP (66, unwind, "unwind", OP1 (U8_X24))
    {
      scm_dynstack_pop (&thread->dynstack);
      NEXT (1);
    }

  /* push-fluid fluid:12 value:12
   *
   * Dynamically bind VALUE to FLUID.
   */
  VM_DEFINE_OP (67, push_fluid, "push-fluid", OP1 (U8_U12_U12))
    {
      scm_t_uint32 fluid, value;

      UNPACK_12_12 (op, fluid, value);

      scm_dynstack_push_fluid (&thread->dynstack,
                               LOCAL_REF (fluid), LOCAL_REF (value),
                               thread->dynamic_state);
      NEXT (1);
    }

  /* pop-fluid _:24
   *
   * Leave the dynamic extent of a with-fluid* expression, restoring the
   * fluid to its previous value.
   */
  VM_DEFINE_OP (68, pop_fluid, "pop-fluid", OP1 (U8_X24))
    {
      /* This function must not allocate.  */
      scm_dynstack_unwind_fluid (&thread->dynstack,
                                 thread->dynamic_state);
      NEXT (1);
    }

  /* fluid-ref dst:12 src:12
   *
   * Reference the fluid in SRC, and place the value in DST.
   */
  VM_DEFINE_OP (69, fluid_ref, "fluid-ref", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;
      size_t num;
      SCM fluid, fluids;

      UNPACK_12_12 (op, dst, src);
      fluid = LOCAL_REF (src);
      fluids = SCM_I_DYNAMIC_STATE_FLUIDS (thread->dynamic_state);
      if (SCM_UNLIKELY (!SCM_FLUID_P (fluid))
          || ((num = SCM_I_FLUID_NUM (fluid)) >= SCM_SIMPLE_VECTOR_LENGTH (fluids)))
        {
          /* Punt dynstate expansion and error handling to the C proc. */
          SYNC_IP ();
          LOCAL_SET (dst, scm_fluid_ref (fluid));
        }
      else
        {
          SCM val = SCM_SIMPLE_VECTOR_REF (fluids, num);
          if (scm_is_eq (val, SCM_UNDEFINED))
            val = SCM_I_FLUID_DEFAULT (fluid);
          VM_ASSERT (!scm_is_eq (val, SCM_UNDEFINED),
                     vm_error_unbound_fluid (fluid));
          LOCAL_SET (dst, val);
        }

      NEXT (1);
    }

  /* fluid-set fluid:12 val:12
   *
   * Set the value of the fluid in DST to the value in SRC.
   */
  VM_DEFINE_OP (70, fluid_set, "fluid-set", OP1 (U8_U12_U12))
    {
      scm_t_uint16 a, b;
      size_t num;
      SCM fluid, fluids;

      UNPACK_12_12 (op, a, b);
      fluid = LOCAL_REF (a);
      fluids = SCM_I_DYNAMIC_STATE_FLUIDS (thread->dynamic_state);
      if (SCM_UNLIKELY (!SCM_FLUID_P (fluid))
          || ((num = SCM_I_FLUID_NUM (fluid)) >= SCM_SIMPLE_VECTOR_LENGTH (fluids)))
        {
          /* Punt dynstate expansion and error handling to the C proc. */
          SYNC_IP ();
          scm_fluid_set_x (fluid, LOCAL_REF (b));
        }
      else
        SCM_SIMPLE_VECTOR_SET (fluids, num, LOCAL_REF (b));

      NEXT (1);
    }


  

  /*
   * Strings, symbols, and keywords
   */

  /* string-length dst:12 src:12
   *
   * Store the length of the string in SRC in DST.
   */
  VM_DEFINE_OP (71, string_length, "string-length", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (str);
      if (SCM_LIKELY (scm_is_string (str)))
        RETURN (SCM_I_MAKINUM (scm_i_string_length (str)));
      else
        {
          SYNC_IP ();
          RETURN (scm_string_length (str));
        }
    }

  /* string-ref dst:8 src:8 idx:8
   *
   * Fetch the character at position IDX in the string in SRC, and store
   * it in DST.
   */
  VM_DEFINE_OP (72, string_ref, "string-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_signed_bits i = 0;
      ARGS2 (str, idx);
      if (SCM_LIKELY (scm_is_string (str)
                      && SCM_I_INUMP (idx)
                      && ((i = SCM_I_INUM (idx)) >= 0)
                      && i < scm_i_string_length (str)))
        RETURN (SCM_MAKE_CHAR (scm_i_string_ref (str, i)));
      else
        {
          SYNC_IP ();
          RETURN (scm_string_ref (str, idx));
        }
    }

  /* No string-set! instruction, as there is no good fast path there.  */

  /* string->number dst:12 src:12
   *
   * Parse a string in SRC to a number, and store in DST.
   */
  VM_DEFINE_OP (73, string_to_number, "string->number", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;

      UNPACK_12_12 (op, dst, src);
      SYNC_IP ();
      LOCAL_SET (dst,
                 scm_string_to_number (LOCAL_REF (src),
                                       SCM_UNDEFINED /* radix = 10 */));
      NEXT (1);
    }

  /* string->symbol dst:12 src:12
   *
   * Parse a string in SRC to a symbol, and store in DST.
   */
  VM_DEFINE_OP (74, string_to_symbol, "string->symbol", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;

      UNPACK_12_12 (op, dst, src);
      SYNC_IP ();
      LOCAL_SET (dst, scm_string_to_symbol (LOCAL_REF (src)));
      NEXT (1);
    }

  /* symbol->keyword dst:12 src:12
   *
   * Make a keyword from the symbol in SRC, and store it in DST.
   */
  VM_DEFINE_OP (75, symbol_to_keyword, "symbol->keyword", OP1 (U8_U12_U12) | OP_DST)
    {
      scm_t_uint16 dst, src;
      UNPACK_12_12 (op, dst, src);
      SYNC_IP ();
      LOCAL_SET (dst, scm_symbol_to_keyword (LOCAL_REF (src)));
      NEXT (1);
    }

  

  /*
   * Pairs
   */

  /* cons dst:8 car:8 cdr:8
   *
   * Cons CAR and CDR, and store the result in DST.
   */
  VM_DEFINE_OP (76, cons, "cons", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN (scm_inline_cons (thread, x, y));
    }

  /* car dst:12 src:12
   *
   * Place the car of SRC in DST.
   */
  VM_DEFINE_OP (77, car, "car", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (x);
      VM_VALIDATE_PAIR (x, "car");
      RETURN (SCM_CAR (x));
    }

  /* cdr dst:12 src:12
   *
   * Place the cdr of SRC in DST.
   */
  VM_DEFINE_OP (78, cdr, "cdr", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (x);
      VM_VALIDATE_PAIR (x, "cdr");
      RETURN (SCM_CDR (x));
    }

  /* set-car! pair:12 car:12
   *
   * Set the car of DST to SRC.
   */
  VM_DEFINE_OP (79, set_car, "set-car!", OP1 (U8_U12_U12))
    {
      scm_t_uint16 a, b;
      SCM x, y;
      UNPACK_12_12 (op, a, b);
      x = LOCAL_REF (a);
      y = LOCAL_REF (b);
      VM_VALIDATE_PAIR (x, "set-car!");
      SCM_SETCAR (x, y);
      NEXT (1);
    }

  /* set-cdr! pair:12 cdr:12
   *
   * Set the cdr of DST to SRC.
   */
  VM_DEFINE_OP (80, set_cdr, "set-cdr!", OP1 (U8_U12_U12))
    {
      scm_t_uint16 a, b;
      SCM x, y;
      UNPACK_12_12 (op, a, b);
      x = LOCAL_REF (a);
      y = LOCAL_REF (b);
      VM_VALIDATE_PAIR (x, "set-car!");
      SCM_SETCDR (x, y);
      NEXT (1);
    }


  

  /*
   * Numeric operations
   */

  /* add dst:8 a:8 b:8
   *
   * Add A to B, and place the result in DST.
   */
  VM_DEFINE_OP (81, add, "add", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      BINARY_INTEGER_OP (+, scm_sum);
    }

  /* add1 dst:12 src:12
   *
   * Add 1 to the value in SRC, and place the result in DST.
   */
  VM_DEFINE_OP (82, add1, "add1", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (x);

      /* Check for overflow.  We must avoid overflow in the signed
         addition below, even if X is not an inum.  */
      if (SCM_LIKELY ((scm_t_signed_bits) SCM_UNPACK (x) <= INUM_MAX - INUM_STEP))
        {
          SCM result;

          /* Add 1 to the integer without untagging.  */
          result = SCM_PACK ((scm_t_signed_bits) SCM_UNPACK (x) + INUM_STEP);

          if (SCM_LIKELY (SCM_I_INUMP (result)))
            RETURN (result);
        }

      RETURN_EXP (scm_sum (x, SCM_I_MAKINUM (1)));
    }

  /* sub dst:8 a:8 b:8
   *
   * Subtract B from A, and place the result in DST.
   */
  VM_DEFINE_OP (83, sub, "sub", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      BINARY_INTEGER_OP (-, scm_difference);
    }

  /* sub1 dst:12 src:12
   *
   * Subtract 1 from SRC, and place the result in DST.
   */
  VM_DEFINE_OP (84, sub1, "sub1", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (x);

      /* Check for overflow.  We must avoid overflow in the signed
         subtraction below, even if X is not an inum.  */
      if (SCM_LIKELY ((scm_t_signed_bits) SCM_UNPACK (x) >= INUM_MIN + INUM_STEP))
        {
          SCM result;

          /* Substract 1 from the integer without untagging.  */
          result = SCM_PACK ((scm_t_signed_bits) SCM_UNPACK (x) - INUM_STEP);

          if (SCM_LIKELY (SCM_I_INUMP (result)))
            RETURN (result);
        }

      RETURN_EXP (scm_difference (x, SCM_I_MAKINUM (1)));
    }

  /* mul dst:8 a:8 b:8
   *
   * Multiply A and B, and place the result in DST.
   */
  VM_DEFINE_OP (85, mul, "mul", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN_EXP (scm_product (x, y));
    }

  /* div dst:8 a:8 b:8
   *
   * Divide A by B, and place the result in DST.
   */
  VM_DEFINE_OP (86, div, "div", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN_EXP (scm_divide (x, y));
    }

  /* quo dst:8 a:8 b:8
   *
   * Divide A by B, and place the quotient in DST.
   */
  VM_DEFINE_OP (87, quo, "quo", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN_EXP (scm_quotient (x, y));
    }

  /* rem dst:8 a:8 b:8
   *
   * Divide A by B, and place the remainder in DST.
   */
  VM_DEFINE_OP (88, rem, "rem", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN_EXP (scm_remainder (x, y));
    }

  /* mod dst:8 a:8 b:8
   *
   * Place the modulo of A by B in DST.
   */
  VM_DEFINE_OP (89, mod, "mod", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      RETURN_EXP (scm_modulo (x, y));
    }

  /* ash dst:8 a:8 b:8
   *
   * Shift A arithmetically by B bits, and place the result in DST.
   */
  VM_DEFINE_OP (90, ash, "ash", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      if (SCM_I_INUMP (x) && SCM_I_INUMP (y))
        {
          if (SCM_I_INUM (y) < 0)
            /* Right shift, will be a fixnum. */
            RETURN (SCM_I_MAKINUM
                    (SCM_SRS (SCM_I_INUM (x),
                              (-SCM_I_INUM (y) <= SCM_I_FIXNUM_BIT-1)
                              ? -SCM_I_INUM (y) : SCM_I_FIXNUM_BIT-1)));
          else
            /* Left shift. See comments in scm_ash. */
            {
              scm_t_signed_bits nn, bits_to_shift;

              nn = SCM_I_INUM (x);
              bits_to_shift = SCM_I_INUM (y);

              if (bits_to_shift < SCM_I_FIXNUM_BIT-1
                  && ((scm_t_bits)
                      (SCM_SRS (nn, (SCM_I_FIXNUM_BIT-1 - bits_to_shift)) + 1)
                      <= 1))
                RETURN (SCM_I_MAKINUM (nn < 0
                                       ? -(-nn << bits_to_shift)
                                       : (nn << bits_to_shift)));
              /* fall through */
            }
          /* fall through */
        }
      RETURN_EXP (scm_ash (x, y));
    }

  /* logand dst:8 a:8 b:8
   *
   * Place the bitwise AND of A and B into DST.
   */
  VM_DEFINE_OP (91, logand, "logand", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      if (SCM_I_INUMP (x) && SCM_I_INUMP (y))
        /* Compute bitwise AND without untagging */
        RETURN (SCM_PACK (SCM_UNPACK (x) & SCM_UNPACK (y)));
      RETURN_EXP (scm_logand (x, y));
    }

  /* logior dst:8 a:8 b:8
   *
   * Place the bitwise inclusive OR of A with B in DST.
   */
  VM_DEFINE_OP (92, logior, "logior", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      if (SCM_I_INUMP (x) && SCM_I_INUMP (y))
        /* Compute bitwise OR without untagging */
        RETURN (SCM_PACK (SCM_UNPACK (x) | SCM_UNPACK (y)));
      RETURN_EXP (scm_logior (x, y));
    }

  /* logxor dst:8 a:8 b:8
   *
   * Place the bitwise exclusive OR of A with B in DST.
   */
  VM_DEFINE_OP (93, logxor, "logxor", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      ARGS2 (x, y);
      if (SCM_I_INUMP (x) && SCM_I_INUMP (y))
        RETURN (SCM_I_MAKINUM (SCM_I_INUM (x) ^ SCM_I_INUM (y)));
      RETURN_EXP (scm_logxor (x, y));
    }

  /* make-vector dst:8 length:8 init:8
   *
   * Make a vector and write it to DST.  The vector will have space for
   * LENGTH slots.  They will be filled with the value in slot INIT.
   */
  VM_DEFINE_OP (94, make_vector, "make-vector", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, init, length;

      UNPACK_8_8_8 (op, dst, length, init);

      LOCAL_SET (dst, scm_make_vector (LOCAL_REF (length), LOCAL_REF (init)));

      NEXT (1);
    }

  /* make-vector/immediate dst:8 length:8 init:8
   *
   * Make a short vector of known size and write it to DST.  The vector
   * will have space for LENGTH slots, an immediate value.  They will be
   * filled with the value in slot INIT.
   */
  VM_DEFINE_OP (95, make_vector_immediate, "make-vector/immediate", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, init;
      scm_t_int32 length, n;
      SCM val, vector;

      UNPACK_8_8_8 (op, dst, length, init);

      val = LOCAL_REF (init);
      vector = scm_inline_words (thread, scm_tc7_vector | (length << 8),
                                 length + 1);
      for (n = 0; n < length; n++)
        SCM_SIMPLE_VECTOR_SET (vector, n, val);
      LOCAL_SET (dst, vector);
      NEXT (1);
    }

  /* vector-length dst:12 src:12
   *
   * Store the length of the vector in SRC in DST.
   */
  VM_DEFINE_OP (96, vector_length, "vector-length", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (vect);
      VM_ASSERT (SCM_I_IS_VECTOR (vect),
                 vm_error_not_a_vector ("vector-ref", vect));
      RETURN (SCM_I_MAKINUM (SCM_I_VECTOR_LENGTH (vect)));
    }

  /* vector-ref dst:8 src:8 idx:8
   *
   * Fetch the item at position IDX in the vector in SRC, and store it
   * in DST.
   */
  VM_DEFINE_OP (97, vector_ref, "vector-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_signed_bits i = 0;
      ARGS2 (vect, idx);
      VM_ASSERT (SCM_I_IS_VECTOR (vect),
                 vm_error_not_a_vector ("vector-ref", vect));
      VM_ASSERT ((SCM_I_INUMP (idx)
                  && ((i = SCM_I_INUM (idx)) >= 0)
                  && i < SCM_I_VECTOR_LENGTH (vect)),
                 vm_error_out_of_range ("vector-ref", idx));
      RETURN (SCM_I_VECTOR_ELTS (vect)[i]);
    }

  /* vector-ref/immediate dst:8 src:8 idx:8
   *
   * Fill DST with the item IDX elements into the vector at SRC.  Useful
   * for building data types using vectors.
   */
  VM_DEFINE_OP (98, vector_ref_immediate, "vector-ref/immediate", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, src, idx;
      SCM v;
      
      UNPACK_8_8_8 (op, dst, src, idx);
      v = LOCAL_REF (src);
      VM_ASSERT (SCM_I_IS_VECTOR (v),
                 vm_error_not_a_vector ("vector-ref", v));
      VM_ASSERT (idx < SCM_I_VECTOR_LENGTH (v),
                 vm_error_out_of_range ("vector-ref", scm_from_size_t (idx)));
      LOCAL_SET (dst, SCM_I_VECTOR_ELTS (LOCAL_REF (src))[idx]);
      NEXT (1);
    }

  /* vector-set! dst:8 idx:8 src:8
   *
   * Store SRC into the vector DST at index IDX.
   */
  VM_DEFINE_OP (99, vector_set, "vector-set!", OP1 (U8_U8_U8_U8))
    {
      scm_t_uint8 dst, idx_var, src;
      SCM vect, idx, val;
      scm_t_signed_bits i = 0;

      UNPACK_8_8_8 (op, dst, idx_var, src);
      vect = LOCAL_REF (dst);
      idx = LOCAL_REF (idx_var);
      val = LOCAL_REF (src);

      VM_ASSERT (SCM_I_IS_VECTOR (vect),
                 vm_error_not_a_vector ("vector-ref", vect));
      VM_ASSERT ((SCM_I_INUMP (idx)
                  && ((i = SCM_I_INUM (idx)) >= 0)
                  && i < SCM_I_VECTOR_LENGTH (vect)),
                 vm_error_out_of_range ("vector-ref", idx));
      SCM_I_VECTOR_WELTS (vect)[i] = val;
      NEXT (1);
    }

  /* vector-set!/immediate dst:8 idx:8 src:8
   *
   * Store SRC into the vector DST at index IDX.  Here IDX is an
   * immediate value.
   */
  VM_DEFINE_OP (100, vector_set_immediate, "vector-set!/immediate", OP1 (U8_U8_U8_U8))
    {
      scm_t_uint8 dst, idx, src;
      SCM vect, val;

      UNPACK_8_8_8 (op, dst, idx, src);
      vect = LOCAL_REF (dst);
      val = LOCAL_REF (src);

      VM_ASSERT (SCM_I_IS_VECTOR (vect),
                 vm_error_not_a_vector ("vector-ref", vect));
      VM_ASSERT (idx < SCM_I_VECTOR_LENGTH (vect),
                 vm_error_out_of_range ("vector-ref", scm_from_size_t (idx)));
      SCM_I_VECTOR_WELTS (vect)[idx] = val;
      NEXT (1);
    }


  

  /*
   * Structs and GOOPS
   */

  /* struct-vtable dst:12 src:12
   *
   * Store the vtable of SRC into DST.
   */
  VM_DEFINE_OP (101, struct_vtable, "struct-vtable", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (obj);
      VM_VALIDATE_STRUCT (obj, "struct_vtable");
      RETURN (SCM_STRUCT_VTABLE (obj));
    }

  /* allocate-struct/immediate dst:8 vtable:8 nfields:8
   *
   * Allocate a new struct with VTABLE, and place it in DST.  The struct
   * will be constructed with space for NFIELDS fields, which should
   * correspond to the field count of the VTABLE.
   */
  VM_DEFINE_OP (102, allocate_struct_immediate, "allocate-struct/immediate", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, vtable, nfields;
      SCM ret;

      UNPACK_8_8_8 (op, dst, vtable, nfields);

      SYNC_IP ();
      ret = scm_allocate_struct (LOCAL_REF (vtable), SCM_I_MAKINUM (nfields));
      LOCAL_SET (dst, ret);

      NEXT (1);
    }

  /* struct-ref/immediate dst:8 src:8 idx:8
   *
   * Fetch the item at slot IDX in the struct in SRC, and store it
   * in DST.  IDX is an immediate unsigned 8-bit value.
   */
  VM_DEFINE_OP (103, struct_ref_immediate, "struct-ref/immediate", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, src, idx;
      SCM obj;

      UNPACK_8_8_8 (op, dst, src, idx);

      obj = LOCAL_REF (src);

      if (SCM_LIKELY (SCM_STRUCTP (obj)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE)
                      && idx < SCM_STRUCT_DATA_REF (SCM_STRUCT_VTABLE (obj),
                                                    scm_vtable_index_size)))
        RETURN (SCM_STRUCT_SLOT_REF (obj, idx));

      SYNC_IP ();
      RETURN (scm_struct_ref (obj, SCM_I_MAKINUM (idx)));
    }

  /* struct-set!/immediate dst:8 idx:8 src:8
   *
   * Store SRC into the struct DST at slot IDX.  IDX is an immediate
   * unsigned 8-bit value.
   */
  VM_DEFINE_OP (104, struct_set_immediate, "struct-set!/immediate", OP1 (U8_U8_U8_U8))
    {
      scm_t_uint8 dst, idx, src;
      SCM obj, val;

      UNPACK_8_8_8 (op, dst, idx, src);

      obj = LOCAL_REF (dst);
      val = LOCAL_REF (src);

      if (SCM_LIKELY (SCM_STRUCTP (obj)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE_RW)
                      && idx < SCM_STRUCT_DATA_REF (SCM_STRUCT_VTABLE (obj),
                                                    scm_vtable_index_size)))
        {
          SCM_STRUCT_SLOT_SET (obj, idx, val);
          NEXT (1);
        }

      SYNC_IP ();
      scm_struct_set_x (obj, SCM_I_MAKINUM (idx), val);
      NEXT (1);
    }

  /* class-of dst:12 type:12
   *
   * Store the vtable of SRC into DST.
   */
  VM_DEFINE_OP (105, class_of, "class-of", OP1 (U8_U12_U12) | OP_DST)
    {
      ARGS1 (obj);
      if (SCM_INSTANCEP (obj))
        RETURN (SCM_CLASS_OF (obj));
      SYNC_IP ();
      RETURN (scm_class_of (obj));
    }

  

  /*
   * Arrays, packed uniform arrays, and bytevectors.
   */

  /* load-typed-array dst:8 type:8 shape:8 offset:32 len:32
   *
   * Load the contiguous typed array located at OFFSET 32-bit words away
   * from the instruction pointer, and store into DST.  LEN is a byte
   * length.  OFFSET is signed.
   */
  VM_DEFINE_OP (106, load_typed_array, "load-typed-array", OP3 (U8_U8_U8_U8, N32, U32) | OP_DST)
    {
      scm_t_uint8 dst, type, shape;
      scm_t_int32 offset;
      scm_t_uint32 len;

      UNPACK_8_8_8 (op, dst, type, shape);
      offset = ip[1];
      len = ip[2];
      SYNC_IP ();
      LOCAL_SET (dst, scm_from_contiguous_typed_array (LOCAL_REF (type),
                                                       LOCAL_REF (shape),
                                                       ip + offset, len));
      NEXT (3);
    }

  /* make-array dst:8 type:8 fill:8 _:8 bounds:24
   *
   * Make a new array with TYPE, FILL, and BOUNDS, storing it in DST.
   */
  VM_DEFINE_OP (107, make_array, "make-array", OP2 (U8_U8_U8_U8, X8_U24) | OP_DST)
    {
      scm_t_uint8 dst, type, fill, bounds;
      UNPACK_8_8_8 (op, dst, type, fill);
      UNPACK_24 (ip[1], bounds);
      SYNC_IP ();
      LOCAL_SET (dst, scm_make_typed_array (LOCAL_REF (type), LOCAL_REF (fill),
                                            LOCAL_REF (bounds)));
      NEXT (2);
    }

  /* bv-u8-ref dst:8 src:8 idx:8
   * bv-s8-ref dst:8 src:8 idx:8
   * bv-u16-ref dst:8 src:8 idx:8
   * bv-s16-ref dst:8 src:8 idx:8
   * bv-u32-ref dst:8 src:8 idx:8
   * bv-s32-ref dst:8 src:8 idx:8
   * bv-u64-ref dst:8 src:8 idx:8
   * bv-s64-ref dst:8 src:8 idx:8
   * bv-f32-ref dst:8 src:8 idx:8
   * bv-f64-ref dst:8 src:8 idx:8
   *
   * Fetch the item at byte offset IDX in the bytevector SRC, and store
   * it in DST.  All accesses use native endianness.
   */
#define BV_FIXABLE_INT_REF(stem, fn_stem, type, size)			\
  do {									\
    scm_t_signed_bits i;                                                \
    const scm_t_ ## type *int_ptr;					\
    ARGS2 (bv, idx);							\
									\
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-ref");                    \
    i = SCM_I_INUM (idx);                                               \
    int_ptr = (scm_t_ ## type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);	\
									\
    if (SCM_LIKELY (SCM_I_INUMP (idx)					\
                    && (i >= 0)						\
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))		\
                    && (ALIGNED_P (int_ptr, scm_t_ ## type))))		\
      RETURN (SCM_I_MAKINUM (*int_ptr));                                \
    else                                                                \
      {									\
        SYNC_IP ();							\
        RETURN (scm_bytevector_ ## fn_stem ## _ref (bv, idx));		\
      }									\
  } while (0)

#define BV_INT_REF(stem, type, size)					\
  do {									\
    scm_t_signed_bits i;                                                \
    const scm_t_ ## type *int_ptr;					\
    ARGS2 (bv, idx);							\
									\
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-ref");                    \
    i = SCM_I_INUM (idx);                                               \
    int_ptr = (scm_t_ ## type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);	\
									\
    if (SCM_LIKELY (SCM_I_INUMP (idx)					\
                    && (i >= 0)						\
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))		\
                    && (ALIGNED_P (int_ptr, scm_t_ ## type))))		\
      {									\
        scm_t_ ## type x = *int_ptr;					\
        if (SCM_FIXABLE (x))						\
          RETURN (SCM_I_MAKINUM (x));					\
        else								\
          {								\
            SYNC_IP ();                                                 \
            RETURN (scm_from_ ## type (x));				\
          }								\
      }									\
    else                                                                \
      {									\
        SYNC_IP ();							\
        RETURN (scm_bytevector_ ## stem ## _native_ref (bv, idx));	\
      }									\
  } while (0)

#define BV_FLOAT_REF(stem, fn_stem, type, size)				\
  do {									\
    scm_t_signed_bits i;                                                \
    const type *float_ptr;						\
    ARGS2 (bv, idx);							\
									\
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-ref");                    \
    i = SCM_I_INUM (idx);                                               \
    float_ptr = (type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);		\
									\
    SYNC_IP ();                                                         \
    if (SCM_LIKELY (SCM_I_INUMP (idx)					\
                    && (i >= 0)						\
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))		\
                    && (ALIGNED_P (float_ptr, type))))			\
      RETURN (scm_from_double (*float_ptr));				\
    else                                                                \
      RETURN (scm_bytevector_ ## fn_stem ## _native_ref (bv, idx));	\
  } while (0)

  VM_DEFINE_OP (108, bv_u8_ref, "bv-u8-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FIXABLE_INT_REF (u8, u8, uint8, 1);

  VM_DEFINE_OP (109, bv_s8_ref, "bv-s8-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FIXABLE_INT_REF (s8, s8, int8, 1);

  VM_DEFINE_OP (110, bv_u16_ref, "bv-u16-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FIXABLE_INT_REF (u16, u16_native, uint16, 2);

  VM_DEFINE_OP (111, bv_s16_ref, "bv-s16-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FIXABLE_INT_REF (s16, s16_native, int16, 2);

  VM_DEFINE_OP (112, bv_u32_ref, "bv-u32-ref", OP1 (U8_U8_U8_U8) | OP_DST)
#if SIZEOF_VOID_P > 4
    BV_FIXABLE_INT_REF (u32, u32_native, uint32, 4);
#else
    BV_INT_REF (u32, uint32, 4);
#endif

  VM_DEFINE_OP (113, bv_s32_ref, "bv-s32-ref", OP1 (U8_U8_U8_U8) | OP_DST)
#if SIZEOF_VOID_P > 4
    BV_FIXABLE_INT_REF (s32, s32_native, int32, 4);
#else
    BV_INT_REF (s32, int32, 4);
#endif

  VM_DEFINE_OP (114, bv_u64_ref, "bv-u64-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_INT_REF (u64, uint64, 8);

  VM_DEFINE_OP (115, bv_s64_ref, "bv-s64-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_INT_REF (s64, int64, 8);

  VM_DEFINE_OP (116, bv_f32_ref, "bv-f32-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FLOAT_REF (f32, ieee_single, float, 4);

  VM_DEFINE_OP (117, bv_f64_ref, "bv-f64-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    BV_FLOAT_REF (f64, ieee_double, double, 8);

  /* bv-u8-set! dst:8 idx:8 src:8
   * bv-s8-set! dst:8 idx:8 src:8
   * bv-u16-set! dst:8 idx:8 src:8
   * bv-s16-set! dst:8 idx:8 src:8
   * bv-u32-set! dst:8 idx:8 src:8
   * bv-s32-set! dst:8 idx:8 src:8
   * bv-u64-set! dst:8 idx:8 src:8
   * bv-s64-set! dst:8 idx:8 src:8
   * bv-f32-set! dst:8 idx:8 src:8
   * bv-f64-set! dst:8 idx:8 src:8
   *
   * Store SRC into the bytevector DST at byte offset IDX.  Multibyte
   * values are written using native endianness.
   */
#define BV_FIXABLE_INT_SET(stem, fn_stem, type, min, max, size)		\
  do {									\
    scm_t_uint8 dst, idx, src;                                          \
    scm_t_signed_bits i, j = 0;                                         \
    SCM bv, scm_idx, val;                                               \
    scm_t_ ## type *int_ptr;						\
									\
    UNPACK_8_8_8 (op, dst, idx, src);                                   \
    bv = LOCAL_REF (dst);                                               \
    scm_idx = LOCAL_REF (idx);                                          \
    val = LOCAL_REF (src);                                              \
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-set!");                   \
    i = SCM_I_INUM (scm_idx);                                           \
    int_ptr = (scm_t_ ## type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);	\
									\
    if (SCM_LIKELY (SCM_I_INUMP (scm_idx)                               \
                    && (i >= 0)                                         \
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))         \
                    && (ALIGNED_P (int_ptr, scm_t_ ## type))		\
                    && (SCM_I_INUMP (val))				\
                    && ((j = SCM_I_INUM (val)) >= min)                  \
                    && (j <= max)))					\
      *int_ptr = (scm_t_ ## type) j;					\
    else                                                                \
      {                                                                 \
        SYNC_IP ();                                                     \
        scm_bytevector_ ## fn_stem ## _set_x (bv, scm_idx, val);        \
      }                                                                 \
    NEXT (1);                                                           \
  } while (0)

#define BV_INT_SET(stem, type, size)					\
  do {									\
    scm_t_uint8 dst, idx, src;                                          \
    scm_t_signed_bits i;                                                \
    SCM bv, scm_idx, val;                                               \
    scm_t_ ## type *int_ptr;						\
									\
    UNPACK_8_8_8 (op, dst, idx, src);                                   \
    bv = LOCAL_REF (dst);                                               \
    scm_idx = LOCAL_REF (idx);                                          \
    val = LOCAL_REF (src);                                              \
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-set!");                   \
    i = SCM_I_INUM (scm_idx);                                           \
    int_ptr = (scm_t_ ## type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);	\
									\
    if (SCM_LIKELY (SCM_I_INUMP (scm_idx)                               \
                    && (i >= 0)                                         \
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))         \
                    && (ALIGNED_P (int_ptr, scm_t_ ## type))))          \
      *int_ptr = scm_to_ ## type (val);                                 \
    else                                                                \
      {                                                                 \
        SYNC_IP ();                                                     \
        scm_bytevector_ ## stem ## _native_set_x (bv, scm_idx, val);    \
      }                                                                 \
    NEXT (1);                                                           \
  } while (0)

#define BV_FLOAT_SET(stem, fn_stem, type, size)                         \
  do {                                                                  \
    scm_t_uint8 dst, idx, src;                                          \
    scm_t_signed_bits i;                                                \
    SCM bv, scm_idx, val;                                               \
    type *float_ptr;                                                    \
									\
    UNPACK_8_8_8 (op, dst, idx, src);                                   \
    bv = LOCAL_REF (dst);                                               \
    scm_idx = LOCAL_REF (idx);                                          \
    val = LOCAL_REF (src);                                              \
    VM_VALIDATE_BYTEVECTOR (bv, "bv-" #stem "-set!");                   \
    i = SCM_I_INUM (scm_idx);                                           \
    float_ptr = (type *) (SCM_BYTEVECTOR_CONTENTS (bv) + i);            \
                                                                        \
    if (SCM_LIKELY (SCM_I_INUMP (scm_idx)                               \
                    && (i >= 0)                                         \
                    && (i + size <= SCM_BYTEVECTOR_LENGTH (bv))         \
                    && (ALIGNED_P (float_ptr, type))))                  \
      *float_ptr = scm_to_double (val);                                 \
    else                                                                \
      {                                                                 \
        SYNC_IP ();                                                     \
        scm_bytevector_ ## fn_stem ## _native_set_x (bv, scm_idx, val); \
      }                                                                 \
    NEXT (1);                                                           \
  } while (0)

  VM_DEFINE_OP (118, bv_u8_set, "bv-u8-set!", OP1 (U8_U8_U8_U8))
    BV_FIXABLE_INT_SET (u8, u8, uint8, 0, SCM_T_UINT8_MAX, 1);

  VM_DEFINE_OP (119, bv_s8_set, "bv-s8-set!", OP1 (U8_U8_U8_U8))
    BV_FIXABLE_INT_SET (s8, s8, int8, SCM_T_INT8_MIN, SCM_T_INT8_MAX, 1);

  VM_DEFINE_OP (120, bv_u16_set, "bv-u16-set!", OP1 (U8_U8_U8_U8))
    BV_FIXABLE_INT_SET (u16, u16_native, uint16, 0, SCM_T_UINT16_MAX, 2);

  VM_DEFINE_OP (121, bv_s16_set, "bv-s16-set!", OP1 (U8_U8_U8_U8))
    BV_FIXABLE_INT_SET (s16, s16_native, int16, SCM_T_INT16_MIN, SCM_T_INT16_MAX, 2);

  VM_DEFINE_OP (122, bv_u32_set, "bv-u32-set!", OP1 (U8_U8_U8_U8))
#if SIZEOF_VOID_P > 4
    BV_FIXABLE_INT_SET (u32, u32_native, uint32, 0, SCM_T_UINT32_MAX, 4);
#else
    BV_INT_SET (u32, uint32, 4);
#endif

  VM_DEFINE_OP (123, bv_s32_set, "bv-s32-set!", OP1 (U8_U8_U8_U8))
#if SIZEOF_VOID_P > 4
    BV_FIXABLE_INT_SET (s32, s32_native, int32, SCM_T_INT32_MIN, SCM_T_INT32_MAX, 4);
#else
    BV_INT_SET (s32, int32, 4);
#endif

  VM_DEFINE_OP (124, bv_u64_set, "bv-u64-set!", OP1 (U8_U8_U8_U8))
    BV_INT_SET (u64, uint64, 8);

  VM_DEFINE_OP (125, bv_s64_set, "bv-s64-set!", OP1 (U8_U8_U8_U8))
    BV_INT_SET (s64, int64, 8);

  VM_DEFINE_OP (126, bv_f32_set, "bv-f32-set!", OP1 (U8_U8_U8_U8))
    BV_FLOAT_SET (f32, ieee_single, float, 4);

  VM_DEFINE_OP (127, bv_f64_set, "bv-f64-set!", OP1 (U8_U8_U8_U8))
    BV_FLOAT_SET (f64, ieee_double, double, 8);

  /* br-if-logtest a:12 b:12 invert:1 _:7 offset:24
   *
   * If the exact integer in A has any bits in common with the exact
   * integer in B, add OFFSET, a signed 24-bit number, to the current
   * instruction pointer.
   */
  VM_DEFINE_OP (128, br_if_logtest, "br-if-logtest", OP2 (U8_U12_U12, B1_X7_L24))
    {
      BR_BINARY (x, y,
                 ((SCM_I_INUMP (x) && SCM_I_INUMP (y))
                  ? (SCM_UNPACK (x) & SCM_UNPACK (y) & ~scm_tc2_int)
                  : scm_is_true (scm_logtest (x, y))));
    }

  /* FIXME: Move above */

  /* allocate-struct dst:8 vtable:8 nfields:8
   *
   * Allocate a new struct with VTABLE, and place it in DST.  The struct
   * will be constructed with space for NFIELDS fields, which should
   * correspond to the field count of the VTABLE.
   */
  VM_DEFINE_OP (129, allocate_struct, "allocate-struct", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, vtable, nfields;
      SCM ret;

      UNPACK_8_8_8 (op, dst, vtable, nfields);

      SYNC_IP ();
      ret = scm_allocate_struct (LOCAL_REF (vtable), LOCAL_REF (nfields));
      LOCAL_SET (dst, ret);

      NEXT (1);
    }

  /* struct-ref dst:8 src:8 idx:8
   *
   * Fetch the item at slot IDX in the struct in SRC, and store it
   * in DST.
   */
  VM_DEFINE_OP (130, struct_ref, "struct-ref", OP1 (U8_U8_U8_U8) | OP_DST)
    {
      scm_t_uint8 dst, src, idx;
      SCM obj;
      SCM index;

      UNPACK_8_8_8 (op, dst, src, idx);

      obj = LOCAL_REF (src);
      index = LOCAL_REF (idx);

      if (SCM_LIKELY (SCM_STRUCTP (obj)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE)
                      && SCM_I_INUMP (index)
                      && SCM_I_INUM (index) >= 0
                      && SCM_I_INUM (index) < (SCM_STRUCT_DATA_REF
                                               (SCM_STRUCT_VTABLE (obj),
                                                scm_vtable_index_size))))
        RETURN (SCM_STRUCT_SLOT_REF (obj, SCM_I_INUM (index)));

      SYNC_IP ();
      RETURN (scm_struct_ref (obj, index));
    }

  /* struct-set! dst:8 idx:8 src:8
   *
   * Store SRC into the struct DST at slot IDX.
   */
  VM_DEFINE_OP (131, struct_set, "struct-set!", OP1 (U8_U8_U8_U8))
    {
      scm_t_uint8 dst, idx, src;
      SCM obj, val, index;

      UNPACK_8_8_8 (op, dst, idx, src);

      obj = LOCAL_REF (dst);
      val = LOCAL_REF (src);
      index = LOCAL_REF (idx);

      if (SCM_LIKELY (SCM_STRUCTP (obj)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE)
                      && SCM_STRUCT_VTABLE_FLAG_IS_SET (obj,
                                                        SCM_VTABLE_FLAG_SIMPLE_RW)
                      && SCM_I_INUMP (index)
                      && SCM_I_INUM (index) >= 0
                      && SCM_I_INUM (index) < (SCM_STRUCT_DATA_REF
                                               (SCM_STRUCT_VTABLE (obj),
                                                scm_vtable_index_size))))
        {
          SCM_STRUCT_SLOT_SET (obj, SCM_I_INUM (index), val);
          NEXT (1);
        }

      SYNC_IP ();
      scm_struct_set_x (obj, index, val);
      NEXT (1);
    }

  VM_DEFINE_OP (132, unused_132, NULL, NOP)
  VM_DEFINE_OP (133, unused_133, NULL, NOP)
  VM_DEFINE_OP (134, unused_134, NULL, NOP)
  VM_DEFINE_OP (135, unused_135, NULL, NOP)
  VM_DEFINE_OP (136, unused_136, NULL, NOP)
  VM_DEFINE_OP (137, unused_137, NULL, NOP)
  VM_DEFINE_OP (138, unused_138, NULL, NOP)
  VM_DEFINE_OP (139, unused_139, NULL, NOP)
  VM_DEFINE_OP (140, unused_140, NULL, NOP)
  VM_DEFINE_OP (141, unused_141, NULL, NOP)
  VM_DEFINE_OP (142, unused_142, NULL, NOP)
  VM_DEFINE_OP (143, unused_143, NULL, NOP)
  VM_DEFINE_OP (144, unused_144, NULL, NOP)
  VM_DEFINE_OP (145, unused_145, NULL, NOP)
  VM_DEFINE_OP (146, unused_146, NULL, NOP)
  VM_DEFINE_OP (147, unused_147, NULL, NOP)
  VM_DEFINE_OP (148, unused_148, NULL, NOP)
  VM_DEFINE_OP (149, unused_149, NULL, NOP)
  VM_DEFINE_OP (150, unused_150, NULL, NOP)
  VM_DEFINE_OP (151, unused_151, NULL, NOP)
  VM_DEFINE_OP (152, unused_152, NULL, NOP)
  VM_DEFINE_OP (153, unused_153, NULL, NOP)
  VM_DEFINE_OP (154, unused_154, NULL, NOP)
  VM_DEFINE_OP (155, unused_155, NULL, NOP)
  VM_DEFINE_OP (156, unused_156, NULL, NOP)
  VM_DEFINE_OP (157, unused_157, NULL, NOP)
  VM_DEFINE_OP (158, unused_158, NULL, NOP)
  VM_DEFINE_OP (159, unused_159, NULL, NOP)
  VM_DEFINE_OP (160, unused_160, NULL, NOP)
  VM_DEFINE_OP (161, unused_161, NULL, NOP)
  VM_DEFINE_OP (162, unused_162, NULL, NOP)
  VM_DEFINE_OP (163, unused_163, NULL, NOP)
  VM_DEFINE_OP (164, unused_164, NULL, NOP)
  VM_DEFINE_OP (165, unused_165, NULL, NOP)
  VM_DEFINE_OP (166, unused_166, NULL, NOP)
  VM_DEFINE_OP (167, unused_167, NULL, NOP)
  VM_DEFINE_OP (168, unused_168, NULL, NOP)
  VM_DEFINE_OP (169, unused_169, NULL, NOP)
  VM_DEFINE_OP (170, unused_170, NULL, NOP)
  VM_DEFINE_OP (171, unused_171, NULL, NOP)
  VM_DEFINE_OP (172, unused_172, NULL, NOP)
  VM_DEFINE_OP (173, unused_173, NULL, NOP)
  VM_DEFINE_OP (174, unused_174, NULL, NOP)
  VM_DEFINE_OP (175, unused_175, NULL, NOP)
  VM_DEFINE_OP (176, unused_176, NULL, NOP)
  VM_DEFINE_OP (177, unused_177, NULL, NOP)
  VM_DEFINE_OP (178, unused_178, NULL, NOP)
  VM_DEFINE_OP (179, unused_179, NULL, NOP)
  VM_DEFINE_OP (180, unused_180, NULL, NOP)
  VM_DEFINE_OP (181, unused_181, NULL, NOP)
  VM_DEFINE_OP (182, unused_182, NULL, NOP)
  VM_DEFINE_OP (183, unused_183, NULL, NOP)
  VM_DEFINE_OP (184, unused_184, NULL, NOP)
  VM_DEFINE_OP (185, unused_185, NULL, NOP)
  VM_DEFINE_OP (186, unused_186, NULL, NOP)
  VM_DEFINE_OP (187, unused_187, NULL, NOP)
  VM_DEFINE_OP (188, unused_188, NULL, NOP)
  VM_DEFINE_OP (189, unused_189, NULL, NOP)
  VM_DEFINE_OP (190, unused_190, NULL, NOP)
  VM_DEFINE_OP (191, unused_191, NULL, NOP)
  VM_DEFINE_OP (192, unused_192, NULL, NOP)
  VM_DEFINE_OP (193, unused_193, NULL, NOP)
  VM_DEFINE_OP (194, unused_194, NULL, NOP)
  VM_DEFINE_OP (195, unused_195, NULL, NOP)
  VM_DEFINE_OP (196, unused_196, NULL, NOP)
  VM_DEFINE_OP (197, unused_197, NULL, NOP)
  VM_DEFINE_OP (198, unused_198, NULL, NOP)
  VM_DEFINE_OP (199, unused_199, NULL, NOP)
  VM_DEFINE_OP (200, unused_200, NULL, NOP)
  VM_DEFINE_OP (201, unused_201, NULL, NOP)
  VM_DEFINE_OP (202, unused_202, NULL, NOP)
  VM_DEFINE_OP (203, unused_203, NULL, NOP)
  VM_DEFINE_OP (204, unused_204, NULL, NOP)
  VM_DEFINE_OP (205, unused_205, NULL, NOP)
  VM_DEFINE_OP (206, unused_206, NULL, NOP)
  VM_DEFINE_OP (207, unused_207, NULL, NOP)
  VM_DEFINE_OP (208, unused_208, NULL, NOP)
  VM_DEFINE_OP (209, unused_209, NULL, NOP)
  VM_DEFINE_OP (210, unused_210, NULL, NOP)
  VM_DEFINE_OP (211, unused_211, NULL, NOP)
  VM_DEFINE_OP (212, unused_212, NULL, NOP)
  VM_DEFINE_OP (213, unused_213, NULL, NOP)
  VM_DEFINE_OP (214, unused_214, NULL, NOP)
  VM_DEFINE_OP (215, unused_215, NULL, NOP)
  VM_DEFINE_OP (216, unused_216, NULL, NOP)
  VM_DEFINE_OP (217, unused_217, NULL, NOP)
  VM_DEFINE_OP (218, unused_218, NULL, NOP)
  VM_DEFINE_OP (219, unused_219, NULL, NOP)
  VM_DEFINE_OP (220, unused_220, NULL, NOP)
  VM_DEFINE_OP (221, unused_221, NULL, NOP)
  VM_DEFINE_OP (222, unused_222, NULL, NOP)
  VM_DEFINE_OP (223, unused_223, NULL, NOP)
  VM_DEFINE_OP (224, unused_224, NULL, NOP)
  VM_DEFINE_OP (225, unused_225, NULL, NOP)
  VM_DEFINE_OP (226, unused_226, NULL, NOP)
  VM_DEFINE_OP (227, unused_227, NULL, NOP)
  VM_DEFINE_OP (228, unused_228, NULL, NOP)
  VM_DEFINE_OP (229, unused_229, NULL, NOP)
  VM_DEFINE_OP (230, unused_230, NULL, NOP)
  VM_DEFINE_OP (231, unused_231, NULL, NOP)
  VM_DEFINE_OP (232, unused_232, NULL, NOP)
  VM_DEFINE_OP (233, unused_233, NULL, NOP)
  VM_DEFINE_OP (234, unused_234, NULL, NOP)
  VM_DEFINE_OP (235, unused_235, NULL, NOP)
  VM_DEFINE_OP (236, unused_236, NULL, NOP)
  VM_DEFINE_OP (237, unused_237, NULL, NOP)
  VM_DEFINE_OP (238, unused_238, NULL, NOP)
  VM_DEFINE_OP (239, unused_239, NULL, NOP)
  VM_DEFINE_OP (240, unused_240, NULL, NOP)
  VM_DEFINE_OP (241, unused_241, NULL, NOP)
  VM_DEFINE_OP (242, unused_242, NULL, NOP)
  VM_DEFINE_OP (243, unused_243, NULL, NOP)
  VM_DEFINE_OP (244, unused_244, NULL, NOP)
  VM_DEFINE_OP (245, unused_245, NULL, NOP)
  VM_DEFINE_OP (246, unused_246, NULL, NOP)
  VM_DEFINE_OP (247, unused_247, NULL, NOP)
  VM_DEFINE_OP (248, unused_248, NULL, NOP)
  VM_DEFINE_OP (249, unused_249, NULL, NOP)
  VM_DEFINE_OP (250, unused_250, NULL, NOP)
  VM_DEFINE_OP (251, unused_251, NULL, NOP)
  VM_DEFINE_OP (252, unused_252, NULL, NOP)
  VM_DEFINE_OP (253, unused_253, NULL, NOP)
  VM_DEFINE_OP (254, unused_254, NULL, NOP)
  VM_DEFINE_OP (255, unused_255, NULL, NOP)
    {
      vm_error_bad_instruction (op);
      abort (); /* never reached */
    }

  END_DISPATCH_SWITCH;
}


#undef ABORT_CONTINUATION_HOOK
#undef ALIGNED_P
#undef APPLY_HOOK
#undef ARGS1
#undef ARGS2
#undef BEGIN_DISPATCH_SWITCH
#undef BINARY_INTEGER_OP
#undef BR_ARITHMETIC
#undef BR_BINARY
#undef BR_NARGS
#undef BR_UNARY
#undef BV_FIXABLE_INT_REF
#undef BV_FIXABLE_INT_SET
#undef BV_FLOAT_REF
#undef BV_FLOAT_SET
#undef BV_INT_REF
#undef BV_INT_SET
#undef CACHE_REGISTER
#undef END_DISPATCH_SWITCH
#undef FREE_VARIABLE_REF
#undef INIT
#undef INUM_MAX
#undef INUM_MIN
#undef LOCAL_REF
#undef LOCAL_SET
#undef NEXT
#undef NEXT_HOOK
#undef NEXT_JUMP
#undef POP_CONTINUATION_HOOK
#undef PUSH_CONTINUATION_HOOK
#undef RETURN
#undef RETURN_ONE_VALUE
#undef RETURN_VALUE_LIST
#undef RUN_HOOK
#undef RUN_HOOK0
#undef RUN_HOOK1
#undef SYNC_IP
#undef UNPACK_8_8_8
#undef UNPACK_8_16
#undef UNPACK_16_8
#undef UNPACK_12_12
#undef UNPACK_24
#undef VARIABLE_BOUNDP
#undef VARIABLE_REF
#undef VARIABLE_SET
#undef VM_CHECK_FREE_VARIABLE
#undef VM_CHECK_OBJECT
#undef VM_CHECK_UNDERFLOW
#undef VM_DEFINE_OP
#undef VM_INSTRUCTION_TO_LABEL
#undef VM_USE_HOOKS
#undef VM_VALIDATE_BYTEVECTOR
#undef VM_VALIDATE_PAIR
#undef VM_VALIDATE_STRUCT

/*
(defun renumber-ops ()
  "start from top of buffer and renumber 'VM_DEFINE_FOO (\n' sequences"
  (interactive "")
  (save-excursion
    (let ((counter -1)) (goto-char (point-min))
      (while (re-search-forward "^ *VM_DEFINE_[^ ]+ (\\([^,]+\\)," (point-max) t)
        (replace-match
         (number-to-string (setq counter (1+ counter)))
          t t nil 1)))))
(renumber-ops)
*/
/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
