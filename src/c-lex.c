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
#include <stdio.h>
#include <setjmp.h>

#include "c-lex.h"
#include "c-parse.h"
#include "c-parse.tab.h"
#include "input.h"
#include "semantics.h"
#include "constants.h"

#include <ctype.h>

/* MULTIBYTE_CHARS support only works for native compilers.
   ??? Ideally what we want is to model widechar support after
   the current floating point support.  */
#ifdef CROSS_COMPILE
#undef MULTIBYTE_CHARS
#endif

#ifdef MULTIBYTE_CHARS
#include "mbchar.h"
#include <stdlib.h>
#include <locale.h>
#endif

static int max_char_length = 1; /* Default value if not MULTIBYTE_CHARS */

/* Location of last token. Used for location of error nodes */
location last_location;

location dummy_location;

static size_t int_type_size;

/* Cause the `yydebug' variable to be defined.  */
#define YYDEBUG 1

#define UNGETC(c) ungetc (c, input_file_stack->lex.finput)

int GETC()
{
  /* Nuke CR from CR-LF */
  int c = getc (input_file_stack->lex.finput);

  if (c != '\r')
    return c;

  c = getc (input_file_stack->lex.finput);
  if (c == '\n')
    return c;

  UNGETC(c);

  return '\r';
}

/* the declaration found for the last IDENTIFIER token read in.
   yylex must look this up to detect typedefs, which get token type TYPENAME,
   so it is left around in case the identifier is not a typedef but is
   used in a context which makes it a reference to a variable.  */
/*tree lastiddecl;*/

extern int yydebug;

static int maxtoken;		/* Current nominal length of token buffer.  */
static char *traditional token_buffer;	/* Pointer to token buffer.
				   Actual allocated length is maxtoken + 2. */

static wchar_array string_array;

static int language_token;

static char *extend_token_buffer(char *);
int check_newline(void);

/* Do not insert generated code into the source, instead, include it.
   This allows us to build gcc automatically even for targets that
   need to add or modify the reserved keyword lists.  */
#include "c-gperf.h"

void
init_lex (void)
{
  dummy_location.filename = "<dummy>";

#ifdef MULTIBYTE_CHARS
  /* Change to the native locale for multibyte conversions.  */
  setlocale (LC_CTYPE, "");
  literal_codeset = getenv ("LANG");
  max_char_length = local_mb_cur_max ();
#endif

  maxtoken = 40;
  token_buffer = (char *) xmalloc (maxtoken + 2);

  string_array = new_wchar_array(parse_region, 512);

  int_type_size = type_size(int_type);

  /* Some options inhibit certain reserved words.
     Clear those words out of the hash table so they won't be recognized.  */
#define UNSET_RESERVED_WORD(STRING) \
  do { struct resword *s = is_reserved_word (STRING, sizeof (STRING) - 1); \
       if (s) s->name = ""; } while (0)

  if (flag_traditional)
    {
      UNSET_RESERVED_WORD ("const");
      UNSET_RESERVED_WORD ("volatile");
      UNSET_RESERVED_WORD ("typeof");
      UNSET_RESERVED_WORD ("signed");
      UNSET_RESERVED_WORD ("inline");
      UNSET_RESERVED_WORD ("iterator");
      UNSET_RESERVED_WORD ("complex");
    }
  if (flag_no_asm)
    {
      UNSET_RESERVED_WORD ("asm");
      UNSET_RESERVED_WORD ("typeof");
      UNSET_RESERVED_WORD ("inline");
      UNSET_RESERVED_WORD ("iterator");
      UNSET_RESERVED_WORD ("complex");
    }

}

void
start_lex (source_language l)
{
  input_file_stack->lex.end_of_file = 0;
  input_file_stack->lex.nextchar = -1;
  input_file_stack->lex.indent_level = 0;
  language_token = -1;

  UNGETC(check_newline());

  switch (l)
    {
    case l_interface: language_token = DISPATCH_INTERFACE; break;
    case l_component: language_token = DISPATCH_COMPONENT; break;
    case l_c: language_token = DISPATCH_C; break;
    default: assert(0); break;
    }
}

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
#endif

/* Function used when yydebug is set, to print a token in more detail.  */

void
yyprint (file, yychar, yylval)
     FILE *file;
     int yychar;
     YYSTYPE yylval;
{
  switch (yychar)
    {
    case IDENTIFIER:
      fprintf(file, " '%s'", yylval.idtoken.id.data);
      break;
    }
}

/* If C is not whitespace, return C.
   Otherwise skip whitespace and return first nonwhite char read.  */

static void skip_c_comment(void)
{
  int last_c = 0, c;
  location start = input_file_stack->l;

  for (;;)
    {
      c = GETC();

      switch (c)
	{
	case EOF:
	  error_with_location(start, "unterminated comment");
	  return;
	case '\n':
	  input_file_stack->l.lineno++;
	  break;
	case '/':
	  if (last_c == '*')
	    return;
	}
      last_c = c;
    }
}

static void skip_cpp_comment(void)
{
  int c;

  for (;;)
    {
      c = GETC();

      switch (c)
	{
	case EOF:
	  return;
	case '\n':
	  input_file_stack->l.lineno++;
	  return;
	}
    }
}


static int
skip_white_space (c)
     int c;
{
  static int newline_warning = 0;
  int c1;

  for (;;)
    {
      switch (c)
	{
	case '/':
	  /* check for comments */
	  c1 = GETC();

	  if (c1 == '/')
	    skip_cpp_comment();
	  else if (c1 == '*')
	    skip_c_comment();
	  else
	    {
	      UNGETC(c1);
	      return c;
	    }
	  c = GETC();
	  break;

	case '\n':
	  c = check_newline ();
	  break;

	case ' ':
	case '\t':
	case '\f':
	case '\v':
	case '\b':
	  c = GETC();
	  break;

	case '\r':
	  /* ANSI C says the effects of a carriage return in a source file
	     are undefined.  */
	  if (pedantic && !newline_warning)
	    {
	      warning ("carriage return in source file");
	      warning ("(we only warn about the first carriage return)");
	      newline_warning = 1;
	    }
	  c = GETC();
	  break;

	case '\\':
	  c = GETC();
	  if (c == '\n')
	    input_file_stack->l.lineno++;
	  else
	    error ("stray '\\' in program");
	  c = GETC();
	  break;

	default:
	  return (c);
	}
    }
}

/* Make the token buffer longer, preserving the data in it.
   P should point to just beyond the last valid character in the old buffer.
   The value we return is a pointer to the new buffer
   at a place corresponding to P.  */

static char *
extend_token_buffer (p)
     char *p;
{
  int offset = p - token_buffer;

  maxtoken = maxtoken * 2 + 10;
  token_buffer = (char *) xrealloc (token_buffer, maxtoken + 2);

  return token_buffer + offset;
}

static char *traditional token_ptr;
#define TUNGETC(c) (token_ptr--, UNGETC((c)))

static int TGETC(void)
{
  int c = GETC();

  if (c != EOF)
    {
      if (token_ptr == token_buffer + maxtoken)
	token_ptr = extend_token_buffer(token_ptr);
      *token_ptr++ = c;
    }
  return c;
}

static cstring make_token_cstring(void)
{
  return make_cstring(parse_region, token_buffer, token_ptr - token_buffer);
}


#define GET_DIRECTIVE_LINE() get_directive_line (input_file_stack->lex.finput)

/* Read the rest of a #-directive from input stream FINPUT.
   In normal use, the directive name and the white space after it
   have already been read, so they won't be included in the result.
   We allow for the fact that the directive line may contain
   a newline embedded within a character or string literal which forms
   a part of the directive.

   The value is a string in a reusable buffer.  It remains valid
   only until the next time this function is called.

   The terminating character ('\n' or EOF) is left in FINPUT for the
   caller to re-read.  */

char *
get_directive_line (finput)
     FILE *finput;
{
  static char *directive_buffer = NULL;
  static unsigned buffer_length = 0;
  char *p;
  char *buffer_limit;
  int looking_for = 0;
  int char_escaped = 0;

  if (buffer_length == 0)
    {
      directive_buffer = (char *)xmalloc (128);
      buffer_length = 128;
    }

  buffer_limit = &directive_buffer[buffer_length];

  for (p = directive_buffer; ; )
    {
      int c;

      /* Make buffer bigger if it is full.  */
      if (p >= buffer_limit)
        {
	  unsigned bytes_used = (p - directive_buffer);

	  buffer_length *= 2;
	  directive_buffer
	    = (char *)xrealloc (directive_buffer, buffer_length);
	  p = &directive_buffer[bytes_used];
	  buffer_limit = &directive_buffer[buffer_length];
        }

      c = getc (finput);

      /* Discard initial whitespace.  */
      if ((c == ' ' || c == '\t') && p == directive_buffer)
	continue;

      /* Detect the end of the directive.  */
      if (looking_for == 0
	  && (c == '\n' || c == EOF))
	{
          ungetc (c, finput);
	  c = '\0';
	}

      *p++ = c;

      if (c == 0)
	return directive_buffer;

      /* Handle string and character constant syntax.  */
      if (looking_for)
	{
	  if (looking_for == c && !char_escaped)
	    looking_for = 0;	/* Found terminator... stop looking.  */
	}
      else
        if (c == '\'' || c == '"')
	  looking_for = c;	/* Don't stop buffering until we see another
				   another one of these (or an EOF).  */

      /* Handle backslash.  */
      char_escaped = (c == '\\' && ! char_escaped);
    }
}

/* Requires: c come from a STRING token.
   Returns a C string with the double quotes removed. Does not deal
   with escape sequences, etc, etc. */
static char *parse_string_token(lexical_cst c)
{
  char *s;

  s = rstralloc(parse_region, c->cstring.length - 1);
  s[c->cstring.length - 2] = '\0';
  memcpy(s, c->cstring.data + 1, c->cstring.length - 2);

  return s;
}

static bool token_isint(int token, struct yystype *lvalp)
{
  return token == CONSTANT && type_integral(lvalp->u.constant->type);
}

static int token_intvalue(struct yystype *lvalp)
{
  return constant_sint_value(lvalp->u.constant->cst);
}

/* At the beginning of a line, increment the line number
   and process any #-directive on this line.
   If the line is a #-directive, read the entire line and return a newline.
   Otherwise, return the line's first non-whitespace character.  */

int
check_newline ()
{
  int c;
  int token;
  struct yystype lval;
  
  input_file_stack->l.lineno++;

  /* Read first nonwhite char on the line.  */

  c = GETC();
  while (c == ' ' || c == '\t')
    c = GETC();

  if (c != '#')
    {
      /* If not #, return it so caller will use it.  */
      return c;
    }

  /* Read first nonwhite char after the `#'.  */

  c = GETC();
  while (c == ' ' || c == '\t')
    c = GETC();

  /* If a letter follows, then if the word here is `line', skip
     it and ignore it; otherwise, ignore the line, with an error
     if the word isn't `pragma', `ident', `define', or `undef'.  */

  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
    {
      if (c == 'p')
	{
	  if (GETC() == 'r'
	      && GETC() == 'a'
	      && GETC() == 'g'
	      && GETC() == 'm'
	      && GETC() == 'a'
	      && ((c = GETC()) == ' ' || c == '\t' || c == '\n'))
	    {
	      return save_directive("pragma");
	    }
	}

      else if (c == 'd')
	{
	  if (GETC() == 'e'
	      && GETC() == 'f'
	      && GETC() == 'i'
	      && GETC() == 'n'
	      && GETC() == 'e'
	      && ((c = GETC()) == ' ' || c == '\t' || c == '\n'))
	    {
	      return save_directive("define");
	    }
	}
      else if (c == 'u')
	{
	  if (GETC() == 'n'
	      && GETC() == 'd'
	      && GETC() == 'e'
	      && GETC() == 'f'
	      && ((c = GETC()) == ' ' || c == '\t' || c == '\n'))
	    {
	      return save_directive("undef");
	    }
	}
      else if (c == 'l')
	{
	  if (GETC() == 'i'
	      && GETC() == 'n'
	      && GETC() == 'e'
	      && ((c = GETC()) == ' ' || c == '\t'))
	    goto linenum;
	}
      else if (c == 'i')
	{
	  if (GETC() == 'd'
	      && GETC() == 'e'
	      && GETC() == 'n'
	      && GETC() == 't'
	      && ((c = GETC()) == ' ' || c == '\t'))
	    {
	      /* #ident.  The pedantic warning is now in cccp.c.  */

	      /* Here we have just seen `#ident '.
		 A string constant should follow.  */

	      while (c == ' ' || c == '\t')
		c = GETC();

	      /* If no argument, ignore the line.  */
	      if (c == '\n')
		return c;

	      UNGETC (c);
	      token = yylex (&lval);
	      if (token != STRING)
		{
		  error ("invalid #ident");
		  goto skipline;
		}

	      /* Skip the rest of this line.  */
	      goto skipline;
	    }
	}

      error ("undefined or invalid # directive");
      goto skipline;
    }

linenum:
  /* Here we have either `#line' or `# <nonletter>'.
     In either case, it should be a line number; a digit should follow.  */

  while (c == ' ' || c == '\t')
    c = GETC();

  /* If the # is the only nonwhite char on the line,
     just ignore it.  Check the new newline.  */
  if (c == '\n')
    return c;

  /* Something follows the #; read a token.  */

  UNGETC (c);
  token = yylex (&lval);

  if (token_isint(token, &lval))
    {
      const char *new_filename;
      int used_up = 0;
      /* subtract one, because it is the following line that
	 gets the specified number */
      int l = token_intvalue(&lval) - 1;

      /* Is this the last nonwhite stuff on the line?  */
      c = GETC();
      while (c == ' ' || c == '\t')
	c = GETC();
      if (c == '\n')
	{
	  /* No more: store the line number and check following line.  */
	  input_file_stack->l.lineno = l;
	  return c;
	}
      UNGETC (c);

      /* More follows: it must be a string constant (filename).  */

      /* Read the string constant.  */
      token = yylex (&lval);

      if (token != STRING)
	{
	  error ("invalid #line");
	  goto skipline;
	}

      new_filename = parse_string_token(CAST(lexical_cst, lval.u.constant));
      /* Each change of file name
	 reinitializes whether we are now in a system header.  */
      input_file_stack->l.in_system_header = 0;

      /* Is this the last nonwhite stuff on the line?  */
      c = GETC();
      while (c == ' ' || c == '\t')
	c = GETC();
      if (c == '\n')
	{
	  input_file_stack->l.filename = new_filename;
	  input_file_stack->l.lineno = l;
	  return c;
	}

      UNGETC (c);

      token = yylex (&lval);
      used_up = 0;

      /* `1' after file name means entering new file.
	 `2' after file name means just left a file.  */

      if (token_isint(token, &lval))
	{
	  int cst = token_intvalue(&lval);

	  if (cst == 1)
	    {
	      push_input();
	      used_up = 1;
	    }
	  else if (cst == 2)
	    {
	      /* Popping out of a file.  */
	      if (input_file_stack->next &&
		  input_file_stack->lex.finput == input_file_stack->next->lex.finput)
		{
		  int current_il = input_file_stack->lex.indent_level;
		  int previous_il = input_file_stack->next->lex.indent_level;
		  if (current_il != previous_il)
		    {
		      warning_with_location
			(input_file_stack->l,
			 "This file contains more `%c's than `%c's.",
			 current_il > previous_il ? '{' : '}',
			 current_il > previous_il ? '}' : '{');
		    }
		  pop_input();
		}
	      else
		error ("#-lines for entering and leaving files don't match");

	      used_up = 1;
	    }
	}

      input_file_stack->l.filename = new_filename;
      input_file_stack->l.lineno = l;

      /* If we have handled a `1' or a `2',
	 see if there is another number to read.  */
      if (used_up)
	{
	  /* Is this the last nonwhite stuff on the line?  */
	  c = GETC();
	  while (c == ' ' || c == '\t')
	    c = GETC();
	  if (c == '\n')
	    return c;
	  UNGETC (c);

	  token = yylex (&lval);
	  used_up = 0;
	}

      /* `3' after file name means this is a system header file.  */

      if (token_isint(token, &lval) && token_intvalue(&lval) == 3)
	input_file_stack->l.in_system_header = 1, used_up = 1;

      if (used_up)
	{
	  /* Is this the last nonwhite stuff on the line?  */
	  c = GETC();
	  while (c == ' ' || c == '\t')
	    c = GETC();
	  if (c == '\n')
	    return c;
	  UNGETC (c);
	}

      warning ("unrecognized text at end of #line");
    }
  else
    error ("invalid #-line");

  /* skip the rest of this line.  */
 skipline:
  if (c != '\n' && c != EOF && input_file_stack->lex.nextchar >= 0)
    c = input_file_stack->lex.nextchar, input_file_stack->lex.nextchar = -1;
  while (c != '\n' && c != EOF)
    c = GETC();
  return c;
}

#define ENDFILE -1  /* token that represents end-of-file */

/* Read an escape sequence, saving it in the token_buffer.
   Return the escape sequence's value.
   store 1 in *ignore_ptr if escape sequence is backslash-newline.  */

static int
readescape (int *ignore_ptr)
{
  int c = TGETC();
  int code;
  unsigned count;
  unsigned firstdig = 0;
  int nonnull;

  switch (c)
    {
    case 'x':
      if (warn_traditional)
	warning ("the meaning of `\\x' varies with -traditional");

      if (flag_traditional)
	return c;

      code = 0;
      count = 0;
      nonnull = 0;
      while (1)
	{
	  c = TGETC();
	  if (!(c >= 'a' && c <= 'f')
	      && !(c >= 'A' && c <= 'F')
	      && !(c >= '0' && c <= '9'))
	    {
	      TUNGETC (c);
	      break;
	    }

	  code *= 16;
	  if (c >= 'a' && c <= 'f')
	    code += c - 'a' + 10;
	  if (c >= 'A' && c <= 'F')
	    code += c - 'A' + 10;
	  if (c >= '0' && c <= '9')
	    code += c - '0';
	  if (code != 0 || count != 0)
	    {
	      if (count == 0)
		firstdig = code;
	      count++;
	    }
	  nonnull = 1;
	}
      if (! nonnull)
	error ("\\x used with no following hex digits");
      else if (count == 0)
	/* Digits are all 0's.  Ok.  */
	;
      else if (count - 1 >= int_type_size * 2
	       || (count > 1
		   && ((1 << (int_type_size - (count - 1) * 4))
		       <= firstdig)))
	pedwarn ("hex escape out of range");
      return code;

    case '0':  case '1':  case '2':  case '3':  case '4':
    case '5':  case '6':  case '7':
      code = 0;
      count = 0;
      while ((c <= '7') && (c >= '0') && (count++ < 3))
	{
	  code = (code * 8) + (c - '0');
	  c = TGETC();
	}
      TUNGETC (c);
      return code;

    case '\\': case '\'': case '"':
      return c;

    case '\n':
      input_file_stack->l.lineno++;
      *ignore_ptr = 1;
      return 0;

      /* SAME */
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'b': return '\b';
    case '?': return '\?';

    case 'a':
      if (warn_traditional)
	warning ("the meaning of `\\a' varies with -traditional");
      if (flag_traditional)
	return c;
      return '\a'; /* SAME */

    case 'v':
#if 0 /* Vertical tab is present in common usage compilers.  */
      if (flag_traditional)
	return c;
#endif
      return '\v'; /* SAME */

    case 'e':
    case 'E':
      if (pedantic)
	pedwarn ("non-ANSI-standard escape sequence, `\\%c'", c);
      return 033;

      /* `\(', etc, are used at beginning of line to avoid confusing Emacs.  */
    case '(':
    case '{':
    case '[':
      /* `\%' is used to prevent SCCS from getting confused.  */
    case '%':
      if (pedantic)
	pedwarn ("non-ANSI escape sequence `\\%c'", c);
      return c;

    default:
      if (c >= 040 && c < 0177)
	pedwarn ("unknown escape sequence `\\%c'", c);
      else
	pedwarn ("unknown escape sequence: `\\' followed by char code 0x%x", c);
      return c;
    }
}

static int read_char(char *context, char terminating_char,
		     char *cp, wchar_t *wcp)
{
  unsigned width = wcp ? type_size(wchar_type) * BITSPERBYTE
    : BITSPERBYTE; /* sizeof(char) == 1 */
  int c;
#ifdef MULTIBYTE_CHARS
  (void) local_mbtowc (NULL_PTR, NULL_PTR, 0);
#endif

 tryagain:
  c = TGETC ();

  if (c == terminating_char)
    return 0;

  if (c < 0)
    return -1;

  if (c == '\\')
    {
      int ignore = 0;
      c = readescape (&ignore);
      if (ignore)
	goto tryagain;

      if (width < sizeof(unsigned) * BITSPERBYTE
	  && (unsigned) c >= ((unsigned)1 << width))
	pedwarn ("escape sequence out of range for character");
    }
  else if (c == '\n')
    {
      if (pedantic)
	pedwarn ("ANSI C forbids newline in %s constant", context);
      input_file_stack->l.lineno++;
    }
  else
    {
#ifdef MULTIBYTE_CHARS
      wchar_t wc;
      int i;
      int char_len = -1;
      for (i = 0; i < max_char_length; ++i)
	{
	  cp[i] = c;

	  char_len = local_mbtowc (& wc, cp, i + 1);
	  if (char_len != -1)
	    break;
	  c = TGETC ();
	}
      if (char_len == -1)
	{
	  warning ("Ignoring invalid multibyte character");
	  /* Note: gcc just takes the character following the
	     invalid multibyte-char-sequence as being the next 
	     character. This is obviously incorrect. */
	  TUNGETC (c);
	  goto tryagain;
	}
      else
	{
	  /* mbtowc sometimes needs an extra char before accepting */
	  if (char_len <= i)
	    TUNGETC (c);
	  if (! wcp)
	    return i + 1;
	  else
	    {
	      *wcp = wc;
	      return 1;
	    }
	}
#endif /* MULTIBYTE_CHARS */
    }

  if (wcp)
    *wcp = c;
  else
    *cp = c;

  return 1;
}


void
yyerror (char *string)
{
  char buf[200];

  strcpy (buf, string);

  /* We can't print string and character constants well
     because the token_buffer contains the result of processing escapes.  */
  /* XXX: This is not true anymore, but ignore that for a while */
  if (input_file_stack->lex.end_of_file)
    strcat (buf, " at end of input");
  else if (token_buffer[0] == 0)
    strcat (buf, " at null character");
  else if (token_buffer[0] == '"')
    strcat (buf, " before string constant");
  else if (token_buffer[0] == '\'')
    strcat (buf, " before character constant");
  else if (token_buffer[0] < 040 || (unsigned char) token_buffer[0] >= 0177)
    sprintf (buf + strlen (buf), " before character 0%o",
	     (unsigned char) token_buffer[0]);
  else
    strcat (buf, " before `%s'");

  error (buf, token_buffer);
}


int
yylex(struct yystype *lvalp)
{
  int c;
  int value;
  int wide_flag = 0;
  location loc;

  /* Grammar selection hack */
  if (language_token != -1)
    {
      int token = language_token;
      language_token = -1;
      return token;
    }

  if (input_file_stack->lex.nextchar >= 0)
    c = input_file_stack->lex.nextchar, input_file_stack->lex.nextchar = -1;
  else
    c = GETC();

  /* Effectively do c = skip_white_space (c)
     but do it faster in the usual cases.  */
  while (1)
    switch (c)
      {
      case ' ':
      case '\t':
      case '\f':
      case '\v':
      case '\b':
	c = GETC();
	break;

      case '\r':
	/* Call skip_white_space so we can warn if appropriate.  */

      case '\n':
      case '/':
      case '\\':
	c = skip_white_space (c);
      default:
	goto found_nonwhite;
      }
 found_nonwhite:

  token_ptr = token_buffer;
  *token_ptr++ = c;

  last_location = loc = input_file_stack->l;

  lvalp->u.itoken.location = loc;
  lvalp->u.itoken.i = 0;

  switch (c)
    {
    case EOF:
      input_file_stack->lex.end_of_file = 1;
      token_buffer[0] = 0;
      value = ENDFILE;
      break;

    case 'L':
      /* Capital L may start a wide-string or wide-character constant.  */
      {
	int c = TGETC();
	if (c == '\'')
	  {
	    wide_flag = 1;
	    goto char_constant;
	  }
	if (c == '"')
	  {
	    wide_flag = 1;
	    goto string_constant;
	  }
	TUNGETC (c);
      }
      goto letter;

    case 'A':  case 'B':  case 'C':  case 'D':  case 'E':
    case 'F':  case 'G':  case 'H':  case 'I':  case 'J':
    case 'K':		  case 'M':  case 'N':  case 'O':
    case 'P':  case 'Q':  case 'R':  case 'S':  case 'T':
    case 'U':  case 'V':  case 'W':  case 'X':  case 'Y':
    case 'Z':
    case 'a':  case 'b':  case 'c':  case 'd':  case 'e':
    case 'f':  case 'g':  case 'h':  case 'i':  case 'j':
    case 'k':  case 'l':  case 'm':  case 'n':  case 'o':
    case 'p':  case 'q':  case 'r':  case 's':  case 't':
    case 'u':  case 'v':  case 'w':  case 'x':  case 'y':
    case 'z':
    case '_':
    case '$':
    letter:
      while (isalnum (c) || c == '_' || c == '$')
	{
	  /* Make sure this char really belongs in an identifier.  */
	  if (c == '$')
	    {
	      if (! dollars_in_ident)
		error ("`$' in identifier");
	      else if (pedantic)
		pedwarn ("`$' in identifier");
	    }

	  c = TGETC();
	}

      *token_ptr = 0;
      token_ptr--;
      input_file_stack->lex.nextchar = c;

      value = IDENTIFIER;

      /* Try to recognize a keyword.  Uses minimum-perfect hash function */
      {
	struct resword *ptr;

	*token_ptr = '\0';
	if ((ptr = is_reserved_word (token_buffer, token_ptr - token_buffer)))
	  {
	    value = (int) ptr->token;
	    lvalp->u.itoken.i = (int) ptr->rid;

	    /* Even if we decided to recognize asm, still perhaps warn.  */
	    if (pedantic
		&& (value == ASM_KEYWORD || value == TYPEOF
		    || ptr->rid == RID_INLINE)
		&& token_buffer[0] != '_')
	      pedwarn ("ANSI does not permit the keyword `%s'",
		       token_buffer);
	  }
      }

      /* If we did not find a keyword, look for an identifier
	 (or a typename).  */
      if (value == IDENTIFIER)
	{
	  lvalp->idtoken.location = loc;
	  lvalp->idtoken.id = make_token_cstring();
	  lvalp->idtoken.decl = lookup_id(lvalp->idtoken.id.data, FALSE);

	  if (lvalp->idtoken.decl)
	    {
	      if (lvalp->idtoken.decl->kind == decl_typedef)
		value = TYPENAME;
	      else if (lvalp->idtoken.decl->kind == decl_magic_string)
		value = MAGIC_STRING;
	    }
	}

      break;

    case '0':  case '1': case '2':  case '3':  case '4':
    case '5':  case '6':  case '7':  case '8':  case '9':
    case '.':
      {
	int base = 10;
	int largest_digit = 0;
	int numdigits = 0;
	largest_uint cstvalue = 0, maxbase;
	int overflow = 0;

	enum anon1 { NOT_FLOAT, AFTER_POINT, TOO_MANY_POINTS} floatflag
	  = NOT_FLOAT;

	if (c == '0')
	  {
	    c = TGETC();
	    if ((c == 'x') || (c == 'X'))
	      {
		base = 16;
		c = TGETC();
	      }
	    /* Leading 0 forces octal unless the 0 is the only digit.  */
	    else if (c >= '0' && c <= '9')
	      {
		base = 8;
		numdigits++;
	      }
	    else
	      numdigits++;
	  }

	/* Read all the digits-and-decimal-points.  */
	/* Find the maximum value that can be multiplied by base without
	   overflowing a largest_uint, i.e., (1 << LARGEST_UINTBITS) / base */
	maxbase = (((largest_uint)1 << (LARGEST_UINTBITS - 1)) / base) << 1;
	maxbase += ((((largest_uint)1 << (LARGEST_UINTBITS - 1)) % base) << 1) / base;

	while (c == '.'
	       || (isalnum (c) && c != 'l' && c != 'L'
		   && c != 'u' && c != 'U'
		   && c != 'i' && c != 'I' && c != 'j' && c != 'J'
		   && (floatflag == NOT_FLOAT || ((c != 'f') && (c != 'F')))))
	  {
	    if (c == '.')
	      {
		if (base == 16)
		  error ("floating constant may not be in radix 16");
		if (floatflag == TOO_MANY_POINTS)
		  /* We have already emitted an error.  Don't need another.  */
		  ;
		else if (floatflag == AFTER_POINT)
		  {
		    error ("malformed floating constant");
		    floatflag = TOO_MANY_POINTS;
		  }
		else
		  floatflag = AFTER_POINT;

		base = 10;
		c = TGETC();
		/* Accept '.' as the start of a floating-point number
		   only when it is followed by a digit.
		   Otherwise, unread the following non-digit
		   and use the '.' as a structural token.  */
		if (token_ptr == token_buffer + 2 && !isdigit (c))
		  {
		    if (c == '.')
		      {
			c = TGETC();
			if (c == '.')
			  {
			    value = ELLIPSIS;
			    goto done;
			  }
			error ("parse error at `..'");
		      }
		    TUNGETC (c);
		    value = '.';
		    goto done;
		  }
	      }
	    else
	      {
		/* It is not a decimal point.
		   It should be a digit (perhaps a hex digit).  */

		if (isdigit (c))
		  {
		    c = c - '0';
		  }
		else if (base <= 10)
		  {
		    if (c == 'e' || c == 'E')
		      {
			base = 10;
			floatflag = AFTER_POINT;
			break;   /* start of exponent */
		      }
		    error ("nondigits in number and not hexadecimal");
		    c = 0;
		  }
		else if (c >= 'a')
		  {
		    c = c - 'a' + 10;
		  }
		else
		  {
		    c = c - 'A' + 10;
		  }
		if (c >= largest_digit)
		  largest_digit = c;
		numdigits++;

		/* Keep track of constants up to largest_uint's range (unsigned) */
		if (cstvalue > maxbase)
		  overflow = 1;
		cstvalue = cstvalue * base;
		if ((largest_uint)-1 - cstvalue < c)
		  overflow = 1;
		cstvalue += c;

		c = TGETC();
	      }
	  }

	if (numdigits == 0)
	  error ("numeric constant with no digits");

	if (largest_digit >= base)
	  error ("numeric constant contains digits beyond the radix");

	if (floatflag != NOT_FLOAT)
	  {
	    type ftype;
	    int imag = 0;
	    int fflag = 0, lflag = 0;

	    /* Read explicit exponent if any, and put it in tokenbuf.  */

	    if ((c == 'e') || (c == 'E'))
	      {
		c = TGETC();
		if ((c == '+') || (c == '-'))
		  {
		    c = TGETC();
		  }
		if (! isdigit (c))
		  error ("floating constant exponent has no digits");
	        while (isdigit (c))
		  {
		    c = TGETC();
		  }
	      }

	    while (1)
	      {
		int lose = 0;

		/* Read the suffixes to choose a data type.  */
		switch (c)
		  {
		  case 'f': case 'F':
		    if (fflag)
		      error ("more than one `f' in numeric constant");
		    fflag = 1;
		    break;

		  case 'l': case 'L':
		    if (lflag)
		      error ("more than one `l' in numeric constant");
		    lflag = 1;
		    break;

		  case 'i': case 'I':
		    if (imag)
		      error ("more than one `i' or `j' in numeric constant");
		    else if (pedantic)
		      pedwarn ("ANSI C forbids imaginary numeric constants");
		    imag = 1;
		    break;

		  default:
		    lose = 1;
		  }

		if (lose)
		  break;

		c = TGETC();
	      }

	    TUNGETC(c);

	    if (fflag)
	      {
		if (lflag)
		  error ("both `f' and `l' in floating constant");

		ftype = float_type;
	      }
	    else if (lflag)
	      ftype = long_double_type;
	    else
	      ftype = double_type;

	    if (imag)
	      ftype = make_complex_type(ftype);

	    lvalp->u.constant = fold_lexical_real(ftype, loc, make_token_cstring());
	  }
	else
	  {
	    int spec_unsigned = 0;
	    int spec_long = 0;
	    int spec_long_long = 0;
	    int spec_imag = 0;
	    type itype;

	    while (1)
	      {
		if (c == 'u' || c == 'U')
		  {
		    if (spec_unsigned)
		      error ("two `u's in integer constant");
		    spec_unsigned = 1;
		  }
		else if (c == 'l' || c == 'L')
		  {
		    if (spec_long)
		      {
			if (spec_long_long)
			  error ("three `l's in integer constant");
			else if (pedantic)
			  pedwarn ("ANSI C forbids long long integer constants");
			spec_long_long = 1;
		      }
		    spec_long = 1;
		  }
		else if (c == 'i' || c == 'j' || c == 'I' || c == 'J')
		  {
		    if (spec_imag)
		      error ("more than one `i' or `j' in numeric constant");
		    else if (pedantic)
		      pedwarn ("ANSI C forbids imaginary numeric constants");
		    spec_imag = 1;
		  }
		else
		  break;
		c = TGETC();
	      }

	    TUNGETC (c);

	    /* Collect type as specified in the lexeme. The constant folder
	       will expand the type if necessary. */
	    if (base != 10)
	      spec_unsigned = 1;

	    if (spec_long_long)
	      itype = spec_unsigned ? unsigned_long_long_type : long_long_type;
	    else if (spec_long)
	      itype = spec_unsigned ? unsigned_long_type : long_type;
	    else
	      itype = spec_unsigned ? unsigned_int_type : int_type;

	    lvalp->u.constant =
	      fold_lexical_int(itype, loc, make_token_cstring(),
			       spec_imag, cstvalue, overflow);
	  }

	if (isalnum (c) || c == '.' || c == '_' || c == '$'
	    || (!flag_traditional && (c == '-' || c == '+')
		&& (token_ptr[-1] == 'e' || token_ptr[-1] == 'E')))
	  error ("missing white space after number `%s'", token_buffer);

	value = CONSTANT; break;
      }

    case '\'':
    char_constant:
      {
	wchar_t wc;
	char *cbuf = alloca(max_char_length);
	int chars_seen = 0, count, result = 0;
	unsigned width = wide_flag ? type_size(wchar_type) * BITSPERBYTE
	  : BITSPERBYTE; /* sizeof(char) == 1 */

	for (;;)
	  {
	    count = read_char("character", '\'', cbuf, wide_flag ? &wc : NULL);
	    if (count <= 0)
	      break;

	    chars_seen += count;

	    if (wide_flag)
	      {
		/* Note: read_char always returns 1 when wide_flag is true,
		   so chars_seen will always be 1 exactly once */
		assert(count == 1);
		if (chars_seen == 1)
		  result = wc;
	      }
	    else
	      {
		int i;

		/* Weird code if you ask me. But this is what gcc 2.95.3 does.
		   There's no particular consistency between compilers
		   on how to handle these (e.g., 'ab' gives different values
		   with Sun's cc and gcc). */
		for (i = 0; i < count && i < int_type_size; i++)
		  if (width < sizeof(unsigned) * BITSPERBYTE)
		    result = (result << width) | (cbuf[i] & ((1 << width) - 1));
		  else
		    result = cbuf[i];
	      }
	  }

	if (count < 0)
	  error ("malformatted character constant");
	else if (chars_seen == 0)
	  error ("empty character constant");
	else if (chars_seen > int_type_size) /* this is what gcc is testing */
	  error ("character constant too long");
	else if (chars_seen != 1 && ! flag_traditional && warn_multichar)
	  warning ("multi-character character constant");

	lvalp->u.constant = fold_lexical_char(loc, make_token_cstring(),
					      wide_flag, result);

	value = CONSTANT;
	break;
      }

    case '"':
    string_constant:
      {
	wchar_t wc;
	char *cbuf = alloca(max_char_length);
	int count;

	wchar_array_reset(string_array);

	for (;;)
	  {
	    wchar_t *p;

	    count = read_char("string", '"', cbuf, wide_flag ? &wc : NULL);
	    if (count <= 0)
	      break;

	    p = wchar_array_extend(string_array, count);
	    if (wide_flag)
	      {
		assert(count == 1);
		*p = wc;
	      }
	    else
	      {
		int i;
		
		for (i = 0; i < count; i++)
		  *p++ = cbuf[i];
	      }
	  }
	if (count < 0)
	  error ("Unterminated string constant");

	lvalp->u.string_cst = fold_lexical_string(loc, make_token_cstring(),
						  wide_flag, string_array);

	value = STRING;

	break;
      }

    case '@': 
      value = '*';
      break;

    case '+':
    case '-':
    case '&':
    case '|':
    case ':':
    case '<':
    case '>':
    case '*':
    case '/':
    case '%':
    case '^':
    case '!':
    case '=':
      {
	int c1;

      combine:

	c1 = TGETC();

	if (c1 == '=')
	  {
	    value = ASSIGN;
	    switch (c)
	      {
	      case '<':
		value = ARITHCOMPARE; lvalp->u.itoken.i = kind_leq; break;
	      case '>':
		value = ARITHCOMPARE; lvalp->u.itoken.i = kind_geq; break;
	      case '!':
		value = EQCOMPARE; lvalp->u.itoken.i = kind_ne; break;
	      case '=':
		value = EQCOMPARE; lvalp->u.itoken.i = kind_eq; break;

	      case '+':
		lvalp->u.itoken.i = kind_plus_assign; break;
	      case '-':
		lvalp->u.itoken.i = kind_minus_assign; break;
	      case '&':
		lvalp->u.itoken.i = kind_bitand_assign; break;
	      case '|':
		lvalp->u.itoken.i = kind_bitor_assign; break;
	      case '*':
		lvalp->u.itoken.i = kind_times_assign; break;
	      case '/':
		lvalp->u.itoken.i = kind_divide_assign; break;
	      case '%':
		lvalp->u.itoken.i = kind_modulo_assign; break;
	      case '^':
		lvalp->u.itoken.i = kind_bitxor_assign; break;
	      case LSHIFT:
		lvalp->u.itoken.i = kind_lshift_assign; break;
	      case RSHIFT:
		lvalp->u.itoken.i = kind_rshift_assign; break;
	      }
	    goto done;
	  }
	else if (c == c1)
	  switch (c)
	    {
	    case '+':
	      value = PLUSPLUS; goto done;
	    case '-':
	      value = MINUSMINUS; goto done;
	    case '&':
	      value = ANDAND; goto done;
	    case '|':
	      value = OROR; goto done;
	    case '<':
	      c = LSHIFT;
	      goto combine;
	    case '>':
	      c = RSHIFT;
	      goto combine;
	    }
	else
	  switch (c)
	    {
	    case '-':
	      if (c1 == '>')
		{ value = POINTSAT; goto done; }
	      break;
	    case ':':
	      if (c1 == '>')
		{ value = ']'; goto done; }
	      break;
	    case '<':
	      if (c1 == '%')
		{ value = '{'; input_file_stack->lex.indent_level++; goto done; }
	      if (c1 == ':')
		{ value = '['; goto done; }
	      break;
	    case '%':
	      if (c1 == '>')
		{ value = '}'; input_file_stack->lex.indent_level--; goto done; }
	      break;
	    }
	TUNGETC (c1);

	if (c == '<') 
	  {
	    c1 = TGETC();

	    if (c1 == '-')
	      value = TASTNIOP;
	    else
	      {
		TUNGETC (c1);
		value = ARITHCOMPARE;
		lvalp->u.itoken.i = kind_lt;
	      }
	  }
	else if (c == '>')
	  {
	    value = ARITHCOMPARE;	
	    lvalp->u.itoken.i = kind_gt;
	  }
	else
	  value = c;
	goto done;
      }

    case 0:
      /* Don't make yyparse think this is eof.  */
      value = 1;
      break;

    case '{':
      input_file_stack->lex.indent_level++;
      value = c;
      break;

    case '}':
      input_file_stack->lex.indent_level--;
      value = c;
      break;

    default:
      value = c;
    }

done:
  *token_ptr = 0;

  return value;
}

/* Sets the value of the 'yydebug' variable to VALUE.
   This is a function so we don't have to have YYDEBUG defined
   in order to build the compiler.  */

void
set_yydebug (value)
     int value;
{
#if YYDEBUG != 0
  yydebug = value;
#else
  warning ("YYDEBUG not defined.");
#endif
}