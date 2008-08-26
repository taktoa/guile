/* classes: h_files */

#ifndef SCM_VPORTS_H
#define SCM_VPORTS_H

/* Copyright (C) 1995,1996,2000, 2006, 2008 Free Software Foundation, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */



#include "libguile/__scm.h"



SCM_API SCM scm_make_soft_port (SCM pv, SCM modes);
SCM_INTERNAL void scm_init_vports (void);

#endif  /* SCM_VPORTS_H */

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
