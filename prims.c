/*
 * prims.c
 *
 *  Created on: 2013年7月18日
 *      Author: liutos
 *
 * This file contains the definition of all primitive functions
 */
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gc/gc.h>

#include "compiler.h"
#include "object.h"
#include "prims.h"
#include "type.h"
#include "utilities.h"
#include "vm.h"

// Register a primitive function
#define ADD(arity, restp, function_name, Lisp_name)                     \
  do {                                                                  \
    lt *func =                                                          \
        make_primitive(arity, (void *)function_name, Lisp_name, restp); \
    symbol_value(LISP(Lisp_name)) = func;                                  \
  } while (0)

// Register the primitive functions without rest parameter
#define NOREST(arity, function_name, Lisp_name) ADD(arity, FALSE, function_name, Lisp_name)

#define SIG(Lisp_name, ...) \
  do { \
    lt *func = symbol_value(S(Lisp_name)); \
    primitive_signature(func) = raw_list(__VA_ARGS__, NULL); \
  } while (0)

#define OR(...) make_pair(S("or"), raw_list(__VA_ARGS__, NULL))

#define T(tag) type_ref(tag)

/* Writer */
void write_raw_char(char c, lt *dest_port) {
  FILE *fp = output_port_stream(dest_port);
  fputc(c, fp);
  if (c == '\n') {
    output_port_linum(dest_port)++;
    output_port_colnum(dest_port) = 0;
  } else
    output_port_colnum(dest_port)++;
}

void write_n_spaces(int n, lt *dest) {
  for (int i = 0; i < n; i++)
    write_raw_char(' ', dest);
}

void write_raw_string(char *string, lt *dest_port) {
  while (*string != '\0') {
    write_raw_char(*string, dest_port);
    string++;
  }
}

void writef(lt *dest, const char *format, ...) {
	int nch = 0;
  va_list ap;
  lisp_object_t *arg;

  va_start(ap, format);
  char c = *format;
  while (c != '\0') {
    if (c != '%')
      write_raw_char(c, dest);
    else {
      format++;
      c = *format;
      arg = va_arg(ap, lisp_object_t *);
      switch (c) {
        case 'c':
          assert(ischar(arg));
          write_raw_char(character_value(arg), dest);
          break;
        case 's':
          assert(is_lt_string(arg));
          write_raw_string(string_value(arg), dest);
          break;
        case 'p':
          nch = fprintf(output_port_stream(dest), "%p", arg);
          output_port_colnum(dest) += nch;
          break;
        case 'f':
          assert(is_lt_float(arg));
          nch = fprintf(output_port_stream(dest), "%f", float_value(arg));
          output_port_colnum(dest) += nch;
          break;
        case 'd':
          assert(isfixnum(arg));
          nch = fprintf(output_port_stream(dest), "%d", fixnum_value(arg));
          output_port_colnum(dest) += nch;
          break;
        case '?':
          write_object(arg, dest);
          break;
        case 'S':
          assert(is_lt_symbol(arg));
          write_object(arg, dest);
          break;
        case '%':
          write_raw_char('%', dest);
          break;
        default :
          fprintf(stdout, "Invalid character %c after %%", c);
          exit(1);
      }
    }
    format++;
    c = *format;
  }
}

void write_opcode(lt *opcode, lt *dest) {
  write_raw_string("#<", dest);
  write_raw_string(opcode_op(opcode_ref(opcode_name(opcode))), dest);
  for (int i = 0; i < vector_length(opcode_oprands(opcode)); i++) {
    writef(dest, " %?", vector_value(opcode_oprands(opcode))[i]);
  }
  write_raw_char('>', dest);
}

void write_compiled_function(lt *function, int indent, lt *dest) {
  writef(dest, "#<COMPILED-FUNCTION %p name: %?\n", function, function_name(function));
  assert(is_lt_vector(function_code(function)));
  for (int i = 0; i < vector_length(function_code(function)); i++) {
    lt *ins = vector_value(function_code(function))[i];
    write_n_spaces(indent, dest);
    if (is_label(ins)) {
      writef(dest, "%S:", ins);
      int rest_width = 8 - (strlen(symbol_name(ins)) + 2);
      write_n_spaces(rest_width, dest);
      continue;
    } else
      write_raw_string("    ", dest);
    write_raw_string(opcode_op(ins), dest);
    int rest_width = opcode_max_length + 1 - strlen(opcode_op(ins));
    write_n_spaces(rest_width, dest);
    if (opcode_name(ins) == FN) {
      write_compiled_function(op_fn_func(ins), output_port_colnum(dest), dest);
    } else {
      for (int j = 0; j < vector_length(opcode_oprands(ins)); j++) {
        write_object(vector_value(opcode_oprands(ins))[j], dest);
        if (j != vector_length(opcode_oprands(ins)) - 1)
          write_raw_char(' ', dest);
      }
    }
    write_raw_char('\n', dest);
  }
  for (int j = 0; j < indent; j++)
    write_raw_char(' ', dest);
  write_raw_char('>', dest);
}

void write_object(lt *x, lt *output_file) {
  assert(x != NULL);
  switch(type_of(x)) {
    case LT_BOOL:
      if (is_true_object(x))
        write_raw_string("#t", output_file);
      else
        write_raw_string("#f", output_file);
      break;
    case LT_CHARACTER: {
      int c = character_value(x);
      switch (c) {
        case ' ':
          write_raw_string("#\\space", output_file);
          break;
        case '\n':
          write_raw_string("#\\newline", output_file);
          break;
        default :
          writef(output_file, "#\\%c", x);
      }
    }
      break;
    case LT_EMPTY_LIST:
    	write_raw_string("()", output_file);
    	break;
    case LT_ENVIRONMENT:
      writef(output_file, "#<ENVIRONMENT %? %p>", environment_bindings(x), x);
      break;
    case LT_EXCEPTION: {
      writef(output_file, "%S: ", exception_tag(x));
      writef(output_file, "%s\n", make_string(exception_msg(x)));
      lt *backtrace = exception_backtrace(x);
      while (!isnull(backtrace)) {
        lt *fn = pair_head(backtrace);
        if (is_lt_function(fn))
          writef(output_file, "%?", function_name(pair_head(backtrace)));
        else
          write_raw_string(primitive_Lisp_name(fn), output_file);
        if (!isnull(pair_tail(backtrace)))
          write_raw_char('\n', output_file);
        backtrace = pair_tail(backtrace);
      }
    }
      break;
    case LT_FIXNUM:
    	writef(output_file, "%d", x);
    	break;
    case LT_FLOAT:
    	writef(output_file, "%f", x);
    	break;
    case LT_FUNCTION: {
      int indent = output_port_colnum(output_file);
      write_compiled_function(x, indent, output_file);
    }
      break;
    case LT_INPUT_PORT:
    	writef(output_file, "#<INPUT-FILE %p>", x);
    	break;
    case LT_OUTPUT_PORT:
    	writef(output_file, "#<OUTPUT-FILE %p>", x);
    	break;
    case LT_PACKAGE:
      writef(output_file, "#<PACKAGE name: %s>", package_name(x));
      break;
    case LT_PAIR:
      write_raw_string("(", output_file);
      write_object(pair_head(x), output_file);
      for (x = pair_tail(x); is_lt_pair(x); x = pair_tail(x)) {
        write_raw_string(" ", output_file);
        write_object(pair_head(x), output_file);
      }
      if (!isnull(x)) {
        write_raw_string(" . ", output_file);
        write_object(x, output_file);
      }
      write_raw_string(")", output_file);
      break;
    case LT_PRIMITIVE:
      write_raw_string("#<PRIMITIVE-FUNCTION ", output_file);
      write_raw_string(primitive_Lisp_name(x), output_file);
      writef(output_file, " %p>", x);
      break;
    case LT_RETADDR:
      writef(output_file, "#<RETADDR %p pc: %d>", x, make_fixnum(retaddr_pc(x)));
      break;
    case LT_STRING: {
      char *value = string_value(x);
      write_raw_string("\"", output_file);
      for (int i = 0; value[i] != '\0'; i++) {
        if (value[i] == '"')
          write_raw_string("\\\"", output_file);
        else
          write_raw_char(value[i], output_file);
      }
      write_raw_string("\"", output_file);
    }
      break;
    case LT_SYMBOL:
    	write_raw_string(symbol_name(x), output_file);
    	break;
    case LT_TEOF:
    	write_raw_string("#<EOF>", output_file);
    	break;
    case LT_TUNDEF:
      write_raw_string("#<UNDEF>", output_file);
      break;
    case LT_TYPE:
      write_raw_string("#<TYPE ", output_file);
      write_raw_string(type_name(x), output_file);
      write_raw_char('>', output_file);
      break;
    case LT_VECTOR: {
      lisp_object_t **vector = vector_value(x);
      write_raw_string("[", output_file);
      for (int i = 0; i <= vector_last(x); i++) {
        write_object(vector[i], output_file);
        if (i != vector_last(x))
          write_raw_string(" ", output_file);
      }
      write_raw_string("]", output_file);
    }
      break;
    case LT_OPCODE: write_opcode(x, output_file); break;
    default :
      fprintf(stdout, "invalid object with type %d", type_of(x));
      exit(1);
  }
}

int get_char(lt *input) {
  assert(is_lt_input_port(input));
  FILE *in = input_port_stream(input);
  input_port_colnum(input)++;
  return getc(in);
}

lt *lt_raw_nth(lt *list, int n) {
  assert(is_lt_pair(list));
  for (int i = 0; i < n; i++) {
    list = pair_tail(list);
    if (!is_lt_pair(list)) {
      char msg[256];
      sprintf(msg, "This list is too short for indexing %d", n);
      return signal_exception(strdup(msg));
    }
  }
  return pair_head(list);
}

lt *lt_raw_nthtail(lt *list, int n) {
  assert(is_lt_pair(list));
  int n2 = n;
  while (n2 > 0) {
    if (!is_lt_pair(list)) {
      fprintf(stdout, "This list is too short for index %d\n", n);
      exit(1);
    }
    list = pair_tail(list);
    n2--;
  }
  return list;
}

/* Exception */
lt *lt_exception_tag(lt *exception) {
  return exception_tag(exception);
}

lt *lt_signal_exception(lt *message) {
  return signal_exception(string_value(message));
}

void init_prim_exception(void) {
  NOREST(1, lt_exception_tag, "exception-tag");
  SIG("exception-tag", T(LT_EXCEPTION));
  NOREST(1, lt_signal_exception, "signal");
  SIG("signal", T(LT_STRING));
}

/* Function */
lt *lt_eval(lt *form) {
  return run_by_llam(compile_to_bytecode(form));
}

lt *lt_simple_apply(lt *function, lt *args) {
  assert(is_lt_primitive(function) || is_lt_function(function));
  assert(is_lt_pair(args) || isnull(args));
  lt *arity = make_fixnum(pair_length(args));
  lt *code = the_empty_list;
  while (is_lt_pair(args)) {
    lt *arg = pair_head(args);
    code = make_pair(make_op_const(arg), code);
    args = pair_tail(args);
  }
  code = make_pair(make_op_const(function), code);
  code = make_pair(make_op_call(arity), code);
  code = lt_list_nreverse(code);
  code = assemble(code);
  return run_by_llam(code);
}

lt *compress_args(lt *args, int nrequired) {
  lt *lt_list_nreverse(lt *);
  lt *new_args = make_empty_list();
  for (int i = 0; i < nrequired; i++) {
    new_args = make_pair(pair_head(args), new_args);
    args = pair_tail(args);
  }
  lt *rest = make_empty_list();
  while (is_lt_pair(args)) {
    rest = make_pair(pair_head(args), rest);
    args = pair_tail(args);
  }
  new_args = make_pair(lt_list_nreverse(rest), new_args);
  return lt_list_nreverse(new_args);
}

// (a b ...) => ((quote a) (quote b) ...)
lt *quote_each_args(lt *args) {
  if (isnull(args))
    return make_empty_list();
  else
    return
        make_pair(list2(S("quote"), pair_head(args)),
            quote_each_args(pair_tail(args)));
}

lt *macro_fn(lt *macro_name) {
  return symbol_macro(macro_name);
}

lt *lt_expand_macro(lt *form) {
  if (is_macro_form(form)) {
    lt *op = pair_head(form);
    lt *proc = macro_fn(op);
    assert(is_lt_primitive(proc) || is_lt_function(proc));
    lt *result = lt_simple_apply(proc, pair_tail(form));
    return lt_expand_macro(result);
  } else
      return form;
}

lt *lt_function_arity(lt *function) {
  lt *lt_list_length(lt *);
  assert(is_lt_primitive(function) || is_lt_function(function));
  if (is_lt_primitive(function))
    return make_fixnum(primitive_arity(function));
  else
    return make_fixnum(pair_length(function_args(function)));
}

lt *lt_set_function_name(lt *f, lt *name) {
  function_name(f) = name;
  return f;
}

lt *lt_function_cenv(lt *f) {
  return function_cenv(f);
}

lt *lt_function_name(lt *f) {
  return function_name(f);
}

lt *lt_function_renv(lt *f) {
  return function_renv(f);
}

lt *lt_load(lt *path) {
  lt *lt_close_in(lt *);
  lt *lt_open_in(lt *);
  assert(is_lt_string(path));
  lt *file = lt_open_in(path);
  lt *expr = read_object(file);
  while (!iseof(expr)) {
    lt_eval(expr);
    expr = read_object(file);
  }
  lt_close_in(file);
  return make_true();
}

void init_prim_function(void) {
  NOREST(1, lt_eval, "eval");
  NOREST(1, lt_expand_macro, "expand-macro");
  NOREST(1, lt_function_arity, "function-arity");
  NOREST(1, lt_function_cenv, "function-cenv");
  NOREST(1, lt_function_name, "function-name");
  NOREST(1, lt_function_renv, "function-renv");
  NOREST(2, lt_set_function_name, "set-function-name!");
  SIG("set-function-name!", T(LT_FUNCTION), T(LT_SYMBOL));
  NOREST(2, lt_simple_apply, "apply");
}

/* Input Port */
lt *lt_close_in(lt *file) {
  assert(is_lt_input_port(file));
  fclose(input_port_stream(file));
  input_port_openp(file) = FALSE;
  return make_true();
}

lt *lt_is_file_open(lt *file) {
  assert(is_lt_input_port(file) || is_lt_output_port(file));
  if (is_lt_input_port(file))
    return booleanize(input_port_openp(file));
  else
    return booleanize(output_port_openp(file));
}

lt *lt_load_file(lt *file) {
  assert(is_lt_input_port(file));
  lt *expr = read_object(file);
  while (!iseof(expr)) {
    lt_eval(expr);
    expr = read_object(file);
  }
  return make_true();
}

lt *lt_make_input_string_port(lt *str) {
  char *c_str = string_value(str);
  return make_input_string_port(c_str);
}

lt *lt_open_in(lt *path) {
  assert(is_lt_string(path));
  FILE *fp = fopen(string_value(path), "r");
  return make_input_port(fp);
}

lt *lt_read_char(lt *in_port) {
  assert(is_lt_input_port(in_port));
  int c = get_char(in_port);
  if (c == EOF)
    return the_eof;
  else
    return make_character(c);
}

lt *lt_read_line(lt *in_port) {
  assert(is_lt_input_port(in_port));
  string_builder_t *sb = make_str_builder();
  int c = get_char(in_port);
  while (c != EOF && c != '\n') {
    sb_add_char(sb, c);
    c = get_char(in_port);
  }
  return make_string(sb2string(sb));
}

void init_prim_input_port(void) {
  NOREST(1, lt_close_in, "close-in");
  NOREST(1, lt_is_file_open, "file-open?");
  NOREST(1, lt_load, "load");
  NOREST(1, lt_load_file, "load-file");
  NOREST(1, lt_make_input_string_port, "make-input-string-port");
  NOREST(1, lt_open_in, "open-in");
  NOREST(1, lt_read_char, "read-char");
  NOREST(1, lt_read_line, "read-line");
}

/* Arithmetic Operations */
lt *lt_nt_level(lt *n) {
  if (isfixnum(n))
    return make_fixnum(0);
  else
    return make_fixnum(1);
}

/* Arithmetic Operations for Fixnum */
lt *lt_fx_add(lt *n, lt *m) {
  return make_fixnum(fixnum_value(n) + fixnum_value(m));
}

lt *lt_fx_sub(lt *n, lt *m) {
  return make_fixnum(fixnum_value(n) - fixnum_value(m));
}

lt *lt_fx_mul(lt *n, lt *m) {
  return make_fixnum(fixnum_value(n) * fixnum_value(m));
}

lt *lt_fx_div(lt *n, lt *m) {
  if (fixnum_value(m) == 0)
    return signal_exception("Divided by zero");
  return make_fixnum(fixnum_value(n) / fixnum_value(m));
}

lt *lt_fx_eq(lt *n, lt *m) {
  return booleanize(fixnum_value(n) == fixnum_value(m));
}

lt *lt_fx2fp(lt *n) {
  return make_float(fixnum_value(n));
}

/* Arithmetic Operations for Floating-Point Number */
lt *lt_fp_add(lt *n, lt *m) {
  return make_float(float_value(n) + float_value(m));
}

lt *lt_fp_sub(lt *n, lt *m) {
  return make_float(float_value(n) - float_value(m));
}

lt *lt_fp_mul(lt *n, lt *m) {
  return make_float(float_value(n) * float_value(m));
}

lt *lt_fp_div(lt *n, lt *m) {
  if (float_value(m) == 0)
    return signal_exception("Divided by zero");
  return make_float(float_value(n) / float_value(m));
}

lt *lt_fp_eq(lt *n, lt *m) {
  return booleanize(float_value(n) == float_value(m));
}

lt *lt_nt_convert(lt *val, lt *origin, lt *target) {
  if (origin == S("fixnum") && target == S("flonum"))
    return lt_fx2fp(val);
  else
    return signal_exception("Unknown convert rules");
}

lisp_object_t *lt_gt(lisp_object_t *n, lisp_object_t *m) {
  assert(isnumber(n) && isnumber(m));
  if (isfixnum(n) && isfixnum(m))
    return booleanize(fixnum_value(n) > fixnum_value(m));
  if (isfixnum(n) && is_lt_float(m))
    return booleanize(fixnum_value(n) > float_value(m));
  if (is_lt_float(n) && isfixnum(m))
    return booleanize(float_value(n) > fixnum_value(m));
  else
    return booleanize(float_value(n) > float_value(m));
}

lisp_object_t *lt_mod(lisp_object_t *n, lisp_object_t *m) {
  assert(isfixnum(n) && isfixnum(m));
  return make_fixnum(fixnum_value(n) % fixnum_value(m));
}

lisp_object_t *lt_numeric_eq(lisp_object_t *n, lisp_object_t *m) {
  assert(isnumber(n) && isnumber(m));
  if (isfixnum(n) && isfixnum(m))
    return booleanize(fixnum_value(n) == fixnum_value(m));
  if (isfixnum(n) && is_lt_float(m))
    return booleanize(fixnum_value(n) == float_value(m));
  if (is_lt_float(n) && isfixnum(m))
    return booleanize(float_value(n) == fixnum_value(m));
  else
    return booleanize(float_value(n) == float_value(m));
}

// The following function doesn't use in any C code
void init_prim_arithmetic(void) {
  /* For Fixnum */
  NOREST(1, lt_fx2fp, "fx->fp");
  NOREST(2, lt_fx_add, "fx+");
  NOREST(2, lt_fx_div, "fx/");
  NOREST(2, lt_fx_eq, "fx=");
  NOREST(2, lt_fx_mul, "fx*");
  NOREST(2, lt_fx_sub, "fx-");
  NOREST(2, lt_mod, "mod");
  /* For Floating-Point Number */
  NOREST(2, lt_fp_add, "fp+");
  NOREST(2, lt_fp_div, "fp/");
  NOREST(2, lt_fp_eq, "fp=");
  NOREST(2, lt_fp_mul, "fp*");
  NOREST(2, lt_fp_sub, "fp-");
  NOREST(3, lt_nt_convert, "nt-convert");
  NOREST(1, lt_nt_level, "nt-level");
  /* Generic */
  NOREST(2, lt_gt, ">");
}

/* Character */
lisp_object_t *lt_char_code(lisp_object_t *c) {
  assert(ischar(c));
  return make_fixnum(character_value(c));
}

lisp_object_t *lt_code_char(lisp_object_t *code) {
  assert(isfixnum(code));
  return make_character(fixnum_value(code));
}

void init_prim_char(void) {
  NOREST(1, lt_char_code, "char-code");
  SIG("char-code", T(LT_CHARACTER));
  NOREST(1, lt_code_char, "code-char");
  SIG("code-char", T(LT_FIXNUM));
}

/* Output File */
lt *lt_close_out(lt *file) {
  assert(is_lt_output_port(file));
  fclose(output_port_stream(file));
  output_port_openp(file) = FALSE;
  return make_true();
}

lt *lt_open_out(lt *path) {
  assert(is_lt_string(path));
  FILE *fp = fopen(string_value(path), "w");
  return make_output_port(fp);
}

lt *lt_write_char(lt *c, lt *dest) {
  write_raw_char(character_value(c), dest);
  return c;
}

lt *lt_write_string(lt *str, lt *dest) {
  write_raw_string(string_value(str), dest);
  return str;
}

lt *lt_write_object(lt *object, lt *dest) {
  write_object(object, dest);
  return the_true;
}

void init_prim_output_port(void) {
  NOREST(1, lt_open_in, "open-in");
  NOREST(1, lt_open_out, "open-out");
  NOREST(2, lt_write_char, "write-char");
  NOREST(2, lt_write_object, "write-object");
  NOREST(2, lt_write_string, "write-string");
}

/* Package */
lt *lt_in_package(lt *name) {
  lt *pkg = search_package(string_value(name), pkgs);
  if (pkg) {
    package = pkg;
    return the_true;
  } else
    return signal_exception("Undefined package with the name");
}

lt *lt_make_package(lt *name) {
  return ensure_package(string_value(name));
}

lt *lt_package_name(lt *pkg) {
  return package_name(pkg);
}

void init_prim_package(void) {
  NOREST(1, lt_in_package, "in-package");
  NOREST(1, lt_make_package, "make-package");
  NOREST(1, lt_package_name, "package-name");
}

/* String */
lisp_object_t *lt_char_at(lisp_object_t *string, lisp_object_t *index) {
  assert(is_lt_string(string) && isfixnum(index));
  assert(strlen(string_value(string)) > fixnum_value(index));
  return make_character(string_value(string)[fixnum_value(index)]);
}

lt *lt_string_length(lt *str) {
  assert(is_lt_string(str));
  return make_fixnum(string_length(str));
}

lt *lt_string_set(lt *string, lt *index, lt *new_char) {
  assert(is_lt_string(string));
  assert(isfixnum(index));
  assert(ischar(new_char));
  string_value(string)[fixnum_value(index)] = character_value(new_char);
  return string;
}

void init_prim_string(void) {
  NOREST(2, lt_char_at, "char-at");
  NOREST(1, lt_string_length, "string-length");
  NOREST(3, lt_string_set, "string-set!");
}

/* Symbol */
lt *lt_gensym(void) {
  static char sym[256];
  int n = sprintf(sym, "G%d", fixnum_value(gensym_counter));
  gensym_counter = make_fixnum(fixnum_value(gensym_counter) + 1);
  return S(strndup(sym, n));
}

lt *lt_intern(lt *name, lt *pkg_name) {
  lt *pkg = ensure_package(string_value(pkg_name));
  return find_or_create_symbol(string_value(name), pkg);
}

lt *lt_is_bound(lt *symbol) {
  assert(is_lt_symbol(symbol));
  return booleanize(!isundef(symbol_value(symbol)));
}

lt *lt_set_symbol_macro(lt *symbol, lt *macro_fn) {
  symbol_macro(symbol) = macro_fn;
  return symbol;
}

lt *lt_set_symbol_value(lt *symbol, lt *value) {
  symbol_value(symbol) = value;
  return value;
}

lt *lt_symbol_macro(lt *symbol) {
  return symbol_macro(symbol);
}

lisp_object_t *lt_symbol_name(lisp_object_t *symbol) {
  assert(is_lt_symbol(symbol));
  return make_string(strdup(symbol_name(symbol)));
}

lt *lt_symbol_package(lt *symbol) {
  return symbol_package(symbol);
}

lisp_object_t *lt_symbol_value(lisp_object_t *symbol) {
  assert(is_lt_symbol(symbol));
  return symbol_value(symbol);
}

void init_prim_symbol(void) {
  NOREST(0, lt_gensym, "gensym");
  NOREST(2, lt_intern, "intern");
  NOREST(1, lt_is_bound, "bound?");
  SIG("bound?", T(LT_SYMBOL));
  NOREST(2, lt_set_symbol_macro, "set-symbol-macro!");
  NOREST(2, lt_set_symbol_value, "set-symbol-value!");
  NOREST(1, lt_symbol_macro, "symbol-macro");
  NOREST(1, lt_symbol_name, "symbol-name");
  NOREST(1, lt_symbol_package, "symbol-package");
  NOREST(1, lt_symbol_value, "symbol-value");
}

/* Vector */
lisp_object_t *lt_is_vector_empty(lisp_object_t *vector) {
  assert(is_lt_vector(vector));
  return booleanize(vector_last(vector) <= -1);
}

lisp_object_t *lt_is_vector_full(lisp_object_t *vector) {
  assert(is_lt_vector(vector));
  return booleanize(vector_last(vector) >= vector_length(vector) - 1);
}

lisp_object_t *lt_list_to_vector(lisp_object_t *list) {
  assert(is_lt_pair(list) || isnull(list));
  int len = pair_length(list);
  lisp_object_t *vector = make_vector(len);
  for (int i = 0; i < len; i++) {
    vector_value(vector)[i] = pair_head(list);
    vector_last(vector)++;
    list = pair_tail(list);
  }
  return vector;
}

lt *lt_vector_equal(lt *v1, lt *v2) {
  if (v1 == v2) return the_true;
  if (vector_length(v1) != vector_length(v2))
    return the_false;
  for (int i = 0; i < vector_length(v1); i++) {
    if (isfalse(lt_equal(vector_value(v1)[i], vector_value(v2)[i])))
      return the_false;
  }
  return the_true;
}

lisp_object_t *lt_vector_last_nth(lisp_object_t *vector, lisp_object_t *n) {
  assert(is_lt_vector(vector) && isfixnum(n));
  assert(isfalse(lt_is_vector_empty(vector)));
  assert(vector_last(vector) >= fixnum_value(n));
  int index = vector_last(vector) - fixnum_value(n);
  return vector_value(vector)[index];
}

lt *lt_vector_length(lt *vector) {
  return make_fixnum(vector_length(vector));
}

lisp_object_t *lt_vector_pop(lisp_object_t *vector) {
  assert(is_lt_vector(vector));
  if (!isfalse(lt_is_vector_empty(vector))) {
    fprintf(stdout, "The vector is empty\n");
    exit(1);
  }
  vector_last(vector)--;
  return vector_value(vector)[vector_last(vector) + 1];
}

lisp_object_t *lt_vector_push(lisp_object_t *vector, lisp_object_t *object) {
  assert(is_lt_vector(vector));
  if (!isfalse(lt_is_vector_full(vector))) {
    fprintf(stdout, "The vector is full\n");
    exit(1);
  }
  vector_last(vector)++;
  vector_value(vector)[vector_last(vector)] = object;
  return vector;
}

lt *lt_vector_push_extend(lt *vector, lt *x) {
  if (isfalse(lt_is_vector_full(vector)))
    return lt_vector_push(vector, x);
  else {
    int length = vector_length(vector) + 1;
    lt **value = GC_MALLOC(length * sizeof(lt *));
    for (int i = 0; i < length - 1; i++)
      value[i] = vector_value(vector)[i];
    vector_value(vector) = value;
    vector_length(vector)++;
    lt_vector_push(vector, x);
    return make_fixnum(vector_last(vector));
  }
}

lisp_object_t *lt_vector_ref(lisp_object_t *vector, lisp_object_t *index) {
  assert(is_lt_vector(vector));
  assert(isfixnum(index));
  if (vector_last(vector) < fixnum_value(index))
    return signal_exception("Out of index when referencing a vector element");
  return vector_value(vector)[fixnum_value(index)];
}

lt *lt_vector_set(lt *vector, lt *index, lt *new_value) {
  if (!is_lt_vector(vector))
    return signal_typerr("VECTOR");
  if (!isfixnum(index))
    return signal_typerr("FIXNUM");
  if (!(vector_length(vector) > fixnum_value(index)))
    return signal_exception("The second argument is too large to index.");
  vector_value(vector)[fixnum_value(index)] = new_value;
  return vector;
}

lt *lt_vector_to_list(lt *vector) {
  int length = vector_length(vector);
  lt *list = make_empty_list();
  for (int i = 0; i < length; i++) {
    list = make_pair(vector_value(vector)[i], list);
  }
  return lt_list_nreverse(list);
}

void init_prim_vector(void) {
  NOREST(1, lt_list_to_vector, "list->vector");
  NOREST(1, lt_vector_length, "vector-length");
  NOREST(1, lt_vector_pop, "vector-pop");
  NOREST(2, lt_vector_push, "vector-push");
  NOREST(2, lt_vector_push_extend, "vector-push-extend");
  NOREST(2, lt_vector_ref, "vector-ref");
  NOREST(3, lt_vector_set, "vector-set!");
  NOREST(1, lt_vector_to_list, "vector->list");
}

/* List */
lt *lt_list_nreverse(lt *list) {
  if (isnull(list))
    return the_empty_list;
  if (isnull(pair_tail(list)))
    return list;
  lt *rhead = the_empty_list;
  lt *rest = list;
  while (!isnull(rest)) {
    if (!is_lt_pair(rest))
      return signal_exception("Argument is not a proper list.");
    lt *tmp = pair_tail(rest);
    pair_tail(rest) = rhead;
    rhead = rest;
    rest = tmp;
  }
  return rhead;
}

lt *raw_list(lt *e0, ...) {
  va_list ap;
  va_start(ap, e0);
  e0 = list1(e0);
  lt *next = va_arg(ap, lt *);
  while (next != NULL) {
    e0 = make_pair(next, e0);
    next = va_arg(ap, lt *);
  }
  return lt_list_nreverse(e0);
}

lisp_object_t *lt_head(lisp_object_t *pair) {
  assert(is_lt_pair(pair));
  return pair_head(pair);
}

lt *lt_list_equal(lt *l1, lt *l2) {
  if (l1 == l2)
    return the_true;
  while (!isnull(l1) && !isnull(l2)) {
    if (isfalse(lt_equal(pair_head(l1), pair_head(l2))))
      return the_false;
    l1 = pair_tail(l1);
    l2 = pair_tail(l2);
  }
  if (!isnull(l1) || !isnull(l2))
    return the_false;
  else
    return the_true;
}

lisp_object_t *lt_set_head(lisp_object_t *pair, lisp_object_t *new_head) {
  assert(is_lt_pair(pair));
  pair_head(pair) = new_head;
  return pair;
}

lisp_object_t *lt_set_tail(lisp_object_t *pair, lisp_object_t *new_tail) {
  assert(is_lt_pair(pair));
  pair_tail(pair) = new_tail;
  return pair;
}

lisp_object_t *lt_tail(lisp_object_t *pair) {
  assert(is_lt_pair(pair));
  return pair_tail(pair);
}

void init_prim_list(void) {
  NOREST(2, make_pair, "cons");
  NOREST(1, lt_head, "head");
  NOREST(1, lt_list_nreverse, "list-reverse!");
  NOREST(2, lt_set_head, "set-head");
  NOREST(2, lt_set_tail, "set-tail");
  NOREST(1, lt_tail, "tail");
}

/* General */
lisp_object_t *lt_eq(lisp_object_t *x, lisp_object_t *y) {
  return booleanize(x == y);
}

lt *lt_eql(lt *x, lt *y) {
  if (x == y)
    return the_true;
  if (isnumber(x) && isnumber(y))
    return lt_numeric_eq(x, y);
  if (ischar(x) && ischar(y))
    return booleanize(character_value(x) == character_value(y));
  return the_false;
}

lt *lt_equal(lt *x, lt *y) {
  if (!isfalse(lt_eql(x, y)))
    return the_true;
  if (is_lt_pair(x) && is_lt_pair(y))
    return lt_list_equal(x, y);
  if (is_lt_vector(x) && is_lt_vector(y))
    return lt_vector_equal(x, y);
  return the_false;
}

lt *lt_is_constant(lt *object) {
  if (is_tag_list(object, S("quote")))
    return make_true();
  if (!is_lt_pair(object) && !is_lt_symbol(object))
    return make_true();
  return make_false();
}

lt *lt_object_size(void) {
  return make_fixnum(sizeof(lt));
}

lisp_object_t *lt_type_of(lisp_object_t *object) {
  return type_ref(type_of(object));
}

lt *lt_is_kind_of(lt *object, lt *type) {
  return lt_eq(lt_type_of(object), type);
}

lt *lt_switch_debug(void) {
  if (debug)
    debug = FALSE;
  else
    debug = TRUE;
  return booleanize(debug);
}

lt *lt_switch_exception_check(void) {
  if (is_check_exception)
    is_check_exception = FALSE;
  else
    is_check_exception = TRUE;
  return booleanize(is_check_exception);
}

lt *lt_switch_type_check(void) {
  if (is_check_type)
    is_check_type = FALSE;
  else
    is_check_type = TRUE;
  return booleanize(is_check_type);
}

lt *lt_type_name(lt *type) {
  return LISP(type_name(type));
}

void init_prim_general(void) {
  /* Type */
  NOREST(1, lt_type_name, "type-name");
  SIG("type-name", T(LT_TYPE));
  /* General */
  NOREST(1, lt_is_constant, "is-constant?");
  NOREST(2, lt_eq, "eq?");
  NOREST(2, lt_eql, "eql?");
  NOREST(2, lt_equal, "equal?");
  NOREST(2, lt_is_kind_of, "of-type?");
  NOREST(0, lt_object_size, "object-size");
  NOREST(1, lt_type_of, "type-of");
  NOREST(0, lt_switch_debug, "switch-debug");
  NOREST(0, lt_switch_exception_check, "switch-exception-check");
  NOREST(0, lt_switch_type_check, "switch-type-check");
}

/* Reader */
int peek_char(lisp_object_t *input) {
  assert(is_lt_input_port(input));
  FILE *in = input_port_stream(input);
  int c = getc(in);
  ungetc(c, in);
  return c;
}

void unget_char(int c, lisp_object_t *input) {
  assert(is_lt_input_port(input));
  ungetc(c, input_port_stream(input));
  input_port_colnum(input)--;
}

int isdelimiter(int c) {
  int ds[] = { EOF, ' ', '\n', '(', ')', '"', '[', ']', ';', '\0'};
  for (int i = 0; i < sizeof(ds) / sizeof(int); i++) {
    if (ds[i] == c)
      return TRUE;
  }
  return FALSE;
}

lt *expect_string(char *target, lisp_object_t *input_file) {
  while (*target != '\0') {
    int c = get_char(input_file);
    if (c != *target) {
      return reader_error("Unexpected character '%c'. Expecting '%c'", c, *target);
    }
    target++;
  }
  return the_empty_list;
}

lisp_object_t *read_character(lisp_object_t *input_file) {
  int c = get_char(input_file);
  lt *tmp;
  switch (c) {
    case 's':
      c = peek_char(input_file);
      if (isdelimiter(c))
        return make_character('s');
      tmp = expect_string("pace", input_file);
      if (is_signaled(tmp))
        return tmp;
      return make_character(' ');
    case 'n':
      c = peek_char(input_file);
      if (isdelimiter(c))
        return make_character('n');
      tmp = expect_string("ewline", input_file);
      if (is_signaled(tmp))
        return tmp;
      return make_character('\n');
    default : {
      int c2 = peek_char(input_file);
      if (isdelimiter(c2))
        return make_character(c);
      else {
        fprintf(stdout, "Unexpected character '%c' after '%c'\n", c2, c);
        exit(1);
      }
    }
  }
}

lisp_object_t *read_float(lisp_object_t *input_file, int integer) {
  int e = 1;
  int sum = 0;
  int c = get_char(input_file);
  for (; isdigit(c); c = get_char(input_file)) {
    e *= 10;
    sum = sum * 10 + c - '0';
  }
  unget_char(c, input_file);
  return make_float(integer + sum * 1.0 / e);
}

lisp_object_t *read_fixnum(lisp_object_t *input_file, int sign, char start) {
  int sum = start - '0';
  int c = get_char(input_file);
  for (; isdigit(c); c = get_char(input_file)) {
    sum = sum * 10 + c - '0';
  }
  if (c == '.') {
    lt *flonum = read_float(input_file, sum);
    float_value(flonum) = sign * float_value(flonum);
    return flonum;
  } else
    unget_char(c, input_file);
  return make_fixnum(sign * sum);
}

lisp_object_t *read_pair(lisp_object_t *input_file) {
  lisp_object_t *obj = read_object(input_file);
  if (iseof(obj))
    return reader_error("Unexpected end-of-file.");
  if (is_signaled(obj))
    return obj;
  if (isclose(obj))
    return the_empty_list;
  if (isdot(obj)) {
    lisp_object_t *o1 = read_object(input_file);
    if (is_signaled(o1))
      return o1;
    lisp_object_t *o2 = read_object(input_file);
    if (isclose(o1))
      return reader_error("Too few tokens after dot");
    else if (isclose(o2))
      return o1;
    else
      return reader_error("multiple tokens in dotted tail");
  } else {
    lisp_object_t *tail = read_pair(input_file);
    if (is_signaled(tail))
      return tail;
    else
      return make_pair(obj, tail);
  }
}

lisp_object_t *read_string(lisp_object_t *input_file) {
  string_builder_t *buffer = make_str_builder();
  for (;;) {
    int c = get_char(input_file);
    if (c == '"')
      return make_string(sb2string(buffer));
    if (c == '\\') {
      c = get_char(input_file);
      switch (c) { case 'n': c = '\n'; break; case 't': c = '\t'; break;}
    }
    if (c == EOF)
      return reader_error("Reading string. Unexpected end-of-file.");
    sb_add_char(buffer, c);
  }
  return reader_error("The string is too long");
}

lisp_object_t *read_symbol(char start, lisp_object_t *input_file) {
  string_builder_t *buffer = make_str_builder();
  sb_add_char(buffer, start);
  int c = get_char(input_file);
  int i = 1;
  for (; !isdelimiter(c); i++) {
    sb_add_char(buffer, c);
    c = get_char(input_file);
  }
  if (isdelimiter(c) && c != EOF)
    unget_char(c, input_file);
  return S(sb2string(buffer));
}

lisp_object_t *read_vector(lisp_object_t *input_file) {
  lisp_object_t *list = read_pair(input_file);
  return lt_list_to_vector(list);
}

lisp_object_t *read_object(lisp_object_t *input_file) {
  int c = get_char(input_file);
  switch (c) {
    case ';':
      while ((c = get_char(input_file)) != EOF && c != '\n');
      return read_object(input_file);
    case EOF:
    	return make_eof();
    case '\n': case '\r': case '\t':
    	input_port_linum(input_file)++;
    	return read_object(input_file);
    case ' ':
    	return read_object(input_file);
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return read_fixnum(input_file, 1, c);
    case '-':
      if (isdigit(peek_char(input_file)))
        return read_fixnum(input_file, -1, '0');
      else
        goto read_symbol_label;
    case '#':
      c = get_char(input_file);
      switch (c) {
        case '\\':
        	return read_character(input_file);
        case 't':
          if (isdelimiter(peek_char(input_file)))
            return the_true;
          else
          	goto bool_error_label;
        case 'f':
          if (isdelimiter(peek_char(input_file)))
            return the_false;
          else
          	goto bool_error_label;
        default : {
        	bool_error_label:
          return reader_error("Unexpected character '%c' after '#', at line %d, column %d",
              c, input_port_linum(input_file), input_port_colnum(input_file));
        }
      }
      break;
    case '"':
    	return read_string(input_file);
    case '(': {
      lisp_object_t *head = read_object(input_file);
      if (isclose(head))
        return the_empty_list;
      lisp_object_t *tail = read_pair(input_file);
      if (is_signaled(tail))
        return tail;
      else
        return make_pair(head, tail);
    }
    case ']': case ')':
    	return make_close();
    case '.':
    	return the_dot_symbol;
    case '[':
    	return read_vector(input_file);
    case '\'':
      return list2(the_quote_symbol, read_object(input_file));
    case '`':
      return list2(the_quasiquote_symbol, read_object(input_file));
    case ',': {
      c = get_char(input_file);
      if (c == '@')
        return list2(the_splicing_symbol, read_object(input_file));
      unget_char(c, input_file);
      return list2(the_unquote_symbol, read_object(input_file));
    }
      break;
    default :
    read_symbol_label:
      return read_symbol(c, input_file);
  }
}

lisp_object_t *read_object_from_string(char *text) {
  FILE *file = fmemopen(text, strlen(text), "r");
  lt *inf = make_input_port(file);
  lt *obj = read_object(inf);
  return obj;
}

lt *lt_read_from_string(lt *string) {
  return read_object_from_string(string_value(string));
}

void init_prim_reader(void) {
  NOREST(1, lt_read_from_string, "read-from-string");
}

void init_prims(void) {
  init_prim_arithmetic();
  init_prim_char();
  init_prim_exception();
  init_prim_function();
  init_prim_general();
  init_prim_input_port();
  init_prim_list();
  init_prim_output_port();
  init_prim_package();
  init_prim_reader();
  init_prim_string();
  init_prim_symbol();
  init_prim_vector();
}

void init_primitive_opcode(void) {
#define ADDOP(Lisp_name, opcode) \
  do { \
    lt *func = symbol_value(LISP(Lisp_name)); \
    set_op4prim(func, opcode); \
  } while (0)

  ADDOP("fx+", ADDI);
  ADDOP("cons", CONS);
  ADDOP("fx/", DIVI);
  ADDOP("fx*", MULI);
  ADDOP("fx-", SUBI);
}

void load_init_file(void) {
  const char *init_file = "init.scm";
  FILE *fp = fopen(init_file, "r");
  if (fp == NULL) {
    fprintf(stderr, "INFO: No initialization file.\n");
    return;
  }
  lt *file = make_input_port(fp);
  lt_load_file(file);
}
