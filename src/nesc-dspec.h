/* This file is part of the nesC compiler.
   Copyright (C) 2002 Intel Corporation

The attached "nesC" software is provided to you under the terms and
conditions of the GNU General Public License Version 2 as published by the
Free Software Foundation.

nesC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nesC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#ifndef NESC_DSPEC_H
#define NESC_DSPEC_H

#include "ND_types.h"
#include "ND_list_nd_arg.h"
#include "ND_defs.h"

nd_option nd_parse(const char *what);
/* Effects: parses -fnesc-dump option argument 'what', 
   Return: the corresponding dump tree (see nesc-dspec.def)
*/

const char *nd_tokenval(nd_arg arg);
/* Requires: nd_arg is an nd_token
   Returns: the token's string value
*/

#endif
