/* This file is part of the nesC compiler.

This file is derived from the RC Compiler. It is thus
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

#include <stdarg.h>
#include <stdlib.h>

#include "parser.h"
#include "unparse.h"
#include "semantics.h"
#include "builtins.h"
#include "constants.h"
#include "AST_utils.h"
#include "errors.h"
#include "nesc-semantics.h"

/* Set this to 1 to avoid warnings from gcc about paren use with
   -Wparentheses */
#define CONSERVATIVE_PARENS 1

/* Pick an indentation amount */
#define INDENT 2

/* The output file for unparsing */
static FILE *of;
static bool no_line_directives;
static int indent_level;
static location output_loc;
static bool at_line_start;

static region tempregion;

typedef struct prt_closure {
  void (*fn)(struct prt_closure *closure);

  const char *name;
  struct prt_closure *parent;
} *prt_closure;


void indent(void)
{
  indent_level += INDENT;
}

void unindent(void)
{
  indent_level -= INDENT;
}

static void output_indentation(void)
{
  int i;

  for (i = 0; i < indent_level; i++) putc(' ', of);
}

void newline(void)
{
  putc('\n', of);
  output_loc.lineno++;
  at_line_start = TRUE;
}

void startline(void)
{
  if (!at_line_start) newline();
}

void startline_noindent(void)
{
  startline();
  at_line_start = FALSE;
}

static void output_indent_if_needed(void)
{
  if (at_line_start)
    {
      at_line_start = FALSE;
      output_indentation();
    }
}

static void voutput(char *format, va_list args)
{
  output_indent_if_needed();
  vfprintf(of, format, args);
}

void output(char *format, ...)
{
  va_list args;

  va_start(args, format);
  voutput(format, args);
  va_end(args);
}

void outputln(char *format, ...)
{
  va_list args;

  va_start(args, format);
  voutput(format, args);
  va_end(args);

  newline();
}

static void output_cstring(cstring s)
{
  output_indent_if_needed();
  fwrite(s.data, s.length, 1, of);
}

static void output_line_directive(location l, bool include_filename)
{
  startline_noindent();
  if (include_filename)
    /* The filename doesn't need any quoting as the quotes were not stripped on
       input */
    outputln("# %lu \"%s\"%s", l.lineno, l.filename,
	     l.in_system_header ? " 3" : "");
  else
    outputln("#line %lu", l.lineno);
}

static void set_location(location l)
{
  /* Ignore dummy locations */
  if (l.filename == dummy_location.filename || no_line_directives)
    return;

  if ((l.filename != output_loc.filename &&
       strcmp(l.filename, output_loc.filename)) ||
      l.in_system_header != output_loc.in_system_header)
    output_line_directive(l, TRUE);
  else if (output_loc.lineno != l.lineno)
    {
      /* Just send some newlines for small changes */
      if (output_loc.lineno < l.lineno && output_loc.lineno + 10 >= l.lineno)
	{
	  while (output_loc.lineno != l.lineno)
	    newline();
	}
      else
	output_line_directive(l, FALSE);
    }

  output_loc = l;
}

location output_location(void)
{
  return output_loc;
}


void prt_toplevel_declarations(declaration dlist);
void prt_toplevel_declaration(declaration d);
void prt_asm_decl(asm_decl d);
void prt_extension_decl(extension_decl d);
void prt_data_decl(data_decl d);
void prt_ellipsis_decl(ellipsis_decl d);
void prt_function_decl(function_decl d);
void prt_variable_decl(variable_decl d);
void prt_type_elements(type_element elements, bool duplicate);
void prt_type_element(type_element em, bool duplicate);
void prt_typename(typename tname);
void prt_typeof_expr(typeof_expr texpr);
void prt_typeof_type(typeof_type ttype);
void prt_attribute(attribute a);
void prt_rid(rid r);
void prt_qualifier(qualifier q);
void prt_tag_ref(tag_ref sr, bool duplicate);
void prt_fields(declaration flist);
void prt_enumerators(declaration elist, tag_declaration ddecl);
void prt_field_declaration(declaration d);
void prt_field_extension_decl(extension_decl d);
void prt_field_data_decl(data_decl d);
void prt_field_decl(field_decl fd);
void prt_enumerator(enumerator ed, tag_declaration ddecl);
void prt_asttype(asttype t);
void prt_word(word w);

void prt_expressions(expression elist, bool isfirst);
void prt_expression(expression e, int context_priority);
void prt_comma(comma e, int context_priority);
void prt_sizeof_type(sizeof_type e, int context_priority);
void prt_alignof_type(alignof_type e, int context_priority);
void prt_label_address(label_address e, int context_priority);
void prt_cast(cast e, int context_priority);
void prt_cast_list(cast_list e, int context_priority);
void prt_conditional(conditional e, int context_priority);
void prt_identifier(identifier e, int context_priority);
void prt_compound_expr(compound_expr e, int context_priority);
void prt_function_call(function_call e, int context_priority);
void prt_generic_call(generic_call e, int context_priority);
void prt_array_ref(array_ref e, int context_priority);
void prt_interface_deref(interface_deref e, int context_priority);
void prt_field_ref(field_ref e, int context_priority);
void prt_unary(unary e, int context_priority);
void prt_binary(binary e, int context_priority);
void prt_init_list(init_list e, int context_priority);
void prt_init_index(init_index e, int context_priority);
void prt_init_field(init_field e, int context_priority);
void prt_lexical_cst(lexical_cst e, int context_priority);
void prt_string(string e, int context_priority);
void prt_parameter_declarations(declaration dlist);
void prt_parameter_declaration(declaration d);

void prt_statement(statement s);
void prt_compound_stmt(compound_stmt s);
void prt_compound_declarations(declaration dlist);
void prt_compound_declaration(declaration d);
void prt_asm_stmt(asm_stmt s);
void prt_asm_stmt_plain(asm_stmt s);
void prt_asm_operands(asm_operand olist);
void prt_asm_operand(asm_operand o);
void prt_if_stmt(if_stmt s);
void prt_labeled_stmt(labeled_stmt s);
void prt_expression_stmt(expression_stmt s);
void prt_while_stmt(while_stmt s);
void prt_dowhile_stmt(while_stmt s);
void prt_switch_stmt(switch_stmt s);
void prt_for_stmt(for_stmt s);
void prt_break_stmt(break_stmt s);
void prt_continue_stmt(continue_stmt s);
void prt_return_stmt(return_stmt s);
void prt_goto_stmt(goto_stmt s);
void prt_computed_goto_stmt(computed_goto_stmt s);
void prt_empty_stmt(empty_stmt s);

void prt_label(label l);
void prt_id_label(id_label l);
void prt_case_label(case_label l);
void prt_default_label(default_label l);

void prt_regionof(expression e);

void unparse_start(FILE *to)
{
  tempregion = newregion();
  of = to;
  output_loc = dummy_location;
  at_line_start = TRUE;
  no_line_directives = FALSE;
  indent_level = 0;
}

void unparse_end(void) deletes
{
  deleteregion_ptr(&tempregion);
}

void unparse(FILE *to, declaration program) deletes
{
  unparse_start(to);
  prt_toplevel_declarations(program);
  unparse_end();
}

void enable_line_directives(void)
{
  if (no_line_directives)
    {
      no_line_directives = FALSE;
      /* Force #line on next output of some location */
      output_loc = dummy_location;
    }
}

void disable_line_directives(void)
{
  no_line_directives = TRUE;
}

void prt_toplevel_declarations(declaration dlist)
{
  declaration d;

  scan_declaration (d, dlist)
    prt_toplevel_declaration(d);
}

#define PRTCASE(type, x) case kind_ ## type: prt_ ## type(CAST(type, (x))); return

void prt_toplevel_declaration(declaration d)
{
  startline();
  switch (d->kind)
    {
      PRTCASE(asm_decl, d);
      PRTCASE(data_decl, d);
      PRTCASE(function_decl, d);
      PRTCASE(extension_decl, d);
    default: assert(0); break;
    }
}

/* Invariant: all declarations end with ; */
void prt_asm_decl(asm_decl d)
{
  prt_asm_stmt(d->asm_stmt);
}

void prt_extension_decl(extension_decl d)
{
  set_location(d->location);
  output("__extension__ ");
  prt_toplevel_declaration(d->decl);
}

void prt_ellipsis_decl(ellipsis_decl d)
{
  set_location(d->location);
  output("...");
}

static bool interesting_elements(type_element elems)
{
  type_element elem;

  scan_type_element (elem, elems)
    if (is_tag_ref(elem))
      return TRUE;

  return FALSE;
}

static void static_hack(data_declaration ddecl)
{
  /* Hack to add static to all defined functions */
  if (ddecl->kind == decl_function &&
      ddecl->ftype != function_static && !ddecl->isexterninline &&
      !ddecl->spontaneous)
    {
      output("static ");
      if (!ddecl->isinline)
	output("inline ");
    }
}

void prt_data_decl(data_decl d)
{
  declaration vd;
  bool first = TRUE;

  scan_declaration (vd, d->decls)
    {
      variable_decl vdd = CAST(variable_decl, vd);
      data_declaration vdecl = vdd->ddecl;

      if (vdecl) /* because of build_declaration */
	{
	  /* Ignore unused declarations */
	  if (((vdecl->kind == decl_function || vdecl->kind == decl_variable)
	       && !vdecl->isused))
	    continue;

	  static_hack(vdecl);
	}

      prt_type_elements(d->modifiers, !first);
      prt_type_elements(CAST(type_element, d->attributes), !first);
      first = FALSE;
      prt_variable_decl(vdd);
      outputln(";");
    }

  if (first && interesting_elements(d->modifiers))
    {
      prt_type_elements(d->modifiers, FALSE);
      prt_type_elements(CAST(type_element, d->attributes), FALSE);
      outputln(";");
    }
}

void prt_parameter_declarations(declaration dlist)
{
  declaration d;

  scan_declaration (d, dlist)
    prt_parameter_declaration(d);
}

void prt_parameter_declaration(declaration d)
{
  startline();
  switch (d->kind)
    {
      PRTCASE(data_decl, d);
      PRTCASE(ellipsis_decl, d);
    default: assert(0); break;
    }
}

void prt_function_decl(function_decl d)
{
  if (d->ddecl->isused && !d->ddecl->suppress_definition)
    {
      static_hack(d->ddecl);
      prt_declarator(d->declarator, d->qualifiers, d->attributes, d->ddecl,
		     psd_print_default);
      outputln(";");
    }
}

void prt_function_body(function_decl d)
{
  if (d->ddecl->isused && !d->ddecl->suppress_definition)
    {
      /* We set current.function_decl because unparsing may produce error
	 messages */
      current.function_decl = d;

      static_hack(d->ddecl);
      prt_declarator(d->declarator, d->qualifiers, d->attributes, d->ddecl,
		     psd_print_default);
      startline();
      prt_parameter_declarations(d->old_parms);
      assert(is_compound_stmt(d->stmt));
      prt_statement(d->stmt);
      newline();

      current.function_decl = d->parent_function;
    }
}

void prt_variable_decl(variable_decl d)
{
  prt_declarator(d->declarator, NULL, d->attributes, d->ddecl, 0);

  if (d->asm_stmt)
    prt_asm_stmt_plain(d->asm_stmt);

  if (d->arg1)
    {
      output(" = ");
      prt_expression(d->arg1, P_ASSIGN);
    }
}

void prt_declarator(declarator d, type_element elements, attribute attributes,
		    data_declaration ddecl, psd_options options)
{
  prt_type_elements(elements, FALSE);
  prt_type_elements(CAST(type_element, attributes), FALSE);
  prt_simple_declarator(d, ddecl, options & ~psd_not_star);
}

void prt_ddecl_full_name(data_declaration ddecl, psd_options options)
{
  if (!ddecl->Cname)
    {
      if (ddecl->container)
	output("%s$", ddecl->container->name);
      if (ddecl->kind == decl_function && ddecl->interface)
	output("%s$", ddecl->interface->name);
      if ((options & psd_print_default) &&
	  (!ddecl->defined && ddecl->kind == decl_function &&
	   (ddecl->ftype == function_event ||
	    ddecl->ftype == function_command)))
	output("default$");
    }
  output("%s", ddecl->name);
}

void prt_simple_declarator(declarator d, data_declaration ddecl,
			   psd_options options)
{
  if (d)
    switch (d->kind)
      {
      case kind_function_declarator:
	{
	  function_declarator fd = CAST(function_declarator, d);

	  prt_simple_declarator(fd->declarator, ddecl, options | psd_not_star);
	  prt_parameters(fd->gparms ? fd->gparms :
			 ddecl ? ddecl_get_gparms(ddecl) : NULL,
			 fd->parms,
			 options & psd_rename_parameters);
	  break;
	}
      case kind_array_declarator:
	{
	  array_declarator ad = CAST(array_declarator, d);

	  prt_simple_declarator(ad->declarator, ddecl, options | psd_not_star);
	  if (!ad->arg1)
	    output("[]");
	  else
	    {
	      set_location(ad->arg1->location);
	      output("[");
	      prt_expression(ad->arg1, P_TOP);
	      output("]");
	    }
	  break;
	}
      case kind_pointer_declarator:
	{
	  pointer_declarator pd = CAST(pointer_declarator, d);

	  if (pd->qualifiers)
	    set_location(pd->qualifiers->location);
	  if (options & psd_not_star)
	    output("(");
	  output("*");
	  prt_type_elements(pd->qualifiers, FALSE);
	  prt_simple_declarator(pd->declarator, ddecl, options & ~psd_not_star);
	  if (options & psd_not_star)
	    output(")");
	  break;
	}
      case kind_identifier_declarator:
	set_location(d->location);
	if (options & psd_rename_identifier)
	  output("arg_%p", ddecl);
	else if (ddecl)
	  prt_ddecl_full_name(ddecl, options);
	else
	  output("%s", CAST(identifier_declarator, d)->cstring.data);
	break;
      case kind_interface_ref_declarator:
	prt_simple_declarator(CAST(interface_ref_declarator, d)->declarator,
			      ddecl, options | psd_not_star);
	break;

      default: assert(0); break;
      }
}

void prt_type_elements(type_element elements, bool duplicate)
{
  type_element em;

  scan_type_element (em, elements)
    {
      prt_type_element(em, duplicate);
      output(" ");
    }
}

void prt_type_element(type_element em, bool duplicate)
{
  switch (em->kind)
    {
      PRTCASE(typename, em);
      PRTCASE(typeof_expr, em);
      PRTCASE(typeof_type, em);
      PRTCASE(attribute, em);
      PRTCASE(rid, em);
      PRTCASE(qualifier, em);
    default:
      if (is_tag_ref(em))
	prt_tag_ref(CAST(tag_ref, em), duplicate);
      else
	assert(0);
      break;
    }
}

void prt_typename(typename tname)
{
  data_declaration tdecl = tname->ddecl;

  set_location(tname->location);
  if (tdecl->container && !tdecl->Cname)
    output("%s$", tdecl->container->name);
  output("%s", tdecl->name);
}

void prt_typeof_expr(typeof_expr texpr)
{
  set_location(texpr->location);
  output("typeof(");
  prt_expression(texpr->arg1, P_TOP);
  output(")");
}

void prt_typeof_type(typeof_type ttype)
{
  set_location(ttype->location);
  output("typeof(");
  prt_asttype(ttype->asttype);
  output(")");
}

void prt_attribute(attribute a)
{
  if (!nesc_attribute(a))
    {
      set_location(a->location);
      output("__attribute((");
      prt_word(a->word1);
      if (a->word2 || a->args)
	{
	  output("(");
	  if (a->word2)
	    prt_word(a->word2);
	  prt_expressions(a->args, a->word2 == NULL);
	  output(")");
	}
      output("))");
    }
}

void prt_rid(rid r)
{
  switch (r->id)
    {
    case RID_COMMAND: case RID_EVENT: case RID_TASK: case RID_DEFAULT:
      break;
    default:
      set_location(r->location);
      output("%s", rid_name(r));
      break;
    }
}

void prt_qualifier(qualifier q)
{
  set_location(q->location);
  output("%s", qualifier_name(q->id));
}

void prt_tag_ref(tag_ref tr, bool duplicate)
{
  set_location(tr->location);
  switch (tr->kind)
    {
    case kind_struct_ref: output("struct "); break;
    case kind_union_ref: output("union "); break;
    case kind_enum_ref: output("enum "); break;
    default: assert(0);
    }

  if (tr->word1)
    {
      if (tr->tdecl && tr->tdecl->container)
	output("%s$", tr->tdecl->container->name);
      prt_word(tr->word1);
    }
  if (!duplicate && tr->defined)
    {
      if (tr->kind == kind_enum_ref)
	prt_enumerators(tr->fields, tr->tdecl);
      else
	prt_fields(tr->fields);
    }
  if (tr->attributes)
    {
      output(" ");
      prt_type_elements(CAST(type_element, tr->attributes), FALSE);
    }
}

void prt_enumerators(declaration elist, tag_declaration tdecl)
{
  declaration d;

  output(" {");
  indent();
  startline();
  scan_declaration (d, elist)
    {
      prt_enumerator(CAST(enumerator, d), tdecl);
      if (d->next)
	output(", ");
    }
  unindent();
  startline();
  output("}");
}

void prt_fields(declaration flist)
{
  declaration d;

  output(" {");
  indent();
  startline();
  scan_declaration (d, flist)
    prt_field_declaration(d);
  unindent();
  startline();
  output("}");
}

void prt_field_declaration(declaration d)
{
  if (is_extension_decl(d))
    prt_field_extension_decl(CAST(extension_decl, d));
  else
    prt_field_data_decl(CAST(data_decl, d));
}

void prt_field_extension_decl(extension_decl d)
{
  set_location(d->location);
  output("__extension__ ");
  prt_field_declaration(d->decl);
}

void prt_field_data_decl(data_decl d)
{
  declaration fd;

  prt_type_elements(d->modifiers, FALSE);
  prt_type_elements(CAST(type_element, d->attributes), FALSE);

  scan_declaration (fd, d->decls)
    {
      prt_field_decl(CAST(field_decl, fd));
      if (fd->next)
	output(", ");
    }
  outputln(";");
}

void prt_field_decl(field_decl fd)
{
  prt_declarator(fd->declarator, NULL, fd->attributes, NULL, 0);
  if (fd->arg1)
    {
      output(" : ");
      prt_expression(fd->arg1, P_TOP);
    }
}

void prt_enumerator(enumerator ed, tag_declaration tdecl)
{
  set_location(ed->location);

  if (tdecl && tdecl->container)
    output("%s$", tdecl->container->name);

  output_cstring(ed->cstring);
  if (ed->arg1)
    {
      output(" = ");
      prt_expression(ed->arg1, P_ASSIGN);
    }
}

void prt_parameters(declaration gparms, declaration parms, psd_options options)
{
  declaration d;
  bool forward = FALSE;
  bool first = TRUE;

  /* If asked to rename parameters, ask prt_parameter to rename identifiers
     when calling prt_declarator */
  if (options & psd_rename_parameters)
    options = psd_rename_identifier;
  else
    options = 0;

  output("(");
  scan_declaration (d, gparms)
    {
      prt_parameter(d, first, FALSE, options);
      first = FALSE;
    }
  scan_declaration (d, parms)
    {
      forward = prt_parameter(d, first, forward, options);
      first = FALSE;
    }
  if (!gparms && !parms)
    output("void");
  output(")");
}

bool prt_parameter(declaration parm, bool first, bool lastforward,
		   psd_options options)
{
  switch (parm->kind)
    {
    case kind_oldidentifier_decl:
      if (!first)
	output(", ");
      set_location(parm->location);
      output_cstring(CAST(oldidentifier_decl, parm)->cstring);
      return FALSE;
    case kind_ellipsis_decl:
      if (!first)
	output(", ");
      set_location(parm->location);
      output("...");
      return FALSE;
    case kind_data_decl:
      {
	data_decl dd = CAST(data_decl, parm);
	variable_decl vd = CAST(variable_decl, dd->decls);

	if (lastforward && !vd->forward)
	  output("; ");
	else if (!first)
	  output(", ");
	prt_type_elements(CAST(type_element, dd->attributes), FALSE);
	prt_declarator(vd->declarator, dd->modifiers, vd->attributes,
		       vd->ddecl, options);

	return vd->forward;
      }
    default: assert(0); return FALSE;
    }
}

void prt_asttype(asttype t)
{
  prt_declarator(t->declarator, t->qualifiers, NULL, NULL, 0);
}

void prt_word(word w)
{
  set_location(w->location);
  output_cstring(w->cstring);
}

void prt_expressions(expression elist, bool isfirst)
{
  expression e;

  scan_expression (e, elist)
    {
      if (!isfirst) output(", ");
      isfirst = FALSE;
      prt_expression(e, P_ASSIGN); /* priority is that of assignment */
    }
}

#define PRTEXPR(type, x) case kind_ ## type: prt_ ## type(CAST(type, (x)), context_priority); return

/* Context priorities are that of the containing operator, starting at 0
   for , going up to 14 for ->, . See the symbolic P_XX constants 
   P_TOP (-1) is used for contexts with no priority restrictions. */
void prt_expression(expression e, int context_priority)
{
  switch (e->kind) 
    {
      PRTEXPR(comma, e);
      PRTEXPR(sizeof_type, e);
      PRTEXPR(alignof_type, e);
      PRTEXPR(label_address, e);
      PRTEXPR(cast, e);
      PRTEXPR(cast_list, e);
      PRTEXPR(conditional, e);
      PRTEXPR(identifier, e);
      PRTEXPR(compound_expr, e);
      PRTEXPR(function_call, e);
      PRTEXPR(generic_call, e);
      PRTEXPR(array_ref, e);
      PRTEXPR(field_ref, e);
      PRTEXPR(interface_deref, e);
      PRTEXPR(init_list, e);
      PRTEXPR(init_index, e);
      PRTEXPR(init_field, e);
    case kind_string_cst:
      PRTEXPR(lexical_cst, e);
      PRTEXPR(string, e);
    default: 
      if (is_unary(e))
	{
	  prt_unary(CAST(unary, e), context_priority);
	  return;
	}
      assert(is_binary(e));
      prt_binary(CAST(binary, e), context_priority);
      return;
    }
}

#define OPEN(pri) \
  if (pri < context_priority) \
    output("(")

#define CLOSE(pri) \
  if (pri < context_priority) \
    output(")")

void prt_comma(comma e, int context_priority)
{
  OPEN(P_COMMA);
  prt_expressions(e->arg1, TRUE);
  CLOSE(P_COMMA);
}

void prt_sizeof_type(sizeof_type e, int context_priority)
{
  set_location(e->location);
  output("sizeof(");
  prt_asttype(e->asttype);
  output(")");
}

void prt_alignof_type(alignof_type e, int context_priority)
{
  set_location(e->location);
  output("__alignof__(");
  prt_asttype(e->asttype);
  output(")");
}

void prt_label_address(label_address e, int context_priority)
{
  set_location(e->location);
  output("&&");
  prt_id_label(e->id_label);
}

void prt_cast(cast e, int context_priority)
{
  OPEN(P_CAST);
  set_location(e->location);
  output("(");
  prt_asttype(e->asttype);
  output(")");
  prt_expression(e->arg1, P_CAST);
  CLOSE(P_CAST);
}

void prt_cast_list(cast_list e, int context_priority)
{
  OPEN(P_CAST);
  set_location(e->location);
  output("(");
  prt_asttype(e->asttype);
  output(")");
  prt_init_list(CAST(init_list, e->init_expr), P_ASSIGN);
  CLOSE(P_CAST);
}

void prt_conditional(conditional e, int context_priority)
{
  OPEN(P_COND);
  prt_expression(e->condition, P_COND);
  output(" ? ");
  if (e->arg1)
    prt_expression(e->arg1, P_COND);
  output(" : ");
  prt_expression(e->arg2, P_COND);
  CLOSE(P_COND);
}

void prt_identifier(identifier e, int context_priority)
{
  data_declaration decl = e->ddecl;

  if (decl->kind == decl_function && decl->uncallable)
    error_with_location(e->location, "%s not connected", e->cstring.data);

  set_location(e->location);
  if (decl->container && !decl->Cname)
    output("%s$", decl->container->name);

  output_cstring(e->cstring);
}

void prt_compound_expr(compound_expr e, int context_priority)
{
  set_location(e->location);
  output("(");
  prt_compound_stmt(CAST(compound_stmt, e->stmt));
  output(")");
}

void prt_function_call(function_call e, int context_priority)
{
  switch (e->call_kind)
    {
    case post_task:
      set_location(e->arg1->location);
      output("TOS_post(");
      prt_expression(e->arg1, P_ASSIGN);
      output(")");
      break;
    default:
      if (e->va_arg_call)
	{
	  /* The extra parentheses are added because gcc 2.96 (aka redhat 7's
	     gcc) has a broken syntax for __builtin_va_arg */
	  output("(__builtin_va_arg(");
	  prt_expression(e->args, P_ASSIGN);
	  output(", ");
	  prt_asttype(e->va_arg_call);
	  output("))");
	}
      else
	{
	  prt_expression(e->arg1, P_CALL);
	  /* Generic calls have already started the argument list.
	     See prt_generic_call */
	  if (is_generic_call(e->arg1))
	    prt_expressions(e->args, FALSE);
	  else
	    {
	      output("(");
	      prt_expressions(e->args, TRUE);
	    }
	  output(")");
	}
      break;
    }
}

void prt_generic_call(generic_call e, int context_priority)
{
  prt_expression(e->arg1, P_CALL);
  /* function_call will finish the argument list. See prt_function_call */
  output("(");
  prt_expressions(e->args, TRUE);

  /* This is a convenient place to do this check. We can't easily do it
     in make_generic_call as we don't (yet) know our parent. */
  if (!is_function_call(e->parent))
    error_with_location(e->location, "generic arguments can only be used in command/event calls");
}

void prt_array_ref(array_ref e, int context_priority)
{
  prt_expression(e->arg1, P_CALL);
  output("[");
  prt_expression(e->arg2, P_TOP);
  output("]");
}

void prt_field_ref(field_ref e, int context_priority)
{
  /* Reconstruct -> for nicer output */
  if (is_dereference(e->arg1))
    {
      prt_expression(CAST(dereference, e->arg1)->arg1, P_CALL);
      output("->");
    }
  else
    {
      prt_expression(e->arg1, P_CALL);
      output(".");
    }
  output_cstring(e->cstring);
}

void prt_interface_deref(interface_deref e, int context_priority)
{
  data_declaration decl = e->ddecl;

  if (decl->kind == decl_function && decl->uncallable)
    error_with_location(e->location, "%s.%s not connected",
			CAST(identifier, e->arg1)->cstring.data,
			e->cstring.data);

  prt_expression(e->arg1, P_CALL);
  output("$");
  output_cstring(e->cstring);
}

void prt_unary(unary e, int context_priority)
{
  char *op = NULL, *postop = NULL;
  int pri = 0;

  /* Yuck. Evil hack because gcc is broken (breaks the a[i] === *(a+i)
     rule when a is a non-lvalue array). So we undo our earlier rewrite
     (from fix.c) of a[i] as *(a+i). Note that gcc doesn't allow i[a] in
     this case (bozos at work?) */
  if (is_dereference(e) && is_plus(e->arg1))
    {
      plus derefed = CAST(plus, e->arg1);

      if (type_array(derefed->arg1->type))
	{
	  prt_array_ref(derefed, context_priority);
	  return;
	}
    }
  switch (e->kind)
    {
    case kind_dereference: op = "*"; break;
    case kind_extension_expr: op = "__extension__ "; break;
      /* Higher priority for sizeof/alignof expr because we must
	 add parens around sizeof cast_expr (e.g. sizeof((char)x), not
	 sizeof (char)x */
    case kind_sizeof_expr: op = "sizeof "; pri = P_CALL; break;
    case kind_alignof_expr: op = "__alignof__ "; pri = P_CALL; break;
    case kind_realpart: op = "__real__ "; break;
    case kind_imagpart: op = "__imag__ "; break;
    case kind_address_of: op = "&"; break;
    case kind_unary_minus: op = "-"; break;
    case kind_unary_plus: op = "+"; break;
    case kind_preincrement: op = "++"; break;
    case kind_predecrement: op = "--"; break;
    case kind_postincrement: postop = "++"; break;
    case kind_postdecrement: postop = "--"; break;
    case kind_conjugate: case kind_bitnot: op = "~"; break;
    case kind_not: op = "!"; break;
    default: assert(0); return;
    }
  OPEN(P_CAST);
  set_location(e->location);
  if (op)
    {
      output(op);
      if (is_unary(e->arg1))
	output(" "); /* Catch weirdness such as - - x */
      if (!pri)
	pri = P_CAST;
    }
  prt_expression(e->arg1, pri ? pri : P_CALL);
  if (postop)
    output(postop);
  CLOSE(P_CAST);
}

const char *binary_op_name(ast_kind kind)
{
  switch (kind)
    {
    case kind_plus: return "+"; 
    case kind_minus: return "-"; 
    case kind_times: return "*"; 
    case kind_divide: return "/"; 
    case kind_modulo: return "%"; 
    case kind_lshift: return "<<"; 
    case kind_rshift: return ">>"; 
    case kind_leq: return "<="; 
    case kind_geq: return ">="; 
    case kind_lt: return "<"; 
    case kind_gt: return ">"; 
    case kind_eq: return "=="; 
    case kind_ne: return "!="; 
    case kind_bitand: return "&"; 
    case kind_bitor: return "|"; 
    case kind_bitxor: return "^"; 
    case kind_andand: return "&&"; 
    case kind_oror: return "||"; 
    case kind_assign: return "="; 
    case kind_plus_assign: return "+="; 
    case kind_minus_assign: return "-="; 
    case kind_times_assign: return "*="; 
    case kind_divide_assign: return "/="; 
    case kind_modulo_assign: return "%="; 
    case kind_lshift_assign: return "<<="; 
    case kind_rshift_assign: return ">>="; 
    case kind_bitand_assign: return "&="; 
    case kind_bitor_assign: return "|="; 
    case kind_bitxor_assign: return "^="; 
    default: assert(0); return "<bad>";
    }
}

void prt_binary(binary e, int context_priority)
{
  int pri, lpri, rpri;
  const char *op = binary_op_name(e->kind);

  switch (e->kind)
    {
    case kind_times: case kind_divide: case kind_modulo:
      lpri = P_TIMES; pri = P_TIMES; rpri = P_CAST; break;
    case kind_plus: case kind_minus:
      lpri = P_PLUS; pri = P_PLUS; rpri = P_TIMES; break;
    case kind_lshift: case kind_rshift:
      pri = P_SHIFT;
      if (CONSERVATIVE_PARENS)
	lpri = rpri = P_TIMES;
      else
	{
	  lpri = P_SHIFT; rpri = P_PLUS; 
	}
      break;
    case kind_leq: case kind_geq: case kind_lt: case kind_gt:
      lpri = P_REL; pri = P_REL; rpri = P_SHIFT; break;
    case kind_eq: case kind_ne:
      lpri = P_EQUALS; pri = P_EQUALS; rpri = P_REL; break;
    case kind_bitand:
      pri = P_BITAND;
      if (CONSERVATIVE_PARENS)
	lpri = rpri = P_TIMES;
      else
	{
	  lpri = P_BITAND; rpri = P_EQUALS; 
	}
      break;
    case kind_bitxor:
      pri = P_BITXOR;
      if (CONSERVATIVE_PARENS)
	lpri = rpri = P_TIMES;
      else
	{
	  lpri = P_BITXOR; rpri = P_BITAND;
	}
      break;
    case kind_bitor:
      pri = P_BITOR;
      if (CONSERVATIVE_PARENS)
	lpri = rpri = P_TIMES;
      else
	{
	  lpri = P_BITOR; rpri = P_BITXOR;
	}
      break;
    case kind_andand:
      lpri = P_AND; pri = P_AND; rpri = P_BITOR; break;
    case kind_oror:
      pri = P_OR;
      if (CONSERVATIVE_PARENS)
	lpri = rpri = P_BITOR;
      else
	{
	  lpri = P_OR; rpri = P_AND; 
	}
      break;
    case kind_assign: case kind_plus_assign: case kind_minus_assign: 
    case kind_times_assign: case kind_divide_assign: case kind_modulo_assign:
    case kind_lshift_assign: case kind_rshift_assign: case kind_bitand_assign:
    case kind_bitor_assign: case kind_bitxor_assign:
      lpri = P_CAST; pri = P_ASSIGN; rpri = P_ASSIGN; break;
    default: assert(0); return;
    }
  OPEN(pri);
  prt_expression(e->arg1, lpri);
  set_location(e->location);
  output(" %s ", op);
  prt_expression(e->arg2, rpri);
  CLOSE(pri);
}

void prt_lexical_cst(lexical_cst e, int context_priority)
{
  set_location(e->location);
  output_cstring(e->cstring);
}

void prt_string(string e, int context_priority)
{
  expression s;

  scan_expression (s, e->strings)
    prt_expression(s, P_TOP);
}

void prt_init_list(init_list e, int context_priority)
{
  set_location(e->location);
  output("{ ");
  prt_expressions(e->args, TRUE);
  output(" }");
}

void prt_init_index(init_index e, int context_priority)
{
  set_location(e->location);
  output("[");
  prt_expression(e->arg1, P_ASSIGN);
  if (e->arg2)
    {
      output(" ... ");
      prt_expression(e->arg2, P_ASSIGN);
    }
  output("] ");
  prt_expression(e->init_expr, P_ASSIGN);
}

void prt_init_field(init_field e, int context_priority)
{
  prt_word(e->word1);
  output(" : ");
  prt_expression(e->init_expr, P_ASSIGN);
}

void prt_statement(statement s)
{
  switch (s->kind)
    {
      PRTCASE(asm_stmt, s);
      PRTCASE(compound_stmt, s);
      PRTCASE(if_stmt, s);
      PRTCASE(labeled_stmt, s);
      PRTCASE(expression_stmt, s);
      PRTCASE(while_stmt, s);
      PRTCASE(dowhile_stmt, s);
      PRTCASE(switch_stmt, s);
      PRTCASE(for_stmt, s);
      PRTCASE(break_stmt, s);
      PRTCASE(continue_stmt, s);
      PRTCASE(return_stmt, s);
      PRTCASE(goto_stmt, s);
      PRTCASE(computed_goto_stmt, s);
      PRTCASE(empty_stmt, s);
    default: assert(0); return;
    }
}

static void prt_as_compound(statement s)
{
  if (!is_compound_stmt(s))
    outputln("{");
  prt_statement(s);
  if (!is_compound_stmt(s))
    {
      startline();
      outputln("}");
    }
}

void prt_compound_stmt(compound_stmt s)
{
  statement s1;

  set_location(s->location);
  outputln("{");
  indent();
  if (s->id_labels)
    {
      id_label l;

      output("__label__ ");
      scan_id_label (l, s->id_labels)
	{
	  prt_id_label(l);
	  if (l->next) 
	    output(", ");
	}
      outputln(";");
    }
  if (s->decls)
    {
      prt_compound_declarations(s->decls);
      newline();
    }

  scan_statement (s1, s->stmts)
    prt_statement(s1);

  unindent();
  outputln("}");
}

void prt_compound_declarations(declaration dlist)
{
  declaration d;

  scan_declaration (d, dlist)
    prt_compound_declaration(d);
}

void prt_compound_declaration(declaration d)
{
  startline();
  switch (d->kind)
    {
      PRTCASE(data_decl, d);
      PRTCASE(extension_decl, d);
      PRTCASE(function_decl, d);
    default: assert(0); break;
    }
}

void prt_asm_stmt(asm_stmt s)
{
  prt_asm_stmt_plain(s);
  output(";");
}

void prt_asm_stmt_plain(asm_stmt s)
{
  set_location(s->location);
  output(" __asm ");
  if (s->qualifiers)
    prt_type_elements(s->qualifiers, FALSE);
  output("(");
  prt_expression(s->arg1, P_TOP);
  if (s->asm_operands1 || s->asm_operands2 || s->asm_clobbers)
    {
      output(" : ");
      prt_asm_operands(s->asm_operands1);

      if (s->asm_operands2 || s->asm_clobbers)
	{
	  output(" : ");
	  prt_asm_operands(s->asm_operands2);

	  if (s->asm_clobbers)
	    {
	      output(" : ");
	      prt_expressions(CAST(expression, s->asm_clobbers), TRUE);
	    }
	}
    }
  output(")");
}

void prt_asm_operands(asm_operand olist)
{
  asm_operand o;

  scan_asm_operand (o, olist)
    {
      prt_asm_operand(o);
      if (o->next)
	output(", ");
    }
}

void prt_asm_operand(asm_operand o)
{
  prt_string(o->string, P_TOP);
  output("(");
  prt_expression(o->arg1, P_TOP);
  output(")");
}

void prt_if_stmt(if_stmt s)
{
  set_location(s->location);
  output("if (");
  /* CONSERVATIVE_PARENS: force parens around assignment within if */
  prt_expression(s->condition, CONSERVATIVE_PARENS ? P_COND : P_TOP);
  output(") ");
  indent();
  prt_as_compound(s->stmt1);
  unindent();
  if (s->stmt2)
    {
      startline();
      output("else ");
      indent();
      prt_as_compound(s->stmt2);
      unindent();
    }
}

void prt_labeled_stmt(labeled_stmt s)
{
  prt_label(s->label);
  output(": ");
  indent();
  prt_statement(s->stmt);
  unindent();
}

void prt_expression_stmt(expression_stmt s)
{
  prt_expression(s->arg1, P_TOP);
  outputln(";");
}

void prt_while_stmt(while_stmt s)
{
  set_location(s->location);
  output("while (");
  /* CONSERVATIVE_PARENS: force parens around assignment within while */
  prt_expression(s->condition, CONSERVATIVE_PARENS ? P_COND : P_TOP);
  output(") ");
  indent();
  prt_statement(s->stmt);
  unindent();
}

void prt_dowhile_stmt(while_stmt s)
{
  set_location(s->location);
  output("do ");
  indent();
  prt_statement(s->stmt);
  unindent();
  startline();
  output("while (");
  /* CONSERVATIVE_PARENS: force parens around assignment within do while */
  prt_expression(s->condition, CONSERVATIVE_PARENS ? P_COND : P_TOP);
  outputln(");");
}

void prt_switch_stmt(switch_stmt s)
{
  set_location(s->location);
  output("switch (");
  /* CONSERVATIVE_PARENS: force parens around assignment within switch */
  prt_expression(s->condition, CONSERVATIVE_PARENS ? P_COND : P_TOP);
  output(") ");
  indent();
  prt_statement(s->stmt);
  unindent();
}

void prt_for_stmt(for_stmt s)
{
  set_location(s->location);
  output("for (");
  if (s->arg1)
    prt_expression(s->arg1, P_TOP);
  output("; ");
  if (s->arg2)
    prt_expression(s->arg2, P_TOP);
  output("; ");
  if (s->arg3)
    prt_expression(s->arg3, P_TOP);
  output(") ");
  indent();
  prt_statement(s->stmt);
  unindent();
}  

void prt_break_stmt(break_stmt s)
{
  set_location(s->location);
  outputln("break;");
}

void prt_continue_stmt(continue_stmt s)
{
  set_location(s->location);
  outputln("continue;");
}

void prt_return_stmt(return_stmt s)
{
  set_location(s->location);
  if (s->arg1)
    {
      output("return ");
      prt_expression(s->arg1, P_TOP);
      outputln(";");
    }
  else
    outputln("return;");
}

void prt_goto_stmt(goto_stmt s)
{
  set_location(s->location);
  output("goto ");
  prt_id_label(s->id_label);
  outputln(";");
}

void prt_computed_goto_stmt(computed_goto_stmt s)
{
  set_location(s->location);
  output("goto *");
  prt_expression(s->arg1, P_TOP);
  outputln(";");
}

void prt_empty_stmt(empty_stmt s)
{
  set_location(s->location);
  outputln(";");
}

void prt_label(label l)
{
  switch (l->kind)
    {
      PRTCASE(id_label, l);
      PRTCASE(case_label, l);
      PRTCASE(default_label, l);
    default: assert(0); return;
    }
}

void prt_id_label(id_label l)
{
  set_location(l->location);
  output_cstring(l->cstring);
}

void prt_case_label(case_label l)
{
  set_location(l->location);
  output("case ");
  prt_expression(l->arg1, P_ASSIGN);
  if (l->arg2)
    {
      output(" ... ");
      prt_expression(l->arg2, P_ASSIGN);
    }
}

void prt_default_label(default_label l)
{
  set_location(l->location);
  output("default");
}