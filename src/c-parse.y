/* This file is part of the nesC compiler.

This file is derived from the RC and the GNU C Compiler. It is thus
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

/* This is a version of c-parse.y with all but one conflict removed, at the
   price of the removal of most attribute syntax and of some error
   recovery. It is mostly intended to make grammar experimentation easier
   (it's hard to tell if a grammar change is causing problems when there
   are 46 s/r conflicts) */

/* This file defines the grammar of C */
/* To whomever it may concern: I have heard that such a thing was once
   written by AT&T, but I have never seen it.  */

%expect 1

%pure_parser


%{
#include <stdio.h>
#include <errno.h>
#include <setjmp.h>

#include "parser.h"
#include "c-parse.h"
#include "c-lex.h"
#include "semantics.h"
#include "input.h"
#include "expr.h"
#include "stmt.h"
#include "nesc-semantics.h"
#include "nesc-interface.h"
#include "nesc-component.h"
#include "nesc-module.h"
#include "nesc-env.h"

int yyparse(void) deletes;

void yyerror();

/* Like YYERROR but do call yyerror.  */
#define YYERROR1 { yyerror ("syntax error"); YYERROR; }

/* Cause the `yydebug' variable to be defined.  */
#define YYDEBUG 1
%}

%start dispatch

/* All identifiers that are not reserved words
   and are not declared typedefs in the current block */
%token IDENTIFIER

/* All identifiers that are declared typedefs in the current block.
   In some contexts, they are treated just like IDENTIFIER,
   but they can also serve as typespecs in declarations.  */
%token TYPENAME

/* Reserved words that specify storage class.
   yylval contains an IDENTIFIER_NODE which indicates which one.  */
%token <u.itoken> SCSPEC

/* Reserved words that specify type.
   yylval contains an IDENTIFIER_NODE which indicates which one.  */
%token <u.itoken> TYPESPEC

/* Reserved words that qualify types/functions: "const" or "volatile", 
   "deletes".
   yylval contains an IDENTIFIER_NODE which indicates which one.  */
%token <u.itoken> TYPE_QUAL FN_QUAL

/* Character or numeric constants.
   yylval is the node for the constant.  */
%token CONSTANT

/* String constants in raw form.
   yylval is a STRING_CST node.  */
%token STRING MAGIC_STRING

/* "...", used for functions with variable arglists.  */
%token <u.itoken> ELLIPSIS

/* the reserved words */
/* SCO include files test "ASM", so use something else. */
%token <u.itoken> SIZEOF ENUM STRUCT UNION IF ELSE WHILE DO FOR SWITCH CASE DEFAULT
%token <u.itoken> BREAK CONTINUE RETURN GOTO ASM_KEYWORD TYPEOF ALIGNOF
%token <u.itoken> ATTRIBUTE EXTENSION LABEL
%token <u.itoken> REALPART IMAGPART VA_ARG OFFSETOF

/* Add precedence rules to solve dangling else s/r conflict */
%nonassoc IF
%nonassoc ELSE

/* Define the operator tokens and their precedences.
   The value is an integer because, if used, it is the tree code
   to use in the expression made from the operator.  */

%right <u.itoken> ASSIGN '='
%right <u.itoken> '?' ':'
%left <u.itoken> OROR
%left <u.itoken> ANDAND
%left <u.itoken> '|'
%left <u.itoken> '^'
%left <u.itoken> '&'
%left <u.itoken> EQCOMPARE
%left <u.itoken> ARITHCOMPARE
%left <u.itoken> LSHIFT RSHIFT
%left <u.itoken> '+' '-'
%left <u.itoken> '*' '/' '%'
%right <u.itoken> PLUSPLUS MINUSMINUS
%left <u.itoken> POINTSAT '.' '(' '['

%type <u.asm_operand> asm_operand asm_operands nonnull_asm_operands
%type <u.asm_stmt> maybeasm
%type <u.attribute> maybe_attribute attributes attribute attribute_list attrib
%type <u.constant> CONSTANT
%type <u.decl> datadecl datadecls datadef decl decls extdef extdefs fndef
%type <u.decl> initdecls initdecls_ notype_initdecls notype_initdecls_ fndef2
%type <u.decl> nested_function notype_nested_function old_style_parm_decls
%type <u.decl> initdcl component_decl_list component_decl_list2 component_decl
%type <u.decl> components component_declarator enumerator enumlist
%type <u.decl> parmlist parmlist_1 parmlist_2 parms parm
%type <u.decl> parmlist_or_identifiers identifiers notype_initdcl
%type <u.decl> parmlist_or_identifiers_1 old_parameter just_datadef
%type <u.declarator> declarator after_type_declarator notype_declarator
%type <u.declarator> absdcl absdcl1 parm_declarator
%type <u.expr> cast_expr expr expr_no_commas exprlist init initlist_maybe_comma
%type <u.expr> initlist1 initelt nonnull_exprlist primary string_component 
%type <u.expr> STRING string_list nonnull_exprlist_
%type <u.expr> unary_expr xexpr function_call
%type <u.id_label> id_label maybe_label_decls label_decls label_decl
%type <u.id_label> identifiers_or_typenames
%type <idtoken> identifier IDENTIFIER TYPENAME MAGIC_STRING
%type <u.iexpr> if_prefix
%type <u.istmt> stmt_or_labels simple_if stmt_or_label
%type <u.itoken> unop extension '~' '!' compstmt_start '{' ';'
%type <u.itoken> sizeof alignof
%type <u.label> label
%type <u.stmt> stmts xstmts compstmt_or_error compstmt
%type <u.stmt> labeled_stmt stmt
%type <u.cstmt> do_stmt_start
%type <u.string> asm_clobbers string
%type <u.telement> scspec type_qual type_spec
%type <u.telement> declmods
%type <u.telement> reserved_declspecs
%type <u.telement> typed_declspecs
%type <u.telement> typed_typespecs reserved_typespecquals 
%type <u.telement> typespec typespecqual_reserved structsp
%type <u.telement> nonempty_type_quals type_quals
%type <u.telement> maybe_type_qual fn_qual fn_quals
%type <u.type> typename
%type <u.word> idword any_word tag
%type <u.fields> fieldlist

/* the dispatching (fake) tokens */
%token <u.itoken> DISPATCH_C DISPATCH_INTERFACE DISPATCH_COMPONENT

/* tinyos reserved words */
%token <u.itoken> USES DEFINES INTERFACE REQUIRES PROVIDES MODULE INCLUDES
%token <u.itoken> CONFIGURATION AS TASTNIOP IMPLEMENTATION CALL SIGNAL POST

%type <u.itoken> callkind
%type <u.iflist> uses_or_defines uses_or_defines_list uses defines
%type <u.decl> datadef_list function_list parameter_list parameter
%type <u.decl> parameters parameters1
%type <u.telement> parameter_type
%type <u.rplist> requires provides requires_or_provides requires_or_provides_list
%type <u.decl> parameterised_interface_list parameterised_interface
%type <u.decl> parameterised_interfaces 
%type <u.iref> interface_ref
%type <u.cref> component_ref component_list cuses
%type <u.conn> connection connection_list
%type <u.ep> endpoint
%type <u.pid> parameterised_identifier
%type <u.impl> iconfiguration imodule

%{
/* Region in which to allocate parse structures. Idea: the AST user can set
   this to different regions at appropriate junctures depending on what's
   being done with the AST */
region parse_region;
/* We'll see this a LOT below */
#define pr parse_region

/* Number of statements (loosely speaking) and compound statements 
   seen so far.  */
static int stmt_count;
static int compstmt_count;
  
#ifdef RC_ADJUST
static size_t rc_adjust_yystype(void *x, int by) 
{
  struct yystype *p = x;
  RC_ADJUST_PREAMBLE;

  RC_ADJUST(p->u.ptr, by);
  RC_ADJUST(p->idtoken.location.filename, by);
  RC_ADJUST(p->idtoken.id.data, by);
  RC_ADJUST(p->idtoken.decl, by);

  return sizeof *p;
}

static void rc_update_yystype(struct yystype *old, struct yystype *new)
{
  regionid base = regionidof(old);

  RC_UPDATE(base, old->u.ptr, new->u.ptr);
  RC_UPDATE(base, old->idtoken.location.filename, new->idtoken.location.filename);
  RC_UPDATE(base, old->idtoken.id.data, new->idtoken.id.data);
  RC_UPDATE(base, old->idtoken.decl, new->idtoken.decl);
}
#endif

/* A stack of declspecs and attributes for use during parsing */
typedef struct spec_stack *spec_stack;
struct spec_stack { 
  type_element parentptr declspecs;
  attribute parentptr attributes;
  spec_stack sameregion next;
};

struct parse_state 
{
  /* Stack of saved values of current_declspecs and prefix_attributes.  */
  /* In an ideal world, we would be able to eliminate most rc ops for
     declspec_stack and ds_region assignments. Seems tricky though. */
  spec_stack declspec_stack;
  region ds_region;

  /* List of types and structure classes of the current declaration.  */
  type_element declspecs;
  attribute prefix_attributes;

  /* >0 if currently parsing an expression that will not be evaluated (argument
     to alignof, sizeof. Currently not typeof though that could be considered
     a bug) */
  int unevaluated_expression;
} pstate;

bool unevaluated_expression(void)
{
  return pstate.unevaluated_expression != 0;
}

/* Pop top entry of declspec_stack back into current_declspecs,
   prefix_attributes */
static void pop_declspec_stack(void) deletes
{
  pstate.declspecs = pstate.declspec_stack->declspecs;
  pstate.prefix_attributes = pstate.declspec_stack->attributes;
  pstate.declspec_stack = pstate.declspec_stack->next;
}

static void push_declspec_stack(void)
{
  spec_stack news;

  news = ralloc(pstate.ds_region, struct spec_stack);
  news->declspecs = pstate.declspecs;
  news->attributes = pstate.prefix_attributes;
  assert(news->attributes == NULL); /* I killed the broken attribute syntax */
  news->next = pstate.declspec_stack;
  pstate.declspec_stack = news;
}

void parse(void) deletes
{
  int result, old_errorcount = errorcount;
  struct parse_state old_pstate = pstate;

  pstate.declspecs = NULL;
  pstate.prefix_attributes = NULL;
  pstate.unevaluated_expression = 0;
  pstate.declspec_stack = NULL;
  pstate.ds_region = newsubregion(parse_region);
  result = yyparse();
  deleteregion_ptr(&pstate.ds_region);

  if (result != 0 && errorcount == old_errorcount)
    fprintf(stderr, "Errors detected in input file (your bison.simple is out of date)");

  pstate = old_pstate;
}

/* Tell yyparse how to print a token's value, if yydebug is set.  */

#define YYPRINT(FILE,YYCHAR,YYLVAL) yyprint(FILE,YYCHAR,YYLVAL)
void yyprint();
%}

%%

dispatch:
	DISPATCH_INTERFACE interface { }
	| DISPATCH_COMPONENT component { }
	| DISPATCH_C extdefs { cdecls = declaration_reverse($2); }
	;

includes_list: includes_list includes 
	| /* empty */
	;

includes:
	INCLUDES include_list ';' { }
	;

include_list:
	identifier { require_c($1.location, $1.id.data); }
	| include_list ',' identifier { require_c($3.location, $3.id.data); }
	;

interface: 
	includes_list
	INTERFACE idword '{' uses_or_defines_list '}' 
		{
		  the_interface = new_interface(pr, $2.location, $3, interface_functions_reverse($5));
		}
	;

uses_or_defines_list: 
	uses_or_defines_list uses_or_defines 
		{ $$ = interface_functions_chain($2, $1); }
	| uses_or_defines 
	;

uses_or_defines: uses | defines ;

uses: USES { interface_defines = FALSE; } function_list 
	{ $$ = new_interface_functions(pr, $1.location, FALSE, $3); } ;

defines: DEFINES { interface_defines = TRUE; } function_list
	{ $$ = new_interface_functions(pr, $1.location, TRUE, $3); } ;

function_list: just_datadef | '{' datadef_list '}' { $$ = declaration_reverse($2); };

datadef_list: 
	datadef_list just_datadef { $$ = declaration_chain($2, $1); }
	| just_datadef ;

parameters: '[' { pushlevel(TRUE); } parameters1 
	{ /* poplevel done in users of parameters */ $$ = $3; } ;

parameters1: 
	  parameter_list ']' { $$ = declaration_reverse($1); }
	| error ']' { $$ = new_error_decl(pr, dummy_location); }
	;

parameter_list: parameter
	| parameter_list ',' parameter { $$ = declaration_chain($3, $1) } ;

parameter: parameter_type identifier
   	{ 
	  identifier_declarator id =
	    new_identifier_declarator(pr, $2.location, $2.id);
	  $$ = declare_parameter(CAST(declarator, id), $1, NULL, NULL, TRUE);
	}
	;

parameter_type: typespec 
	| parameter_type typespec { $$ = type_element_chain($1, $2); }
	;



component: includes_list module
	| includes_list configuration
	;

module: MODULE idword '{' requires_or_provides_list '}' imodule
	{ the_component = new_component(pr, $1.location, $2, rp_interface_reverse($4), $6); }
	;

configuration: CONFIGURATION idword '{' requires_or_provides_list '}' iconfiguration
	{ the_component = new_component(pr, $1.location, $2, rp_interface_reverse($4), $6); }
	;

requires_or_provides_list: 
	requires_or_provides_list requires_or_provides
		{ $$ = rp_interface_chain($2, $1); }
	| /* empty */ { $$ = NULL; }
	;

requires_or_provides: requires | provides ;

requires: REQUIRES { component_requires = TRUE; interface_defines = FALSE; } parameterised_interface_list 
		{ $$ = new_rp_interface(pr, $1.location, TRUE, declaration_reverse($3)); } ;

provides: PROVIDES { component_requires = FALSE; interface_defines = TRUE; } parameterised_interface_list 
		{ $$ = new_rp_interface(pr, $1.location, FALSE, declaration_reverse($3)); } ;

parameterised_interface_list:
	parameterised_interface
	| '{' parameterised_interfaces '}' { $$ = $2; }
	;

parameterised_interfaces: 
	parameterised_interfaces parameterised_interface 
		{ $$ = declaration_chain($2, $1); }
	| parameterised_interface
	;

parameterised_interface:
	just_datadef
	| interface_ref ';' 
		{
		  declare_interface_ref($1, NULL, NULL);
		  $$ = CAST(declaration, $1);
		}
	| interface_ref parameters ';'
		{ 
		  $1->gparms = $2;
		  declare_interface_ref($1, $2, poplevel());
		  $$ = CAST(declaration, $1);
		}
	;

interface_ref: 
	INTERFACE idword 
		{ $$ = new_interface_ref(pr, $1.location, $2, NULL, NULL); }
	| INTERFACE idword idword
		{ $$ = new_interface_ref(pr, $1.location, $2, $3, NULL); }
	;

iconfiguration:
	IMPLEMENTATION { $<u.env>$ = start_implementation(); } '{'
	  cuses
	  connection_list
	'}'
		{ $$ = CAST(implementation, new_configuration(pr, $1.location, $<u.env>2, $4, connection_reverse($5)));
		}
	;

cuses:  /* empty */ { $$ = NULL; }
	| USES component_list ';' { $$ = component_ref_reverse($2); }
	;

component_list: 
	component_list ',' component_ref { $$ = component_ref_chain($3, $1); }
	| component_ref
	;

component_ref: 
	idword { $$ = new_component_ref(pr, $1->location, $1, NULL); }
	| idword AS idword { $$ = new_component_ref(pr, $1->location, $1, $3); }
	;

connection_list: 
	connection_list connection { $$ = connection_chain($2, $1); }
	| connection
	;

connection:
	endpoint '=' endpoint ';' 
		{ $$ = CAST(connection, new_eq_connection(pr, $2.location, $3, $1)); }
	| endpoint POINTSAT endpoint ';' 
		{ $$ = CAST(connection, new_rp_connection(pr, $2.location, $3, $1)); }
	| endpoint TASTNIOP endpoint ';'
		{ $$ = CAST(connection, new_rp_connection(pr, $2.location, $1, $3)); }
	;

endpoint: 
	endpoint '.' parameterised_identifier
		{ $$ = $1;
		  $$->ids = parameterised_identifier_chain($$->ids, $3);
		}
	| parameterised_identifier 
		{ $$ = new_endpoint(parse_region, $1->location, $1); }
	;

parameterised_identifier:
	idword 
	  { $$ = new_parameterised_identifier(pr, $1->location, $1, NULL); }
	| idword '[' nonnull_exprlist ']'
	  { $$ = new_parameterised_identifier(pr, $1->location, $1, $3); }
	;


imodule: IMPLEMENTATION { $<u.env>$ = start_implementation(); } '{' extdefs '}' 
		{ 
		  $$ = CAST(implementation, new_module(pr, $1.location, $<u.env>2, declaration_reverse($4))); 
		} ;

/* the reason for the strange actions in this rule
 is so that notype_initdecls when reached via datadef
 can find a valid list of type and sc specs in $0. */

extdefs:
	{ $<u.telement>$ = NULL; } extdef { $$ = $2; }
	| extdefs { $<u.telement>$ = NULL; } extdef
		{ $$ = declaration_chain($3, $1); }	  
	;

extdef:
	fndef
	| datadef
	| ASM_KEYWORD '(' expr ')' ';'
		{ 
		  $$ = CAST(declaration, new_asm_decl
		    (pr, $1.location,
		     new_asm_stmt(pr, $1.location, $3, NULL, NULL, NULL, NULL))); }
	| extension extdef
		{ pedantic = $1.i; 
		  $$ = CAST(declaration, new_extension_decl(pr, $1.location, $2)); }
	;

just_datadef: 
	{ $<u.telement>$ = NULL; } datadef { $$ = $2; } ;

datadef:
	  setspecs notype_initdecls ';'
		{ if (pedantic)
		    error("ANSI C forbids data definition with no type or storage class");
		  else if (!flag_traditional)
		    warning("data definition has no type or storage class"); 

		  $$ = CAST(declaration, new_data_decl(pr, $2->location, NULL, NULL, $2));
		  pop_declspec_stack(); }
        | declmods setspecs notype_initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
	| typed_declspecs setspecs initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
        | declmods ';'
	  { pedwarn("empty declaration"); }
	| typed_declspecs setspecs ';'
	  { shadow_tag($1); 
	    $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, NULL));
	    pop_declspec_stack(); }
	| error ';' { $$ = new_error_decl(pr, last_location); }
	| error '}' { $$ = new_error_decl(pr, last_location); }
	| ';'
		{ if (pedantic)
		    pedwarn("ANSI C does not allow extra `;' outside of a function");
		  $$ = NULL; }
	;

fndef:
	  typed_declspecs setspecs declarator fndef2 { $$ = $4; }
	| declmods setspecs notype_declarator fndef2 { $$ = $4; }
	| setspecs notype_declarator fndef2 { $$ = $3; }
	;

fndef2:	  maybeasm maybe_attribute
		{ 
		  /* maybeasm is only here to avoid a s/r conflict */
		  if ($1)
		    error_with_location($1->location,
		    			"unexpected asm statement");
		  /* $0 refers to the declarator that precedes fndef2
		     in fndef (we can't just save it in an action, as that
		     causes s/r and r/r conflicts) */
		  if (!start_function(pstate.declspecs, $<u.declarator>0, $2, 0))
		    YYERROR1; 
		}
	  old_style_parm_decls
		{ store_parm_decls(declaration_reverse($4)); }
	  compstmt_or_error
		{ $$ = finish_function($6);
		  pop_declspec_stack(); }
	;

identifier:
	IDENTIFIER
	| TYPENAME
	;

id_label:
	identifier { $$ = new_id_label(pr, $1.location, $1.id); }
	;

idword:
	identifier { $$ = new_word(pr, $1.location, $1.id); }
        ;

unop:     '&'
		{ $$ = $1; $$.i = kind_address_of; }
	| '-'
		{ $$ = $1; $$.i = kind_unary_minus; }
	| '+'
		{ $$ = $1; $$.i = kind_unary_plus; }
	| PLUSPLUS
		{ $$ = $1; $$.i = kind_preincrement; }
	| MINUSMINUS
		{ $$ = $1; $$.i = kind_predecrement; }
	| '~'
		{ $$ = $1; $$.i = kind_bitnot; }
	| '!'
		{ $$ = $1; $$.i = kind_not; }
	| REALPART
		{ $$ = $1; $$.i = kind_realpart; }
	| IMAGPART
		{ $$ = $1; $$.i = kind_imagpart; }
	;

expr:	nonnull_exprlist
		{ if ($1->next)
		    $$ = make_comma($1->location, $1);
		  else
		    $$ = $1; }
	;

exprlist:
	  /* empty */
		{ $$ = NULL; }
	| nonnull_exprlist
	;

nonnull_exprlist:
	nonnull_exprlist_
		{ $$ = expression_reverse($1); }
	;

nonnull_exprlist_:
	expr_no_commas
		{ $$ = $1; }
	| nonnull_exprlist_ ',' expr_no_commas
		{ $$ = expression_chain($3, $1); }
	;

callkind:
	CALL { $$.i = command_call; }
	| SIGNAL { $$.i = event_signal; }
	| POST { $$.i = post_task; }
	;

unary_expr:
	primary
	| callkind function_call 
		{
		  function_call fc = CAST(function_call, $2);
		  type calltype = fc->arg1->type;
		  
		  $$ = $2;
		  CAST(function_call, $$)->call_kind = $1.i;
		  switch ($1.i)
		    {
		    case command_call:
		      if (!type_command(calltype))
			error("only commands can be called");
		      break;
		    case event_signal:
		      if (!type_event(calltype))
			error("only events can be signaled");
		      break;
		    case post_task:
		      if (!type_task(calltype))
			error("only tasks can be posted");
		      break;
		    }
		}
	| '*' cast_expr
		{ $$ = make_dereference($1.location, $2); }
	/* __extension__ turns off -pedantic for following primary.  */
	| extension cast_expr	
		{ $$ = make_extension_expr($1.location, $2);
		  pedantic = $1.i; }
	| unop cast_expr
		{ $$ = make_unary($1.location, $1.i, $2);
#if 0
		  overflow_warning($$); 
#endif
		}
	/* Refer to the address of a label as a pointer.  */
	| ANDAND id_label
		{
		  $$ = CAST(expression, make_label_address($1.location, $2));
		  use_label($2);
		}
	| sizeof unary_expr
		{ 
#if 0
		  if (TREE_CODE ($2) == COMPONENT_REF
		      && DECL_C_BIT_FIELD (TREE_OPERAND ($2, 1)))
		    error("`sizeof' applied to a bit-field");
		  $$ = c_sizeof (TREE_TYPE ($2)); 
#endif
		  $$ = make_sizeof_expr($1.location, $2);
		  pstate.unevaluated_expression--; }
	| sizeof '(' typename ')'
		{ $$ = make_sizeof_type($1.location, $3);
		  pstate.unevaluated_expression--; }
	| alignof unary_expr
		{ $$ = make_alignof_expr($1.location, $2);
		  pstate.unevaluated_expression--; }
	| alignof '(' typename ')'
		{ $$ = make_alignof_type($1.location, $3); 
		  pstate.unevaluated_expression--; }
	;

sizeof:
	SIZEOF { pstate.unevaluated_expression++; $$ = $1; }
	;

alignof:
	ALIGNOF { pstate.unevaluated_expression++; $$ = $1; }
	;

cast_expr:
	unary_expr
	| '(' typename ')' cast_expr
	  	{ $$ = make_cast($1.location, $2, $4); }
	| '(' typename ')' '{' 
		{ 
#if 0
		  start_init (NULL, NULL, 0);
		  $2 = groktypename ($2);
		  really_start_incremental_init ($2); 
#endif
		}
	  initlist_maybe_comma '}'
		{ 
		  $$ = CAST(expression, new_cast_list(pr, $1.location, $2, CAST(expression, new_init_list(pr, $6->location, $6))));
		  $$->type = $2->type;
		  $$->lvalue = TRUE;
		  /* XXX: Evil hack for foo((int[5]) {1, 2, 3}) */
		  /* XXX: what does gcc do ? */
		  /* XXX: why on earth does gcc consider this an lvalue ?
		     (see cparser/tests/addr1.c) */
		  if (type_array($$->type))
		    $$->lvalue = TRUE;

		  if (pedantic)
		    pedwarn("ANSI C forbids constructor expressions");
#if 0
		  char *name;
		  tree result = pop_init_level (0);
		  tree type = $2;
		  finish_init ();

		  if (TYPE_NAME (type) != 0)
		    {
		      if (TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE)
			name = IDENTIFIER_POINTER (TYPE_NAME (type));
		      else
			name = IDENTIFIER_POINTER (DECL_NAME (TYPE_NAME (type)));
		    }
		  else
		    name = "";
		  $$ = result;
		  if (TREE_CODE (type) == ARRAY_TYPE && TYPE_SIZE (type) == 0)
		    {
		      int failure = complete_array_type (type, $$, 1);
		      if (failure)
			abort ();
		    }
#endif
		}
	;

expr_no_commas:
	  cast_expr
	| expr_no_commas '+' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_plus, $1, $3); }
	| expr_no_commas '-' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_minus, $1, $3); }
	| expr_no_commas '*' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_times, $1, $3); }
	| expr_no_commas '/' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_divide, $1, $3); }
	| expr_no_commas '%' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_modulo, $1, $3); }
	| expr_no_commas LSHIFT expr_no_commas
	    	{ $$ = make_binary($2.location, kind_lshift, $1, $3); }
	| expr_no_commas RSHIFT expr_no_commas
	    	{ $$ = make_binary($2.location, kind_rshift, $1, $3); }
	| expr_no_commas ARITHCOMPARE expr_no_commas
	    	{ $$ = make_binary($2.location, $2.i, $1, $3); }
	| expr_no_commas EQCOMPARE expr_no_commas
	    	{ $$ = make_binary($2.location, $2.i, $1, $3); }
	| expr_no_commas '&' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_bitand, $1, $3); }
	| expr_no_commas '|' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_bitor, $1, $3); }
	| expr_no_commas '^' expr_no_commas
	    	{ $$ = make_binary($2.location, kind_bitxor, $1, $3); }
	| expr_no_commas ANDAND expr_no_commas
	    	{ $$ = make_binary($2.location, kind_andand, $1, $3); }
	| expr_no_commas OROR expr_no_commas
	    	{ $$ = make_binary($2.location, kind_oror, $1, $3); }
	| expr_no_commas '?' expr ':' expr_no_commas
	  	{ $$ = make_conditional($2.location, $1, $3, $5); }
	| expr_no_commas '?'
		{ if (pedantic)
		    pedwarn("ANSI C forbids omitting the middle term of a ?: expression"); 
		}
	  ':' expr_no_commas
	  	{ $$ = make_conditional($2.location, $1, NULL, $5); }
	| expr_no_commas '=' expr_no_commas
	    	{ $$ = make_assign($2.location, kind_assign, $1, $3); }
	| expr_no_commas ASSIGN expr_no_commas
	    	{ $$ = make_assign($2.location, $2.i, $1, $3); }
	;

primary:
	IDENTIFIER
		{ 
		  if (yychar == YYEMPTY)
		    yychar = YYLEX;
		  $$ = make_identifier($1.location, $1.id, yychar == '('); 
		}
	| CONSTANT { $$ = CAST(expression, $1); }
	| string { $$ = CAST(expression, $1); }
	| '(' expr ')'
		{ $$ = $2; }
	| '(' error ')'
		{ $$ = make_error_expr(last_location); }
	| '('
		{ if (current.function_decl == 0)
		    {
		      error("braced-group within expression allowed only inside a function");
		      YYERROR;
		    }
		    push_label_level();
		}
	  compstmt ')'
		{ 
		  pop_label_level();
		  if (pedantic)
		    pedwarn("ANSI C forbids braced-groups within expressions");
		  $$ = make_compound_expr($1.location, $3);
		}
	| function_call
		{
		  function_call fc = CAST(function_call, $1);
		  type calltype = fc->arg1->type;

		  if (type_command(calltype))
		    error("commands must be called with call");
		  else if (type_event(calltype))
		    error("events must be signaled with signal");
		  else if (type_task(calltype))
		    error("tasks must be posted with post");

		  $$ = $1;
		}
	| VA_ARG '(' expr_no_commas ',' typename ')'
		{ $$ = make_va_arg($1.location, $3, $5); }
	| OFFSETOF '(' typename ',' fieldlist ')'
		{ $$ = make_offsetof($1.location, $3, $5); }
	| primary '[' nonnull_exprlist ']' 
		{ $$ = make_array_ref($2.location, $1, $3); }
	| primary '.' identifier
		{ $$ = make_field_ref($2.location, $1, $3.id); }
	| primary POINTSAT identifier
		{ $$ = make_field_ref($2.location, make_dereference($2.location, $1),
				      $3.id); }
	| primary PLUSPLUS
		{ $$ = make_postincrement($2.location, $1); }
	| primary MINUSMINUS
		{ $$ = make_postdecrement($2.location, $1); }
	;

fieldlist:
	identifier { $$ = dd_new_list(pr); dd_add_last(pr, $$, $1.id.data); }
	| fieldlist '.' identifier { $$ = $1; dd_add_last(pr, $$, $3.id.data); }
	;

function_call: primary '(' exprlist ')'
	  	{ $$ = make_function_call($2.location, $1, $3); }
	;

/* Produces a STRING_CST with perhaps more STRING_CSTs chained onto it.  */
string:
	string_list { $$ = make_string($1->location, expression_reverse($1)); }
        ;

string_list:
	string_component { $$ = $1; }
	| string_list string_component
		{ $$ = expression_chain($2, $1); }
	;

string_component:
	STRING { $$ = CAST(expression, $1); }
	| MAGIC_STRING
	  { $$ = make_identifier($1.location, $1.id, FALSE);
	  }
	;


old_style_parm_decls:
	/* empty */ { $$ = NULL; }
	| datadecls
	| datadecls ELLIPSIS
		/* ... is used here to indicate a varargs function.  */
		{ if (pedantic)
		    pedwarn("ANSI C does not permit use of `varargs.h'"); 
		  $$ = declaration_chain(CAST(declaration, new_ellipsis_decl(pr, $2.location)), $1);
		}
	;

/* The following are analogous to decls and decl
   except that they do not allow nested functions.
   They are used for old-style parm decls.  */
datadecls:
	datadecl
	| datadecls datadecl { $$ = declaration_chain($2, $1); }
	;

/* We don't allow prefix attributes here because they cause reduce/reduce
   conflicts: we can't know whether we're parsing a function decl with
   attribute suffix, or function defn with attribute prefix on first old
   style parm.  */
datadecl:
	typed_declspecs setspecs initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
	| declmods setspecs notype_initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
	| typed_declspecs setspecs ';'
		{ shadow_tag_warned($1, 1);
		  $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, NULL));
		  pop_declspec_stack();
		  pedwarn("empty declaration"); }
	| declmods ';'
		{ pedwarn("empty declaration"); 
		  $$ = NULL; }
	;

/* This combination which saves a lineno before a decl
   is the normal thing to use, rather than decl itself.
   This is to avoid shift/reduce conflicts in contexts
   where statement labels are allowed.  */
decls:
	decl
	| errstmt { $$ = new_error_decl(pr, last_location); }
	| decls decl { $$ = declaration_chain($2, $1); }
	| decl errstmt { $$ = new_error_decl(pr, last_location); }
	;

/* records the type and storage class specs to use for processing
   the declarators that follow.
   Maintains a stack of outer-level values of pstate.declspecs,
   for the sake of parm declarations nested in function declarators.  */
setspecs: /* empty */
		{ 
		  push_declspec_stack();
		  pending_xref_error();
		  split_type_elements($<u.telement>0,
				      &pstate.declspecs, &pstate.prefix_attributes);
		}
	;

decl:
	typed_declspecs setspecs initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
	| declmods setspecs notype_initdecls ';'
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, $3));
		  pop_declspec_stack(); }
	| typed_declspecs setspecs nested_function
		{ $$ = $3;
		  pop_declspec_stack(); }
	| declmods setspecs notype_nested_function
		{ $$ = $3;
		  pop_declspec_stack(); }
	| typed_declspecs setspecs ';'
		{ shadow_tag($1);
		  $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, NULL));
		  pop_declspec_stack(); }
	| declmods ';'
		{ pedwarn("empty declaration"); }
	| extension decl
		{ pedantic = $1.i; 
		  $$ = CAST(declaration, new_extension_decl(pr, $1.location, $2)); }
	;

/* Declspecs which contain at least one type specifier or typedef name.
   (Just `const' or `volatile' is not enough.)
   A typedef'd name following these is taken as a name to be declared. */

typed_declspecs:
	  typespec reserved_declspecs
		{ $$ = $1; $1->next = CAST(node, $2); }
	| declmods typespec reserved_declspecs
		{ $$ = type_element_chain($1, $2); $2->next = CAST(node, $3); }
	;

reserved_declspecs:
	  /* empty */
		{ $$ = NULL; }
	| reserved_declspecs typespecqual_reserved
		{ $$ = type_element_chain($1, $2); }
	| reserved_declspecs scspec
		{ if (extra_warnings)
		    warning("`%s' is not at beginning of declaration",
			    rid_name(CAST(rid, $2)));
		  $$ = type_element_chain($1, $2); }
	;

/* List of just storage classes, type modifiers, and prefix attributes.
   A declaration can start with just this, but then it cannot be used
   to redeclare a typedef-name.
   Declspecs have a non-NULL TREE_VALUE, attributes do not.  */

declmods:
	  type_qual
	| scspec
	| declmods type_qual
		{ $$ = type_element_chain($1, $2); }
	| declmods scspec
		{ if (extra_warnings /*&& TREE_STATIC ($1)*/)
		    warning("`%s' is not at beginning of declaration",
			    rid_name(CAST(rid, $2)));
		  $$ = type_element_chain($1, $2); }
	;


/* Used instead of declspecs where storage classes are not allowed
   (that is, for typenames and structure components).
   Don't accept a typedef-name if anything but a modifier precedes it.  */

typed_typespecs:
	  typespec reserved_typespecquals
		{ $$ = $1; $1->next = CAST(node, $2); }
	| nonempty_type_quals typespec reserved_typespecquals
		{ $$ = type_element_chain($1, $2); $2->next = CAST(node, $3); }
	;

reserved_typespecquals:  /* empty */
		{ $$ = NULL; }
	| reserved_typespecquals typespecqual_reserved
		{ $$ = type_element_chain($1, $2); }
	;

/* A typespec (but not a type qualifier).
   Once we have seen one of these in a declaration,
   if a typedef name appears then it is being redeclared.  */

typespec: type_spec
	| structsp
	| TYPENAME
		{ /* For a typedef name, record the meaning, not the name.
		     In case of `foo foo, bar;'.  */
		  $$ = CAST(type_element, new_typename(pr, $1.location, $1.decl)); }
	| TYPEOF '(' expr ')'
		{ $$ = CAST(type_element, new_typeof_expr(pr, $1.location, $3)); }
	| TYPEOF '(' typename ')'
		{ $$ = CAST(type_element, new_typeof_type(pr, $1.location, $3)); }
	;

/* A typespec that is a reserved word, or a type qualifier.  */

typespecqual_reserved: type_spec
	| type_qual
	| structsp
	;

initdecls:
	initdecls_ { $$ = declaration_reverse($1); }
	;

notype_initdecls:
	notype_initdecls_ { $$ = declaration_reverse($1); }
	;

initdecls_:
	initdcl
	| initdecls_ ',' initdcl { $$ = declaration_chain($3, $1); }
	;

notype_initdecls_:
	notype_initdcl { $$ = $1; }
	| notype_initdecls_ ',' initdcl { $$ = declaration_chain($3, $1); }
	;

maybeasm:
	  /* empty */
		{ $$ = NULL; }
	| ASM_KEYWORD '(' string ')'
		{ $$ = new_asm_stmt(pr, $1.location, CAST(expression, $3),
				    NULL, NULL, NULL, NULL); }
	;

initdcl:
	  declarator maybeasm maybe_attribute '='
		{ $<u.decl>$ = start_decl($1, $2, pstate.declspecs, 1,
					$3, pstate.prefix_attributes); }
	  init
/* Note how the declaration of the variable is in effect while its init is parsed! */
		{ $$ = finish_decl($<u.decl>5, $6); }
	| declarator maybeasm maybe_attribute
		{ declaration d = start_decl($1, $2, pstate.declspecs, 0,
					     $3, pstate.prefix_attributes);
		  $$ = finish_decl(d, NULL); }
	;

notype_initdcl:
	  notype_declarator maybeasm maybe_attribute '='
		{ $<u.decl>$ = start_decl($1, $2, pstate.declspecs, 1,
					 $3, pstate.prefix_attributes); }
	  init
/* Note how the declaration of the variable is in effect while its init is parsed! */
		{ $$ = finish_decl($<u.decl>5, $6); }
	| notype_declarator maybeasm maybe_attribute
		{ declaration d = start_decl($1, $2, pstate.declspecs, 0,
					     $3, pstate.prefix_attributes);
		  $$ = finish_decl(d, NULL); }
	;
maybe_attribute:
	/* empty */
  		{ $$ = NULL; }
	| attributes
		{ $$ = attribute_reverse($1); }
	;
 
attributes:
	attribute
		{ $$ = $1; }
	| attributes attribute
		{ $$ = attribute_chain($2, $1); }
	;

attribute:
      ATTRIBUTE '(' '(' attribute_list ')' ')'
		{ $$ = attribute_reverse($4); }
	;

attribute_list:
	attrib
		{ $$ = $1; }
	| attribute_list ',' attrib
		{ $$ = attribute_chain($3, $1); }
	;
 
attrib:
	/* empty */
		{ $$ = NULL; }
	| any_word
		{ $$ = new_attribute(pr, $1->location, $1, NULL, NULL); }
	| any_word '(' IDENTIFIER ')'
		{ $$ = new_attribute
		    (pr, $1->location, $1, new_word(pr, $3.location, $3.id), NULL); }
	| any_word '(' IDENTIFIER ',' nonnull_exprlist ')'
		{ $$ = new_attribute
		    (pr, $2.location, $1, new_word(pr, $3.location, $3.id), $5);
		}
	;

/* This still leaves out most reserved keywords,
   shouldn't we include them?  */

any_word:
	  idword
	| scspec { $$ = new_word(pr, $1->location, str2cstring(pr, rid_name(CAST(rid, $1)))); }
	| type_spec { $$ = new_word(pr, $1->location, str2cstring(pr, rid_name(CAST(rid, $1)))); }
	| type_qual { $$ = new_word(pr, $1->location, str2cstring(pr, qualifier_name(CAST(qualifier, $1)->id))); }
	| SIGNAL { $$ = new_word(pr, $1.location, str2cstring(pr, "signal")); }
	;

/* Initializers.  `init' is the entry point.  */

init:
	expr_no_commas
	| '{'
	  initlist_maybe_comma '}'
		{ $$ = CAST(expression, new_init_list(pr, $1.location, $2)); 
		  $$->type = error_type; }
	| error
		{ $$ = make_error_expr(last_location); }
	;

/* `initlist_maybe_comma' is the guts of an initializer in braces.  */
initlist_maybe_comma:
	  /* empty */
		{ if (pedantic)
		    pedwarn("ANSI C forbids empty initializer braces"); 
		  $$ = NULL; }
	| initlist1 maybecomma { $$ = expression_reverse($1); }
	;

initlist1:
	  initelt
	| initlist1 ',' initelt { $$ = expression_chain($3, $1); }
	;

/* `initelt' is a single element of an initializer.
   It may use braces.  */
initelt:
	expr_no_commas { $$ = $1; }
	| '{' initlist_maybe_comma '}'
		{ $$ = CAST(expression, new_init_list(pr, $1.location, $2)); }
	| error { $$ = make_error_expr(last_location); }
	/* These are for labeled elements.  The syntax for an array element
	   initializer conflicts with the syntax for an Objective-C message,
	   so don't include these productions in the Objective-C grammar.  */
	| '[' expr_no_commas ELLIPSIS expr_no_commas ']' '=' initelt
	    	{ $$ = CAST(expression, new_init_index(pr, $1.location, $2, $4, $7)); }
	| '[' expr_no_commas ']' '=' initelt
	    	{ $$ = CAST(expression, new_init_index(pr, $1.location, $2, NULL, $5)); }
	| '[' expr_no_commas ']' initelt
	    	{ $$ = CAST(expression, new_init_index(pr, $1.location, $2, NULL, $4)); }
	| idword ':' initelt
	    	{ $$ = CAST(expression, new_init_field(pr, $1->location, $1, $3)); }
	| '.' idword '=' initelt
	    	{ $$ = CAST(expression, new_init_field(pr, $1.location, $2, $4)); }
	;

nested_function:
	  declarator
		{ if (!start_function(pstate.declspecs, $1,
				      pstate.prefix_attributes, 1))
		    {
		      YYERROR1;
		    }
		  }
	   old_style_parm_decls
		{ store_parm_decls(declaration_reverse($3)); }
/* This used to use compstmt_or_error.
   That caused a bug with input `f(g) int g {}',
   where the use of YYERROR1 above caused an error
   which then was handled by compstmt_or_error.
   There followed a repeated execution of that same rule,
   which called YYERROR1 again, and so on.  */
	  compstmt
		{ $$ = finish_function($5); }
	;

notype_nested_function:
	  notype_declarator
		{ if (!start_function(pstate.declspecs, $1,
				      pstate.prefix_attributes, 1))
		    {
		      YYERROR1;
		    }
		}
	  old_style_parm_decls
		{ store_parm_decls(declaration_reverse($3)); }
/* This used to use compstmt_or_error.
   That caused a bug with input `f(g) int g {}',
   where the use of YYERROR1 above caused an error
   which then was handled by compstmt_or_error.
   There followed a repeated execution of that same rule,
   which called YYERROR1 again, and so on.  */
	  compstmt
		{ $$ = finish_function($5); }
	;

/* Any kind of declarator (thus, all declarators allowed
   after an explicit typespec).  */

declarator:
	  after_type_declarator
	| notype_declarator
	;

/* A declarator that is allowed only after an explicit typespec.  */

after_type_declarator:
	  '(' after_type_declarator ')'
		{ $$ = $2; }
	| after_type_declarator parameters '(' parmlist_or_identifiers_1 fn_quals
		{ $$ = make_function_declarator($3.location, $1, $4, $5, $2); }
	| after_type_declarator '(' parmlist_or_identifiers fn_quals
		{ $$ = make_function_declarator($2.location, $1, $3, $4, NULL); }
	| after_type_declarator '[' expr ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, $3)); }
	| after_type_declarator '[' ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, NULL)); }
	| '*' type_quals after_type_declarator
		{ $$ = CAST(declarator, new_pointer_declarator(pr, $1.location, $3, $2)); }
	| TYPENAME { $$ = CAST(declarator, new_identifier_declarator(pr, $1.location, $1.id)); }
	| TYPENAME '.' identifier 
		{
		  $$ = make_interface_ref_declarator($1.location, $1.id, $3.id);
		}
	;

/* Kinds of declarator that can appear in a parameter list
   in addition to notype_declarator.  This is like after_type_declarator
   but does not allow a typedef name in parentheses as an identifier
   (because it would conflict with a function with that typedef as arg).  */

parm_declarator:
	  parm_declarator '(' parmlist_or_identifiers fn_quals
		{ $$ = make_function_declarator($2.location, $1, $3, $4, NULL); }
	| parm_declarator '[' expr ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, $3)); }
	| parm_declarator '[' ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, NULL)); }
	| '*' type_quals parm_declarator
		{ $$ = CAST(declarator, new_pointer_declarator(pr, $1.location, $3, $2)); }
	| TYPENAME { $$ = CAST(declarator, new_identifier_declarator(pr, $1.location, $1.id)); }
	;

/* A declarator allowed whether or not there has been
   an explicit typespec.  These cannot redeclare a typedef-name.  */

notype_declarator:
	  notype_declarator parameters '(' parmlist_or_identifiers_1 fn_quals
		{ $$ = make_function_declarator($3.location, $1, $4, $5, $2); }
	| notype_declarator '(' parmlist_or_identifiers fn_quals
		{ $$ = make_function_declarator($2.location, $1, $3, $4, NULL); }
	| '(' notype_declarator ')'
		{ $$ = $2; }
	| '*' type_quals notype_declarator
		{ $$ = CAST(declarator, new_pointer_declarator(pr, $1.location, $3, $2)); }
	| notype_declarator '[' expr ']' 
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, $3)); }
	| notype_declarator '[' ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, NULL)); }
	| IDENTIFIER { $$ = CAST(declarator, new_identifier_declarator(pr, $1.location, $1.id)); }
	| IDENTIFIER '.' identifier { }
		{
		  $$ = make_interface_ref_declarator($1.location, $1.id, $3.id);
		}
	;

tag:
	identifier { $$ = new_word(pr, $1.location, $1.id); }
	;

structsp:
	  STRUCT tag '{'
		{ $$ = start_struct($1.location, kind_struct_ref, $2);
		  /* Start scope of tag before parsing components.  */
		}
	  component_decl_list '}' maybe_attribute 
		{ $$ = finish_struct($<u.telement>4, $5, $7); }
	| STRUCT '{' component_decl_list '}' maybe_attribute
		{ $$ = finish_struct(start_struct($1.location, kind_struct_ref, NULL),
				     $3, $5);
		}
	| STRUCT tag
		{ $$ = xref_tag($1.location, kind_struct_ref, $2); }
	| UNION tag '{'
		{ $$ = start_struct ($1.location, kind_union_ref, $2); }
	  component_decl_list '}' maybe_attribute
		{ $$ = finish_struct($<u.telement>4, $5, $7); }
	| UNION '{' component_decl_list '}' maybe_attribute
		{ $$ = finish_struct(start_struct($1.location, kind_union_ref, NULL),
				     $3, $5);
		}
	| UNION tag
		{ $$ = xref_tag($1.location, kind_union_ref, $2); }
	| ENUM tag '{'
		{ $$ = start_enum($1.location, $2); }
	  enumlist maybecomma_warn '}' maybe_attribute
		{ $$ = finish_enum($<u.telement>4, declaration_reverse($5), $8); }
	| ENUM '{'
		{ $$ = start_enum($1.location, NULL); }
	  enumlist maybecomma_warn '}' maybe_attribute
		{ $$ = finish_enum($<u.telement>3, declaration_reverse($4), $7); }
	| ENUM tag
		{ $$ = xref_tag($1.location, kind_enum_ref, $2); }
	;

maybecomma:
	  /* empty */
	| ','
	;

maybecomma_warn:
	  /* empty */
	| ','
		{ if (pedantic) pedwarn("comma at end of enumerator list"); }
	;

component_decl_list:
	  component_decl_list2
		{ $$ = declaration_reverse($1); }
	| component_decl_list2 component_decl
		{ $$ = declaration_reverse(declaration_chain($2, $1));
		  pedwarn("no semicolon at end of struct or union"); }
	;

component_decl_list2:	/* empty */
		{ $$ = NULL; }
	| component_decl_list2 component_decl ';'
		{ $$ = declaration_chain($2, $1); }
	| component_decl_list2 ';'
		{ if (pedantic)
		    pedwarn("extra semicolon in struct or union specified"); 
		   $$ = $1; }
	;

/* There is a shift-reduce conflict here, because `components' may
   start with a `typename'.  It happens that shifting (the default resolution)
   does the right thing, because it treats the `typename' as part of
   a `typed_typespecs'.

   It is possible that this same technique would allow the distinction
   between `notype_initdecls' and `initdecls' to be eliminated.
   But I am being cautious and not trying it.  */

component_decl:
	  typed_typespecs setspecs components
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, declaration_reverse($3)));
		  pop_declspec_stack(); }
	| typed_typespecs setspecs
		{ if (pedantic)
		    pedwarn("ANSI C forbids member declarations with no members");
		  shadow_tag($1);
		  $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, NULL));
		  pop_declspec_stack(); }
	| nonempty_type_quals setspecs components
		{ $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, declaration_reverse($3)));
		  pop_declspec_stack(); }
	| nonempty_type_quals setspecs
		{ if (pedantic)
		    pedwarn("ANSI C forbids member declarations with no members");
		  shadow_tag($1);
		  $$ = CAST(declaration, new_data_decl(pr, $1->location, pstate.declspecs, pstate.prefix_attributes, NULL));	
		  pop_declspec_stack(); }
	| error
		{ $$ = new_error_decl(pr, last_location); }
	| extension component_decl
		{ pedantic = $<u.itoken>1.i;
		  $$ = CAST(declaration, new_extension_decl(pr, $1.location, $2)); }
	;

components:
	  component_declarator
	| components ',' component_declarator
		{ $$ = declaration_chain($3, $1); }
	;

component_declarator:
	  declarator maybe_attribute
		{ $$ = make_field($1, NULL, pstate.declspecs,
				  $2, pstate.prefix_attributes); }
	| declarator ':' expr_no_commas maybe_attribute
		{ $$ = make_field($1, $3, pstate.declspecs,
				  $4, pstate.prefix_attributes); }
	| ':' expr_no_commas maybe_attribute
		{ $$ = make_field(NULL, $2, pstate.declspecs,
				  $3, pstate.prefix_attributes); }
	;

enumlist:
	  enumerator
	| enumlist ',' enumerator
		{ $$ = declaration_chain($3, $1); }
	| error
		{ $$ = NULL; }
	;


enumerator:
	  identifier
	  	{ $$ = make_enumerator($1.location, $1.id, NULL); }
	| identifier '=' expr_no_commas
	  	{ $$ = make_enumerator($1.location, $1.id, $3); }
	;

typename:
	typed_typespecs absdcl
		{ $$ = make_type($1, $2); }
	| nonempty_type_quals absdcl
		{ $$ = make_type($1, $2); }
	;

absdcl:   /* an abstract declarator */
	/* empty */
		{ $$ = NULL; }
	| absdcl1
	;

nonempty_type_quals:
	  type_qual
	| nonempty_type_quals type_qual
		{ $$ = type_element_chain($1, $2); }
	;

type_quals:
	  /* empty */
		{ $$ = NULL; }
	| type_quals type_qual
		{ $$ = type_element_chain($1, $2); }
	;

absdcl1:  /* a nonempty abstract declarator */
	  '(' absdcl1 ')'
		{ $$ = $2; }
	  /* `(typedef)1' is `int'.  */
	| '*' type_quals absdcl1
		{ $$ = CAST(declarator, new_pointer_declarator(pr, $1.location, $3, $2)); }
	| '*' type_quals
		{ $$ = CAST(declarator, new_pointer_declarator(pr, $1.location, NULL, $2)); }
	| absdcl1 '(' parmlist fn_quals
		{ $$ = make_function_declarator($2.location, $1, $3, $4, NULL); }
	| absdcl1 '[' expr ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, $3)); }
	| absdcl1 '[' ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $2.location, $1, NULL)); }
	| '(' parmlist fn_quals
		{ $$ = make_function_declarator($1.location, NULL, $2, $3, NULL); }
	| '[' expr ']'
		{ $$ = CAST(declarator, new_array_declarator(pr, $1.location, NULL, $2)); }
	| '[' ']' 
		{ $$ = CAST(declarator, new_array_declarator(pr, $1.location, NULL, NULL)); }
	/* ??? It appears we have to support attributes here, however
	   using pstate.prefix_attributes is wrong.  */
	;

/* at least one statement, the first of which parses without error.  */
/* stmts is used only after decls, so an invalid first statement
   is actually regarded as an invalid decl and part of the decls.  */

stmts:
	stmt_or_labels
		{
		  if (pedantic && $1.i)
		    pedwarn("ANSI C forbids label at end of compound statement");
		  /* Add an empty statement to last label if stand-alone */
		  if ($1.i)
		    {
		      statement last_label = CAST(statement, last_node(CAST(node, $1.stmt)));

		      chain_with_labels(last_label, CAST(statement, new_empty_stmt(pr, last_label->location)));
		    }
		  $$ = $1.stmt;
		}
	;

stmt_or_labels:
	  stmt_or_label
	| stmt_or_labels stmt_or_label
		{ $$.i = $2.i; $$.stmt = chain_with_labels($1.stmt, $2.stmt); }
	| stmt_or_labels errstmt
		{ $$.i = 0; $$.stmt = new_error_stmt(pr, last_location); }
	;

xstmts:
	/* empty */ { $$ = NULL; }
	| stmts
	;

errstmt:  error ';'
	;

pushlevel:  /* empty */
		{ pushlevel(FALSE); }
	;

/* Read zero or more forward-declarations for labels
   that nested functions can jump to.  */
maybe_label_decls:
	  /* empty */ { $$ = NULL; }
	| label_decls
		{ if (pedantic)
		    pedwarn("ANSI C forbids label declarations"); 
		  $$ = id_label_reverse($1); }
	;

label_decls:
	  label_decl
	| label_decls label_decl { $$ = id_label_chain($2, $1); }
	;

label_decl:
	  LABEL identifiers_or_typenames ';'
		{ $$ = $2; }
	;

/* This is the body of a function definition.
   It causes syntax errors to ignore to the next openbrace.  */
compstmt_or_error:
	  compstmt
	| error compstmt { $$ = $2; }
	;

compstmt_start: '{' { $$ = $1; compstmt_count++; }
        ;

compstmt: compstmt_start pushlevel '}'
		{ $$ = CAST(statement, new_compound_stmt(pr, $1.location, NULL, NULL, NULL, poplevel())); }
	| compstmt_start pushlevel maybe_label_decls decls xstmts '}'
		{ $$ = CAST(statement, new_compound_stmt(pr, $1.location, $3,
		    declaration_reverse($4), $5, poplevel())); }
	| compstmt_start pushlevel maybe_label_decls error '}'
		{ poplevel();
		  $$ = new_error_stmt(pr, last_location); }
	| compstmt_start pushlevel maybe_label_decls stmts '}'
		{ $$ = CAST(statement, new_compound_stmt(pr, $1.location, $3, NULL, $4, poplevel())); }
	;

/* Value is number of statements counted as of the closeparen.  */
simple_if:
	  if_prefix labeled_stmt
		{ $$.stmt = CAST(statement, new_if_stmt(pr, $1.expr->location, $1.expr, $2, NULL));
		  $$.i = $1.i; }
	| if_prefix error { $$.i = $1.i; $$.stmt = new_error_stmt(pr, last_location); }
	;

if_prefix:
	  IF '(' expr ')'
		{ $$.i = stmt_count;
		  $$.expr = $3;
		  check_condition("if", $3); }
	;

/* This is a subroutine of stmt.
   It is used twice, once for valid DO statements
   and once for catching errors in parsing the end test.  */
do_stmt_start:
	  DO
		{ stmt_count++;
		  compstmt_count++; 
		  $<u.cstmt>$ = CAST(conditional_stmt,
				   new_dowhile_stmt(pr, $1.location, NULL, NULL));
		 push_loop(CAST(statement, $<u.cstmt>$)); }
	  labeled_stmt WHILE
		{ $$ = $<u.cstmt>2; 
		  $$->stmt = $3; }
	;

labeled_stmt:
	  stmt
		{ $$ = $1; }
	| label labeled_stmt
		{ $$ = CAST(statement, new_labeled_stmt(pr, $1->location, $1, $2)); }
	;

stmt_or_label:
	  stmt
		{ $$.i = 0; $$.stmt = $1; }
	| label
		{ $$.i = 1; $$.stmt = CAST(statement, new_labeled_stmt(pr, $1->location, $1, NULL)); }
	;

/* Parse a single real statement, not including any labels.  */
stmt:
	  compstmt
		{ stmt_count++; $$ = $1; }
	| expr ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_expression_stmt(pr, $1->location, $1)); }
	| simple_if ELSE
		{ $1.i = stmt_count; }
	  labeled_stmt
		{ if (extra_warnings && stmt_count == $1.i)
		    warning("empty body in an else-statement");
		  $$ = $1.stmt;
		  CAST(if_stmt, $$)->stmt2 = $4;
		}
	| simple_if %prec IF
		{ /* This warning is here instead of in simple_if, because we
		     do not want a warning if an empty if is followed by an
		     else statement.  Increment stmt_count so we don't
		     give a second error if this is a nested `if'.  */
		  if (extra_warnings && stmt_count++ == $1.i)
		    warning_with_location ($1.stmt->location,
					   "empty body in an if-statement");
		  $$ = $1.stmt; }
	| simple_if ELSE error
		{ $$ = new_error_stmt(pr, last_location); }
	| WHILE
		{ stmt_count++; }
	  '(' expr ')' 
	        { check_condition("while", $4); 
		  $<u.cstmt>$ = CAST(conditional_stmt,
			           new_while_stmt(pr, $1.location, $4, NULL));
		  /* The condition is not "in the loop" for break or continue */
		  push_loop(CAST(statement, $<u.cstmt>$)); }
	  labeled_stmt
		{ $$ = CAST(statement, $<u.cstmt>6);
		  $<u.cstmt>6->stmt = $7; 
		  pop_loop(); }
	| do_stmt_start '(' expr ')' ';'
		{ $$ = CAST(statement, $1);
		  $1->condition = $3;
		  check_condition("do-while", $3); 
		  /* Note that pop_loop should be before the expr to be consistent
		     with while, but GCC is inconsistent. See loop1.c */
		  pop_loop(); }
	| do_stmt_start error
		{ $$ = new_error_stmt(pr, last_location); 
		  pop_loop(); }
	| FOR '(' xexpr ';' { stmt_count++; }
		xexpr ';' { if ($6) check_condition("for", $6); }
		xexpr ')' 
		{ $<u.for_stmt>$ = new_for_stmt(pr, $1.location, $3, $6, $9, NULL);
		  push_loop(CAST(statement, $<u.for_stmt>$)); }
		labeled_stmt
		{ $$ = CAST(statement, $<u.for_stmt>11);
		  $<u.for_stmt>11->stmt = $12; 
		  pop_loop(); }
	| SWITCH '(' expr ')'
	        { stmt_count++; check_switch($3); 
		  $<u.cstmt>$ = CAST(conditional_stmt,
			           new_switch_stmt(pr, $1.location, $3, NULL)); 
		  push_loop(CAST(statement, $<u.cstmt>$)); } 
	  labeled_stmt
		{ $$ = CAST(statement, $<u.cstmt>5); 
		  $<u.cstmt>5->stmt = $6;
		  pop_loop(); }
	| BREAK ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_break_stmt(pr, $1.location));
		  check_break($$);
		}
	| CONTINUE ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_continue_stmt(pr, $1.location));
		  check_continue($$);
		}
	| RETURN ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_return_stmt(pr, $1.location, NULL)); 
		  check_void_return(); }
	| RETURN expr ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_return_stmt(pr, $1.location, $2)); 
		  check_return($2); }
	| ASM_KEYWORD maybe_type_qual '(' expr ')' ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_asm_stmt(pr, $1.location, $4, NULL,
					       NULL, NULL, $2)); }
	/* This is the case with just output operands.  */
	| ASM_KEYWORD maybe_type_qual '(' expr ':' asm_operands ')' ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_asm_stmt(pr, $1.location, $4, $6, NULL,
					       NULL, $2)); }
	/* This is the case with input operands as well.  */
	| ASM_KEYWORD maybe_type_qual '(' expr ':' asm_operands ':' asm_operands ')' ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_asm_stmt(pr, $1.location, $4, $6, $8, NULL, $2)); }
	/* This is the case with clobbered registers as well.  */
	| ASM_KEYWORD maybe_type_qual '(' expr ':' asm_operands ':'
  	  asm_operands ':' asm_clobbers ')' ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_asm_stmt(pr, $1.location, $4, $6, $8, $10, $2)); }
	| GOTO id_label ';'
		{ stmt_count++;
		  $$ = CAST(statement, new_goto_stmt(pr, $1.location, $2));
		  use_label($2);
		}
	| GOTO '*' expr ';'
		{ if (pedantic)
		    pedwarn("ANSI C forbids `goto *expr;'");
		  stmt_count++;
		  $$ = CAST(statement, new_computed_goto_stmt(pr, $1.location, $3)); 
		  check_computed_goto($3); }
	| ';' { $$ = CAST(statement, new_empty_stmt(pr, $1.location)); }
	;

/* Any kind of label, including jump labels and case labels.
   ANSI C accepts labels only before statements, but we allow them
   also at the end of a compound statement.  */

label:	  CASE expr_no_commas ':'
		{ $$ = CAST(label, new_case_label(pr, $1.location, $2, NULL)); 
		  check_case($$); }
	| CASE expr_no_commas ELLIPSIS expr_no_commas ':'
		{ $$ = CAST(label, new_case_label(pr, $1.location, $2, $4)); 
		  check_case($$); }
	| DEFAULT ':'
		{ $$ = CAST(label, new_default_label(pr, $1.location)); 
		  check_default($$); }
	| id_label ':'
		{ $$ = CAST(label, $1); 
		  define_label($1); }
	;

/* Either a type-qualifier or nothing.  First thing in an `asm' statement.  */

maybe_type_qual:
	/* empty */
		{ $$ = NULL; }
	| type_qual
	;

xexpr:
	/* empty */
		{ $$ = NULL; }
	| expr
	;

/* These are the operands other than the first string and colon
   in  asm ("addextend %2,%1": "=dm" (x), "0" (y), "g" (*x))  */
asm_operands: /* empty */
		{ $$ = NULL; }
	| nonnull_asm_operands
	;

nonnull_asm_operands:
	  asm_operand
	| nonnull_asm_operands ',' asm_operand
		{ $$ = asm_operand_chain($1, $3); }
	;

asm_operand:
	  STRING '(' expr ')'
		{ $$ = new_asm_operand(pr, $1->location,
				       make_string($1->location, CAST(expression, $1)),
				       $3);  }
	;

asm_clobbers:
	  string
		{ $$ = $1; }
	| asm_clobbers ',' string
		{ $$ = string_chain($1, $3); }
	;

/* This is what appears inside the parens in a function declarator.
   Its value is a list of ..._TYPE nodes.  */
parmlist:
		{ pushlevel(TRUE); }
	  parmlist_1
		{ $$ = $2;
		  /* poplevel() is done when building the declarator */
		}
	;

parmlist_1:
	  parmlist_2 ')' { $$ = $1; }
	| parms ';'
		{ if (pedantic)
		    pedwarn("ANSI C forbids forward parameter declarations");
		  mark_forward_parameters($1);
		}
	  parmlist_1
		{ $$ = declaration_chain($1, $4); }
	| error ')'
		{ $$ = new_error_decl(pr, last_location); }
	;

/* This is what appears inside the parens in a function declarator.
   Is value is represented in the format that grokdeclarator expects.  */
parmlist_2:  /* empty */
		{ $$ = NULL; }
	| ELLIPSIS
		{ $$ = new_error_decl(pr, last_location);
		  /* Gcc used to allow this as an extension.  However, it does
		     not work for all targets, and thus has been disabled.
		     Also, since func (...) and func () are indistinguishable,
		     it caused problems with the code in expand_builtin which
		     tries to verify that BUILT_IN_NEXT_ARG is being used
		     correctly.  */
		  error("ANSI C requires a named argument before `...'");
		}
	| parms
		{ $$ = $1; }
	| parms ',' ELLIPSIS
		{ $$ = declaration_chain($1, CAST(declaration, new_ellipsis_decl(pr, $3.location))); }
	;

parms:
	parm
	| parms ',' parm
		{ $$ = declaration_chain($1, $3); }
	;

/* A single parameter declaration or parameter type name,
   as found in a parmlist.  */
parm:
	  typed_declspecs setspecs parm_declarator maybe_attribute
		{ $$ = declare_parameter($3, pstate.declspecs, $4,
					 pstate.prefix_attributes, FALSE);
		  pop_declspec_stack(); }
	| typed_declspecs setspecs notype_declarator maybe_attribute
		{ $$ = declare_parameter($3, pstate.declspecs, $4,
					 pstate.prefix_attributes, FALSE);
		  pop_declspec_stack(); }
	| typed_declspecs setspecs absdcl
		{ $$ = declare_parameter($3, pstate.declspecs, NULL,
					 pstate.prefix_attributes, FALSE);
		pop_declspec_stack(); }
	| declmods setspecs notype_declarator maybe_attribute
		{ $$ = declare_parameter($3, pstate.declspecs, $4,
					 pstate.prefix_attributes, FALSE);
		  pop_declspec_stack(); }
	| declmods setspecs absdcl
		{ $$ = declare_parameter($3, pstate.declspecs, NULL,
					 pstate.prefix_attributes, FALSE);
		  pop_declspec_stack(); }
	;

/* This is used in a function definition
   where either a parmlist or an identifier list is ok.
   Its value is a list of ..._TYPE nodes or a list of identifiers.  */
parmlist_or_identifiers:
		{ pushlevel(TRUE); }
	  parmlist_or_identifiers_1
		{ $$ = $2;
		  /* poplevel is done when building the declarator */ }
	;

parmlist_or_identifiers_1:
	  parmlist_1
	| identifiers ')' { $$ = $1; }
	;

/* A nonempty list of identifiers.  */
identifiers:
	old_parameter
		{ $$ = $1; }
	| identifiers ',' old_parameter
		{ $$ = declaration_chain($1, $3); }
	;

old_parameter:
	IDENTIFIER { $$ = declare_old_parameter($1.location, $1.id); }
	;

/* A nonempty list of identifiers, including typenames.  */
identifiers_or_typenames:
	id_label { $$ = $1; declare_label($1); }
	| identifiers_or_typenames ',' id_label
		{ $$ = id_label_chain($3, $1);
		  declare_label($3); }
	;

/* A possibly empty list of function qualifiers (only one exists so far) */
fn_quals:
	/* empty */ { $$ = NULL; }
	| fn_qual { $$ = $1; }
	;

extension:
	EXTENSION
		{ $$.location = $1.location;
		  $$.i = pedantic;
		  pedantic = 0; }
	;

scspec:
	SCSPEC { $$ = CAST(type_element, new_rid(pr, $1.location, $1.i)); }
	| DEFAULT { $$ = CAST(type_element, new_rid(pr, $1.location, RID_DEFAULT)); }
	;

type_qual:
	TYPE_QUAL { $$ = CAST(type_element, new_qualifier(pr, $1.location, $1.i)); }
	;

fn_qual:
	FN_QUAL { $$ = CAST(type_element, new_qualifier(pr, $1.location, $1.i)); }
	;

type_spec:
	TYPESPEC { $$ = CAST(type_element, new_rid(pr, $1.location, $1.i)); }
	;


%%