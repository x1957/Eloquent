/*
 * object.c
 *
 *  Created on: 2013年7月18日
 *      Author: liutos
 *
 * This file contains the constructor of all Lisp data types
 */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gc/gc.h>

#include "hash_table.h"
#include "object.h"
#include "type.h"

int debug;
int is_check_exception;
int is_check_type;
lt *gensym_counter;
lt *null_env;
/* Opcode */
int opcode_max_length;
hash_table_t *prim2op_map;
/* Package */
lt *package;
lt *pkg_lisp;
lt *pkg_user;
lt *pkgs;

lt *standard_error;
lt *standard_in;
lt *standard_out;
lt *symbol_list;
/* Symbol */
lt *the_begin_symbol;
lt *the_catch_symbol;
lt *the_dot_symbol;
lt *the_goto_symbol;
lt *the_if_symbol;
lt *the_lambda_symbol;
lt *the_quasiquote_symbol;
lt *the_quote_symbol;
lt *the_set_symbol;
lt *the_splicing_symbol;
lt *the_tagbody_symbol;
lt *the_unquote_symbol;

lt *the_argv;
lt *the_empty_list;
lt *the_eof;
lt *the_false;
lt *the_true;
lt *the_undef;

#define DEFTYPE(tag, name) {.type=LT_TYPE, .u={.type={tag, name}}}

struct lisp_object_t lt_types[] = {
    DEFTYPE(LT_BOOL, "bool"),
    DEFTYPE(LT_CHARACTER, "character"),
    DEFTYPE(LT_EMPTY_LIST, "empty-list"),
    DEFTYPE(LT_FIXNUM, "fixnum"),
    DEFTYPE(LT_TCLOSE, "tclose"),
    DEFTYPE(LT_TEOF, "teof"),
    DEFTYPE(LT_TUNDEF, "tundef"),
    DEFTYPE(LT_ENVIRONMENT, "environment"),
    DEFTYPE(LT_EXCEPTION, "exception"),
    DEFTYPE(LT_FUNCTION, "function"),
    DEFTYPE(LT_FLOAT, "float"),
    DEFTYPE(LT_INPUT_PORT, "input-file"),
    DEFTYPE(LT_OPCODE, "opcode"),
    DEFTYPE(LT_OUTPUT_PORT, "output-file"),
    DEFTYPE(LT_PACKAGE, "package"),
    DEFTYPE(LT_PAIR, "pair"),
    DEFTYPE(LT_PRIMITIVE, "primitive-function"),
    DEFTYPE(LT_RETADDR, "retaddr"),
    DEFTYPE(LT_STRING, "string"),
    DEFTYPE(LT_SYMBOL, "symbol"),
    DEFTYPE(LT_TYPE, "type"),
    DEFTYPE(LT_UNICODE, "unicode"),
    DEFTYPE(LT_VECTOR, "vector"),
};

#define DEFCODE(name) {.type=LT_OPCODE, .u={.opcode={name, #name}}}

struct lisp_object_t lt_codes[] = {
    DEFCODE(CALL),
    DEFCODE(CATCH),
    DEFCODE(CHECKEX),
    DEFCODE(CHKARITY),
    DEFCODE(CHKTYPE),
    DEFCODE(CONST),
    DEFCODE(EXTENV),
    DEFCODE(FN),
    DEFCODE(GSET),
    DEFCODE(GVAR),
    DEFCODE(FJUMP),
    DEFCODE(JUMP),
    DEFCODE(LSET),
    DEFCODE(LVAR),
    DEFCODE(MOVEARGS),
    DEFCODE(POP),
    DEFCODE(POPENV),
    DEFCODE(PRIM),
    DEFCODE(RESTARGS),
    DEFCODE(RETURN),
//    Opcodes for some primitive functions
    DEFCODE(ADDI),
    DEFCODE(CONS),
    DEFCODE(DIVI),
    DEFCODE(MULI),
    DEFCODE(SUBI),
};

/* Type predicate */
int ischar(lt *object) {
  return ((intptr_t)object & CHAR_MASK) == CHAR_TAG;
}

int isfixnum(lt *object) {
  return ((intptr_t)object & FIXNUM_MASK) == FIXNUM_TAG;
}

int is_pointer(lt *object) {
  return ((intptr_t)object & POINTER_MASK) == POINTER_TAG;
}

int is_of_type(lisp_object_t *object, enum TYPE type) {
  return is_pointer(object) && (object->type == type? TRUE: FALSE);
}

#define mktype_pred(func_name, type)            \
  int func_name(lisp_object_t *object) {        \
    return is_of_type(object, type);            \
  }

mktype_pred(is_lt_environment, LT_ENVIRONMENT)
mktype_pred(is_lt_exception, LT_EXCEPTION)
mktype_pred(is_lt_float, LT_FLOAT)
mktype_pred(is_lt_function, LT_FUNCTION)
mktype_pred(is_lt_input_port, LT_INPUT_PORT)
mktype_pred(is_lt_output_port, LT_OUTPUT_PORT)
mktype_pred(is_lt_opcode, LT_OPCODE)
mktype_pred(is_lt_pair, LT_PAIR)
mktype_pred(is_lt_primitive, LT_PRIMITIVE)
mktype_pred(is_lt_string, LT_STRING)
mktype_pred(is_lt_symbol, LT_SYMBOL)
mktype_pred(is_lt_type, LT_TYPE)
mktype_pred(is_lt_unicode, LT_UNICODE)
mktype_pred(is_lt_vector, LT_VECTOR)

int is_immediate(lt *object) {
  return ((intptr_t)object & IMMEDIATE_MASK) == IMMEDIATE_TAG;
}

int is_tag_immediate(lt *object, int origin) {
  return is_immediate(object) && ((intptr_t)object >> IMMEDIATE_BITS) == origin;
}

#define mkim_pred(func_name, origin)		      \
  int func_name(lt *object) {			            \
	  return is_tag_immediate(object, origin);	\
  }

mkim_pred(iseof, EOF_ORIGIN)
mkim_pred(isnull, NULL_ORIGIN)
mkim_pred(isfalse, FALSE_ORIGIN)
mkim_pred(is_true_object, TRUE_ORIGIN)
mkim_pred(isundef, UNDEF_ORIGIN)
mkim_pred(isclose, CLOSE_ORIGIN)

int isboolean(lisp_object_t *object) {
  return isfalse(object) || is_true_object(object);
}

int is_signaled(lisp_object_t *object) {
  return is_lt_exception(object) && exception_flag(object) == TRUE;
}

int isdot(lisp_object_t *object) {
  return object == the_dot_symbol;
}

int isnull_env(lt *obj) {
  return obj == null_env;
}

int isnumber(lisp_object_t *object) {
  return isfixnum(object) || is_lt_float(object);
}

int type_of(lisp_object_t *x) {
  if (isboolean(x))
    return LT_BOOL;
  if (ischar(x))
    return LT_CHARACTER;
  if (isnull(x))
    return LT_EMPTY_LIST;
  if (isfixnum(x))
    return LT_FIXNUM;
  if (isclose(x))
    return LT_TCLOSE;
  if (iseof(x))
    return LT_TEOF;
  if (isundef(x))
    return LT_TUNDEF;
  assert(is_pointer(x));
  return x->type;
}

/* Constructor functions */
lt *allocate_object(void) {
  return GC_MALLOC(sizeof(struct lisp_object_t));
}

lisp_object_t *make_object(enum TYPE type) {
  lt *obj = allocate_object();
  obj->type = type;
  return obj;
}

#define MAKE_IMMEDIATE(origin) \
  ((lt *)(((intptr_t)origin << IMMEDIATE_BITS) | IMMEDIATE_TAG))

#define mksingle_type(func_name, origin)    \
  lt *func_name(void) {             \
    return MAKE_IMMEDIATE(origin);      \
  }

mksingle_type(make_false, FALSE_ORIGIN)
mksingle_type(make_true, TRUE_ORIGIN)
mksingle_type(make_empty_list, NULL_ORIGIN)
mksingle_type(make_eof, EOF_ORIGIN)
mksingle_type(make_undef, UNDEF_ORIGIN)
mksingle_type(make_close, CLOSE_ORIGIN)

lisp_object_t *make_character(char value) {
  return (lt *)((((intptr_t)value) << CHAR_BITS) | CHAR_TAG);
}

lt *make_fixnum(int value) {
  return (lt *)((value << FIXNUM_BITS) | FIXNUM_TAG);
}

lt *make_environment(lt *bindings, lt *next) {
  lt *env = make_object(LT_ENVIRONMENT);
  environment_bindings(env) = bindings;
  environment_next(env) = next;
  return env;
}

lt *make_exception(char *message, int signal_flag, lt *tag, lt *backtrace) {
  lt *ex = make_object(LT_EXCEPTION);
  exception_msg(ex) = message;
  exception_flag(ex) = signal_flag;
  exception_backtrace(ex) = backtrace;
  exception_tag(ex) = tag;
  return ex;
}

lisp_object_t *make_float(float value) {
  lisp_object_t *flt_num = make_object(LT_FLOAT);
  float_value(flt_num) = value;
  return flt_num;
}

lt *make_function(lt *cenv, lt *args, lt *code, lt *renv) {
  lt *func = make_object(LT_FUNCTION);
  function_cenv(func) = cenv;
  function_args(func) = args;
  function_code(func) = code;
  function_name(func) = the_undef;
  function_renv(func) = renv;
  return func;
}

lt *make_input_port(FILE *stream) {
  lt *inf = make_object(LT_INPUT_PORT);
  input_port_stream(inf) = stream;
  input_port_linum(inf) = 1;
  input_port_colnum(inf) = 0;
  input_port_openp(inf) = TRUE;
  return inf;
}

lt *make_input_string_port(char *str) {
  FILE *stream = fmemopen(str, strlen(str), "r");
  return make_input_port(stream);
}

lt *make_output_port(FILE *stream) {
  lt *outf = make_object(LT_OUTPUT_PORT);
  output_port_stream(outf) = stream;
  output_port_linum(outf) = 1;
  output_port_colnum(outf) = 0;
  output_port_openp(outf) = TRUE;
  return outf;
}

lt *make_output_string_port(char *str) {
  FILE *stream = fmemopen(str, strlen(str), "w");
  return make_output_port(stream);
}

lt *make_package(lt *name, hash_table_t *symbol_table) {
  lt *obj = make_object(LT_PACKAGE);
  package_name(obj) = name;
  package_symbol_table(obj) = symbol_table;
  package_used_packages(obj) = the_empty_list;
  return obj;
}

lisp_object_t *make_pair(lisp_object_t *head, lisp_object_t *tail) {
  lisp_object_t *pair = make_object(LT_PAIR);
  pair_head(pair) = head;
  pair_tail(pair) = tail;
  return pair;
}

lisp_object_t *make_primitive(int arity, void *C_function, char *Lisp_name, int restp) {
  lisp_object_t *p = make_object(LT_PRIMITIVE);
  primitive_arity(p) = arity;
  primitive_func(p) = C_function;
  primitive_restp(p) = restp;
  primitive_Lisp_name(p) = Lisp_name;
  primitive_signature(p) = make_empty_list();
  return p;
}

lt *make_retaddr(lt *code, lt *env, lt *fn, int pc, int throw_flag, int sp) {
  lt *retaddr = make_object(LT_RETADDR);
  retaddr_code(retaddr) = code;
  retaddr_env(retaddr) = env;
  retaddr_fn(retaddr) = fn;
  retaddr_pc(retaddr) = pc;
  retaddr_throw_flag(retaddr) = throw_flag;
  return retaddr;
}

string_builder_t *make_str_builder(void) {
  string_builder_t *sb = GC_MALLOC(sizeof(*sb));
  sb->length = 20;
  sb->string = GC_MALLOC(sb->length * sizeof(char));
  sb->index = 0;
  return sb;
}

lt *make_string(char *value) {
  lt *string = make_object(LT_STRING);
  string_length(string) = strlen(value);
  string_value(string) = value;
  return string;
}

lisp_object_t *make_symbol(char *name, lt *package) {
  lisp_object_t *symbol = make_object(LT_SYMBOL);
  symbol_name(symbol) = name;
  symbol_macro(symbol) = the_undef;
  symbol_package(symbol) = package;
  symbol_value(symbol) = the_undef;
  return symbol;
}

lt *make_type(enum TYPE type, char *name) {
  lt *t = make_object(LT_TYPE);
  type_tag(t) = type;
  type_name(t) = name;
  return t;
}

lt *make_unicode(char *data) {
  lt *obj = make_object(LT_UNICODE);
  unicode_data(obj) = data;
  return obj;
}

lisp_object_t *make_vector(int length) {
  lisp_object_t *vector = make_object(LT_VECTOR);
  vector_last(vector) = -1;
  vector_length(vector) = length;
  vector_value(vector) = GC_MALLOC(length * sizeof(lisp_object_t *));
  return vector;
}

/* Opcode constructor functions */
lt *make_opcode(enum OPCODE_TYPE name, char *op, lt *oprands) {
  lt *obj = make_object(LT_OPCODE);
  opcode_name(obj) = name;
  opcode_op(obj) = op;
  opcode_oprands(obj) = oprands;
  return obj;
}

lt *mkopcode(enum OPCODE_TYPE name, int arity, ...) {
  lt *oprands = make_vector(arity);
  va_list ap;
  va_start(ap, arity);
  for (int i = 0; i < arity; i++)
    vector_value(oprands)[i] = va_arg(ap, lt *);
  vector_last(oprands) = arity - 1;
  return make_opcode(name, opcode_op(opcode_ref(name)), oprands);
}

lisp_object_t *make_op_call(lisp_object_t *arity) {
  return mkopcode(CALL, 1, arity);
}

lt *make_op_checkex(void) {
  return mkopcode(CHECKEX, 0);
}

lt *make_op_chkarity(lt *arity) {
  return mkopcode(CHKARITY, 1, arity);
}

lt *make_op_chktype(lt *position, lt *target_type, lt *nargs) {
  return mkopcode(CHKTYPE, 3, position, target_type, nargs);
}

lisp_object_t *make_op_const(lisp_object_t *value) {
  return mkopcode(CONST, 1, value);
}

lt *make_op_extenv(lt *count) {
  return mkopcode(EXTENV, 1, count);
}

lisp_object_t *make_op_fjump(lisp_object_t *label) {
  return mkopcode(FJUMP, 1, label);
}

lisp_object_t *make_op_fn(lisp_object_t *func) {
  return mkopcode(FN, 1, func);
}

lisp_object_t *make_op_gset(lisp_object_t *symbol) {
  return mkopcode(GSET, 1, symbol);
}

lisp_object_t *make_op_gvar(lisp_object_t *symbol) {
  return mkopcode(GVAR, 1, symbol);
}

lisp_object_t *make_op_jump(lisp_object_t *label) {
  return mkopcode(JUMP, 1, label);
}

lt *make_op_lset(lt *i, lt *j, lt *symbol) {
  return mkopcode(LSET, 3, i, j, symbol);
}

lt *make_op_lvar(lt *i, lt *j, lt *symbol) {
  return mkopcode(LVAR, 3, i, j, symbol);
}

lt *make_op_moveargs(lt *count) {
  return mkopcode(MOVEARGS, 1, count);
}

lisp_object_t *make_op_pop(void) {
  return mkopcode(POP, 0);
}

lt *make_op_popenv(void) {
  return mkopcode(POPENV, 0);
}

lisp_object_t *make_op_prim(lisp_object_t *nargs) {
  return mkopcode(PRIM, 1, nargs);
}

lisp_object_t *make_op_return() {
  return mkopcode(RETURN, 0);
}

lt *make_op_restargs(lt *count) {
  return mkopcode(RESTARGS, 1, count);
}

lt *make_op_catch(void) {
  return mkopcode(CATCH, 0);
}

/* Opcode */
lt *opcode_ref(enum OPCODE_TYPE opcode) {
  return &lt_codes[opcode];
}

int prim_comp_fn(void *p1, void *p2) {
  return p1 - p2;
}

unsigned int prim_hash_fn(void *prim) {
  return (unsigned int)prim;
}

hash_table_t *make_prim2op_map(void) {
  return make_hash_table(31, prim_hash_fn, prim_comp_fn);
}

lt *search_op4prim(lt *prim) {
  assert(is_lt_primitive(prim));
  return search_ht(prim, prim2op_map);
}

void set_op4prim(lt *prim, enum OPCODE_TYPE opcode) {
  set_ht(prim, opcode_ref(opcode), prim2op_map);
}

int isopcode_fn(lt *prim) {
  assert(is_lt_primitive(prim));
  return search_op4prim(prim) != NULL;
}

lt *make_fn_inst(lt *prim) {
  assert(is_lt_primitive(prim));
  lt *opcode = search_op4prim(prim);
  assert(opcode != NULL);
  return make_pair(mkopcode(opcode_name(opcode), 0), the_empty_list);
}

/* Package */
lt *search_package(char *name, lt *packages) {
  while (is_lt_pair(packages)) {
    lt *pkg = pair_head(packages);
    if (strcmp(string_value(package_name(pkg)), name) == 0)
      return pkg;
    packages = pair_tail(packages);
  }
  return NULL;
}

lt *ensure_package(char *name) {
  lt *result = search_package(name, pkgs);
  if (result)
    return result;
  lt *pkg = make_package(make_string(name), make_symbol_table());
  pkgs = make_pair(pkg, pkgs);
  symbol_value(find_or_create_symbol("*package*", pkg)) = pkg;
  return pkg;
}

void use_package_in(lt *used, lt *pkg) {
  package_used_packages(pkg) =
      make_pair(used, package_used_packages(pkg));
}

/* Symbol */
// The following algorithm comes from http://bbs.csdn.net/topics/350030230
unsigned int symbol_hash_fn(void *symbol) {
  char *name = (char *)symbol;
  int seed = 131;
  unsigned int hash = 0;
  while (*name != '\0') {
    hash = hash * seed + *name;
    name++;
  }
  return hash & 0x7FFFFFFF;
}

int symbol_comp_fn(void *s1, void *s2) {
  char *n1 = (char *)s1;
  char *n2 = (char *)s2;
  return strcmp(n1, n2);
}

hash_table_t *make_symbol_table(void) {
  return make_hash_table(31, symbol_hash_fn, symbol_comp_fn);
}

// Search the symbol with `name' in `symbol_table'
lt *search_symbol_table(char *name, hash_table_t *symbol_table) {
  return search_ht((void *)name, symbol_table);
}

lt *find_symbol(char *name, lt *package) {
  lt *sym = search_symbol_table(name, package_symbol_table(package));
  if (sym)
    return sym;
  lt *useds = package_used_packages(package);
  while (is_lt_pair(useds)) {
    lt *pkg = pair_head(useds);
    sym = search_symbol_table(name, package_symbol_table(pkg));
    if (sym)
      return sym;
    useds = pair_tail(useds);
  }
  return NULL;
}

lt *find_or_create_symbol(char *name, lt *package) {
  lt *result = find_symbol(name, package);
  if (result)
    return result;
  lt *sym = make_symbol(name, package);
  set_ht((void *)name, (void *)sym, package_symbol_table(package));
  return sym;
}

/* Type */
lt *type_ref(enum TYPE type) {
  return &lt_types[type];
}

void init_opcode_length(void) {
  int max = 0;
  for (int i = 0; i < sizeof(lt_codes) / sizeof(*lt_codes); i++) {
    lt *opcode = &lt_codes[i];
    if (strlen(opcode_op(opcode)) > max)
      max = strlen(opcode_op(opcode));
  }
  opcode_max_length = max;
}

void init_packages(void) {
  pkgs = make_empty_list();
  pkg_lisp = ensure_package("Lisp");
//  (defpackage :233-user
//    (:use :233))
  pkg_user = ensure_package("User");
  use_package_in(pkg_lisp, pkg_user);
// Set the current package
  package = pkg_lisp;
}

void init_global_variable(void) {
  /* Initialize global variables */
  debug = FALSE;
  is_check_exception = TRUE;
  is_check_type = TRUE;

  the_argv = make_vector(0);
  the_false = make_false();
  the_true = make_true();
  the_empty_list = make_empty_list();
  the_eof = make_eof();
  gensym_counter = make_fixnum(0);
  null_env = make_environment(the_empty_list, NULL);
  environment_next(null_env) = null_env;
  standard_error = make_output_port(stderr);
  standard_in = make_input_port(stdin);
  standard_out = make_output_port(stdout);
  symbol_list = the_empty_list;
  the_undef = make_undef();

  prim2op_map = make_prim2op_map();
  init_opcode_length();
//  Packages initialization
  init_packages();

// Global variables initialization
  symbol_value(S("*ARGV*")) = the_argv;
  symbol_value(S("*gensym-counter*")) = gensym_counter;
  symbol_value(S("*standard-error*")) = standard_error;
  symbol_value(S("*standard-output*")) = standard_out;
  symbol_value(S("*standard-input*")) = standard_in;

  /* Symbol initialization */
  the_begin_symbol = LISP("begin");
  the_catch_symbol = LISP("catch");
  the_dot_symbol = LISP(".");
  the_goto_symbol = LISP("goto");
  the_if_symbol = LISP("if");
  the_lambda_symbol = LISP("lambda");
  the_quasiquote_symbol = LISP("quasiquote");
  the_quote_symbol = LISP("quote");
  the_set_symbol = LISP("set");
  the_splicing_symbol = LISP("unquote-splicing");
  the_tagbody_symbol = LISP("tagbody");
  the_unquote_symbol = LISP("unquote");
}
