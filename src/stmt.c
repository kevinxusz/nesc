/* This file is part of the nesC compiler.

This file is derived from RC and the GNU C Compiler. It is thus
   Copyright (C) 1987, 88, 89, 92-7, 1998 Free Software Foundation, Inc.
   Copyright (C) 2000-2001 The Regents of the University of California.
Changes for nesC are
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
Boston, MA 02111-1307, USA. */

#include "parser.h"
#include "stmt.h"
#include "semantics.h"
#include "expr.h"
#include "c-parse.h"

void check_condition(const char *context, expression e)
{
  type etype = default_conversion(e);

  if (etype != error_type && !type_scalar(etype))
    error("%s condition must be scalar", context);
}

void check_switch(expression e)
{
  if (e->type != error_type && !type_integer(e->type))
    error("switch quantity not an integer");
}

static type current_return_type(void)
{
  type fntype = current.function_decl->ddecl->type;

  if (type_generic(fntype))
    fntype = type_function_return_type(fntype);

  if (type_volatile(fntype))
    warning("function declared `noreturn' has a `return' statement");

  return type_function_return_type(fntype);
}

void check_void_return(void)
{
  type ret = current_return_type();

  if (warn_return_type && ret != error_type && !type_void(ret))
    warning("`return' with no value, in function returning non-void");
}

void check_return(expression e)
{
  type ret = current_return_type();
 
  if (type_void(ret))
    {
      if (pedantic || !type_void(e->type))
	warning("`return' with a value, in function returning void");
    }
  else
    {
      check_assignment(ret, default_conversion_for_assignment(e), e, "return", NULL, NULL, 0);
      /* XXX: Missing warning about returning address of local var */
    }
}

void check_computed_goto(expression e)
{
  /* Rather weak check (same as gcc) */
  check_conversion(ptr_void_type, e->type);
}

label_declaration new_label_declaration(region r, const char *name, id_label firstuse)
{
  label_declaration ldecl = ralloc(r, struct label_declaration);

  ldecl->name = name;
  ldecl->explicitly_declared = FALSE;
  ldecl->firstuse = firstuse;
  ldecl->definition = NULL;
  ldecl->containing_function = current.function_decl;

  return ldecl;
}

void lookup_label(id_label label)
{
  label_declaration ldecl;

  if (!current.function_decl)
    {
      error ("label %s referenced outside of any function",
	     label->cstring.data);
      /* A dummy decl to make everybody happy. */
      label->ldecl = new_label_declaration(parse_region, label->cstring.data, label);
      return;
    }

  ldecl = env_lookup(current.function_decl->scoped_labels, label->cstring.data,
		     FALSE);

  /* Only explicitly declared labels are visible in nested functions */
  if (ldecl && !ldecl->explicitly_declared &&
      ldecl->containing_function != current.function_decl)
    ldecl = NULL;

  if (!ldecl) /* new label */
    {
      ldecl = new_label_declaration(parse_region, label->cstring.data, label);
      env_add(current.function_decl->base_labels, label->cstring.data, ldecl);
    }

  label->ldecl = ldecl;
}

void use_label(id_label label)
{
  lookup_label(label);
  label->ldecl->used = TRUE;
}

static void duplicate_label_error(id_label label)
{
  error("duplicate label declaration `%s'", label->cstring.data);
  error_with_location(label->ldecl->definition ?
		      label->ldecl->definition->location :
		      label->ldecl->firstuse->location,
		      "this is a previous declaration");
}

void define_label(id_label label)
{
  lookup_label(label);
  if (label->ldecl->definition)
    duplicate_label_error(label);
  else
    label->ldecl->definition = label;
}

void declare_label(id_label label)
{
  label_declaration ldecl =
    env_lookup(current.function_decl->scoped_labels, label->cstring.data, TRUE);

  if (ldecl)
    {
      label->ldecl = ldecl;
      duplicate_label_error(label);
    }
  else
    {
      ldecl = new_label_declaration(parse_region, label->cstring.data, label);
      env_add(current.function_decl->scoped_labels, label->cstring.data, ldecl);
    }
  ldecl->explicitly_declared = TRUE;
}

void check_labels(void)
{
  env_scanner scan_labels;
  void *ld;
  const char *lname;

  env_scan(current.function_decl->scoped_labels, &scan_labels);
  while (env_next(&scan_labels, &lname, &ld))
    {
      label_declaration ldecl = ld;

      if (!ldecl->definition)
	error_with_location(ldecl->firstuse->location,
			    "label `%s' used but not defined", lname);
      else if (!ldecl->used && warn_unused)
	warning_with_location(ldecl->firstuse->location,
			      "label `%s' defined but not used", lname);
    }
}

void push_loop(statement loop_statement)
{
  loop_statement->parent_loop = current.function_decl->current_loop;
  current.function_decl->current_loop = loop_statement;
}

void pop_loop(void)
{
  current.function_decl->current_loop =
    current.function_decl->current_loop->parent_loop;
}

static statement containing_switch(label l)
{
  statement sw = current.function_decl->current_loop;

  /* Find a switch */
  while (sw && !is_switch_stmt(sw))
    sw = sw->parent_loop;

  if (sw)
    {
      /* Chain the label */
      /* XXX: n^2 algo */
      label *last = &CAST(switch_stmt, sw)->next_label;
      bool lisdefault = is_default_label(l);

      /* XXX: no duplicate case check */
      while (*last)
	{
	  if (is_default_label(*last) && lisdefault)
	    {
	      error("multiple default labels in one switch");
	      error_with_location((*last)->location,
				  "this is the first default label");
	      lisdefault = FALSE; /* Only one error */
	    }
	  last = &(*last)->next_label;
	}

      *last = l;
    }

  return sw;
}

static void check_case_value(expression e)
{
  if (!e->cst || !(e->type == error_type || type_integer(e->type)))
    error_with_location(e->location,
			"case label does not reduce to an integer constant");
}

void check_case(label label0)
{
  statement sw = containing_switch(label0);
  case_label label = CAST(case_label, label0);

  if (!sw)
     error("case label not within a switch statement");

  check_case_value(label->arg1);
  if (label->arg2) 
    check_case_value(label->arg2);
  /* XXX: no range check (compared to switched type), no empty range check */
  /* XXX: no check for unreachable code */
}

void check_default(label default_label)
{
  statement sw = containing_switch(default_label);

  if (!sw)
    error("default label not within a switch statement");
  /* XXX: no check for unreachable code */
}

void check_break(statement break_statement)
{
  if (!current.function_decl->current_loop)
    error("break statement not within loop or switch");

  break_statement->parent_loop = current.function_decl->current_loop;
}

void check_continue(statement continue_statement)
{
  statement loop = current.function_decl->current_loop;

  /* Find a loop */
  while (loop && is_switch_stmt(loop))
    loop = loop->parent_loop;

  if (!loop)
    error("continue statement not within a loop");

  continue_statement->parent_loop = loop;
}