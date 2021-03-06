/* Copyright (C) 2001, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.
 * * 
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

#ifndef _SCM_FRAMES_H_
#define _SCM_FRAMES_H_

#include <libguile.h>
#include "programs.h"


/* Stack frames
   ------------

   It's a little confusing, but there are two representations of frames
   in this file: frame pointers, and Scheme objects wrapping those frame
   pointers.  The former uses the SCM_FRAME macro prefix, the latter
   SCM_VM_FRAME prefix.

   The confusing thing is that only Scheme frame objects have functions
   that use them, and they use the lower-case scm_frame prefix.


   Stack frame layout
   ------------------

   /------------------\
   | Local N-1        | <- sp
   | ...              |
   | Local 1          |
   | Local 0          | <- fp = SCM_FRAME_LOCALS_ADDRESS (fp)
   +==================+
   | Return address   |
   | Dynamic link     | <- fp - 2 = SCM_FRAME_LOWER_ADDRESS (fp)
   +==================+
   |                  | <- fp - 3 = SCM_FRAME_PREVIOUS_SP (fp)

   The calling convention is that a caller prepares a stack frame
   consisting of the saved FP and the return address, followed by the
   procedure and then the arguments to the call, in order.  Thus in the
   beginning of a call, the procedure being called is in slot 0, the
   first argument is in slot 1, and the SP points to the last argument.
   The number of arguments, including the procedure, is thus SP - FP +
   1.

   After ensuring that the correct number of arguments have been passed,
   a function will set the stack pointer to point to the last local
   slot.  This lets a function allocate the temporary space that it
   needs once in the beginning of the call, instead of pushing and
   popping the stack pointer during the call's extent.

   When a program returns, it returns its values in the slots starting
   from local 1, as if the values were arguments to a tail call.  We
   start from 1 instead of 0 for the convenience of the "values" builtin
   function, which can just leave its arguments in place.

   The callee resets the stack pointer to point to the last value.  In
   this way the caller knows how many values there are: it's the number
   of words between the stack pointer and the slot at which the caller
   placed the procedure.

   After checking that the number of values returned is appropriate, the
   caller shuffles the values around (if needed), and resets the stack
   pointer back to its original value from before the call.  */




/* This structure maps to the contents of a VM stack frame.  It can
   alias a frame directly.  */
struct scm_vm_frame
{
  SCM *dynamic_link;
  scm_t_uint32 *return_address;
  SCM locals[1]; /* Variable-length */
};

#define SCM_FRAME_LOWER_ADDRESS(fp)	(((SCM *) (fp)) - 2)
#define SCM_FRAME_STRUCT(fp)				\
  ((struct scm_vm_frame *) SCM_FRAME_LOWER_ADDRESS (fp))
#define SCM_FRAME_LOCALS_ADDRESS(fp)	(SCM_FRAME_STRUCT (fp)->locals)

#define SCM_FRAME_PREVIOUS_SP(fp)	(((SCM *) (fp)) - 3)

#define SCM_FRAME_RETURN_ADDRESS(fp)            \
  (SCM_FRAME_STRUCT (fp)->return_address)
#define SCM_FRAME_SET_RETURN_ADDRESS(fp, ra)    \
  SCM_FRAME_STRUCT (fp)->return_address = (ra)
#define SCM_FRAME_DYNAMIC_LINK(fp)              \
  (SCM_FRAME_STRUCT (fp)->dynamic_link)
#define SCM_FRAME_SET_DYNAMIC_LINK(fp, dl)      \
  SCM_FRAME_DYNAMIC_LINK (fp) = (dl)
#define SCM_FRAME_LOCAL(fp,i)                   \
  (SCM_FRAME_STRUCT (fp)->locals[i])

#define SCM_FRAME_NUM_LOCALS(fp, sp)            \
  ((sp) + 1 - &SCM_FRAME_LOCAL (fp, 0))


/*
 * Heap frames
 */

#ifdef BUILDING_LIBGUILE

struct scm_frame 
{
  void *stack_holder;
  scm_t_ptrdiff fp_offset;
  scm_t_ptrdiff sp_offset;
  scm_t_uint32 *ip;
};

enum scm_vm_frame_kind
  {
    SCM_VM_FRAME_KIND_VM,
    SCM_VM_FRAME_KIND_CONT
  };

#define SCM_VM_FRAME_P(x)	(SCM_HAS_TYP7 (x, scm_tc7_frame))
#define SCM_VM_FRAME_KIND(x)	((enum scm_vm_frame_kind) (SCM_CELL_WORD_0 (x) >> 8))
#define SCM_VM_FRAME_DATA(x)	((struct scm_frame *)SCM_CELL_WORD_1 (x))
#define SCM_VM_FRAME_STACK_HOLDER(f)	SCM_VM_FRAME_DATA (f)->stack_holder
#define SCM_VM_FRAME_FP_OFFSET(f)	SCM_VM_FRAME_DATA (f)->fp_offset
#define SCM_VM_FRAME_SP_OFFSET(f)	SCM_VM_FRAME_DATA (f)->sp_offset
#define SCM_VM_FRAME_FP(f)	(SCM_VM_FRAME_FP_OFFSET (f) + scm_i_frame_stack_base (f))
#define SCM_VM_FRAME_SP(f)	(SCM_VM_FRAME_SP_OFFSET (f) + scm_i_frame_stack_base (f))
#define SCM_VM_FRAME_IP(f)	SCM_VM_FRAME_DATA (f)->ip
#define SCM_VM_FRAME_OFFSET(f)	scm_i_frame_offset (f)
#define SCM_VALIDATE_VM_FRAME(p,x)	SCM_MAKE_VALIDATE (p, x, VM_FRAME_P)

SCM_INTERNAL SCM* scm_i_frame_stack_base (SCM frame);
SCM_INTERNAL scm_t_ptrdiff scm_i_frame_offset (SCM frame);

/* See notes in frames.c before using this.  */
SCM_INTERNAL SCM scm_c_frame_closure (enum scm_vm_frame_kind kind,
                                      const struct scm_frame *frame);

SCM_INTERNAL SCM scm_c_make_frame (enum scm_vm_frame_kind kind,
                                   const struct scm_frame *frame);

SCM_INTERNAL int scm_c_frame_previous (enum scm_vm_frame_kind kind,
                                       struct scm_frame *frame);

#endif

SCM_API SCM scm_frame_p (SCM obj);
SCM_API SCM scm_frame_procedure (SCM frame);
SCM_API SCM scm_frame_call_representation (SCM frame);
SCM_API SCM scm_frame_arguments (SCM frame);
SCM_API SCM scm_frame_source (SCM frame);
SCM_API SCM scm_frame_num_locals (SCM frame);
SCM_API SCM scm_frame_local_ref (SCM frame, SCM index);
SCM_API SCM scm_frame_local_set_x (SCM frame, SCM index, SCM val);
SCM_API SCM scm_frame_address (SCM frame);
SCM_API SCM scm_frame_stack_pointer (SCM frame);
SCM_API SCM scm_frame_instruction_pointer (SCM frame);
SCM_API SCM scm_frame_return_address (SCM frame);
SCM_API SCM scm_frame_dynamic_link (SCM frame);
SCM_API SCM scm_frame_previous (SCM frame);

SCM_INTERNAL void scm_i_frame_print (SCM frame, SCM port,
                                     scm_print_state *pstate);
SCM_INTERNAL void scm_init_frames (void);

#endif /* _SCM_FRAMES_H_ */

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
