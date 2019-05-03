#include <stdlib.h>
#include "lib/mpc.h"

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32

static char buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
	fputs(prompt, stdout);
	fgets(buffer, 2048, stdin);
	char* cpy = malloc(strlen(buffer)+1);
	strcpy(cpy, buffer);
	cpy[strlen(cpy)-1] = '\0';
	return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* Otherwise include the editline headers */
#else
#include <editline/readline.h>
#endif

#define LASSERT(args, cond, fmt, ...) \
	if(!(cond)) { \
		lval* err = lval_err(fmt, ##__VA_ARGS__); \
		lval_del(args); \
		return err; \
	}

#define LASSERT_NUM_ARGS(a, num, func) \
	if(a->count != num) { \
		lval* err = lval_err("Function '" func "' passed incorrect number of arguments. Got %i, expected %i.", a->count, num); \
		lval_del(a); \
		return err; \
	}

#define LASSERT_TYPE(a, t, func) \
	enum lval_type lt = a->cell[0]->type; \
	if (lt != t) { \
		lval* err = lval_err("Function '" func "' passed incorrect type. Expected %s, got %s.", ltype_name(t), ltype_name(lt)); \
		lval_del(a); \
		return err; \
	}

#define LASSERT_NOT_EMPTY_LIST(a, func) \
	if (a->cell[0]->count == 0) { lval_del(a); return lval_err("Function '" func "' passed {}."); }

/* Forward Declarations */
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
typedef lval*(*lbuiltin) (lenv*, lval*);
lval* lval_eval(lenv* e, lval *v);
lval* lenv_get(lenv* e, lval* k);
char* find_builtin(lenv* e, lbuiltin b);

/* Create enumeration of possible lval types */
enum lval_type {
	LVAL_ERR,
	LVAL_NUM,
	LVAL_SYM,
	LVAL_FUN,
	LVAL_SEXPR,
	LVAL_QEXPR,
	LVAL_EXIT
};


struct lval {
	enum lval_type type;

	long num;
	char* err;
	char* sym;
	lbuiltin fun;

	int count;
	struct lval** cell;
};

char* ltype_name(enum lval_type t) {
	switch(t) {
		case LVAL_ERR: return "Error";
		case LVAL_NUM: return "Number";
		case LVAL_SYM: return "Symbol";
		case LVAL_FUN: return "Function";
		case LVAL_SEXPR: return "S-Expression";
		case LVAL_QEXPR: return "Q-Expression";
		case LVAL_EXIT: return "Exit";
		default: return "Unknown";
	}
}

/* Construct a pointer to a new Number lval */
lval* lval_num(long x) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

/* Construct a pointer to a new Error lval */
lval* lval_err(char* fmt, ...) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;

	/* Create a va list and initialize it */
	va_list va;
	va_start(va, fmt);

	/* Allocate 512 bytes of space */
	v->err = malloc(512);

	/* printf the error string with a maximum of 511 characters */
	vsnprintf(v->err, 511, fmt, va);

	/* Reallocate to number of bytes actually used */
	v->err = realloc(v->err, strlen(v->err) + 1);

	/* Cleanup our va list */
	va_end(va);
	return v;
};

/* Construct a pointer to a new Symbol lval */
lval* lval_sym(char* s) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SYM;
	v->sym = malloc(strlen(s) + 1);
	strcpy(v->sym, s);
	return v;
}

/* Construct a pointer to a new Function lval */
lval* lval_fun(lbuiltin func) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_FUN;
	v->fun = func;
	return v;
}

/* Construct a pointer to a new empty Sexpr lval */
lval* lval_sexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

/* Construct a pointer to a new empty Qexpr lval */
lval* lval_qexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_QEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

/* Construct a pointer to a new Exit lval */
lval* lval_exit(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_EXIT;
	v->count = 0;
	v->cell = NULL;
	return v;
}

void lval_del(lval* v) {
	switch(v->type) {
		/* Do nothing special for number/function/exit type */
		case LVAL_NUM:
		case LVAL_FUN:
		case LVAL_EXIT:
			break;

		/* For Err or Sym free the string data) */
		case LVAL_ERR:
			free(v->err);
			break;
		case LVAL_SYM:
			free(v->sym);
			break;

		/* If Sexpr or Qexpr then delete all elements inside */
		case LVAL_SEXPR:
		case LVAL_QEXPR:
			for (int i = 0; i < v->count; i++) {
				lval_del(v->cell[i]);
			}
			/* Also free the memory allocated to contain the pointers */
			free(v->cell);
			break;
	}

	/* Free the memory allocated for the lval struct itself */
	free(v);
}

lval* lval_add(lval* v, lval* x) {
	v->count++;
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	v->cell[v->count - 1] = x;
	return v;
}

lval* lval_read_num(mpc_ast_t* t) {
	errno = 0;
	long x = strtol(t->contents, NULL, 10);
	return errno != ERANGE
		? lval_num(x)
		: lval_err("Invalid number.");
}

lval* lval_read(mpc_ast_t* t) {
	/* If Symbol or Number return conversion to that type */
	if (strstr(t->tag, "number")) {
		return lval_read_num(t);
	}
	if (strstr(t->tag, "symbol")) {
		return lval_sym(t->contents);
	}

	/* If root (>) or sexpr then create empty list */
	lval* x = NULL;
	if (
		strcmp(t->tag, ">") == 0
		|| strstr(t->tag, "sexpr")
	) {
		x = lval_sexpr();
	}

	if (strstr(t->tag, "qexpr")) {
		x = lval_qexpr();
	}

	/* Fill in this list with any valid expression contained within */
	for (int i = 0; i < t->children_num; i++) {
		if (
			strcmp(t->children[i]->contents, "(") == 0
			|| strcmp(t->children[i]->contents, ")") == 0
			|| strcmp(t->children[i]->contents, "{") == 0
			|| strcmp(t->children[i]->contents, "}") == 0
			|| strcmp(t->children[i]->tag, "regex") == 0
		) {
			continue;
		}
		x = lval_add(x, lval_read(t->children[i]));
	}

	return x;
}

lval* lval_copy(lval* v) {
	lval* x = malloc(sizeof(lval));
	x->type = v->type;

	switch(x->type) {
		/* Nothing to do for exit */
		case LVAL_EXIT:
			break;
		/* Copy functions and numbers directly */
		case LVAL_NUM:
			x->num = v->num;
			break;
		case LVAL_FUN:
			x->fun = v->fun;
			break;

		/* Copy strings using malloc and strcpy */
		case LVAL_ERR:
			x->err = malloc(strlen(v->err) + 1);
			strcpy(x->err, v->err);
			break;
		case LVAL_SYM:
			x->sym = malloc(strlen(v->sym) + 1);
			strcpy(x->sym, v->sym);
			break;

		/* Copy lists by copying each sub-expression */
		case LVAL_SEXPR:
		case LVAL_QEXPR:
			x->count = v->count;
			x->cell = malloc(sizeof(lval*) * x->count);
			for (int i = 0; i < x->count; ++i) {
				x->cell[i] = lval_copy(v->cell[i]);
			}
			break;
	}

	return x;
}

/* Forward declaration for circular dependency */
void lval_print(lenv* e, lval* v);

void lval_expr_print(lenv*e, lval* v, char open, char close) {
	putchar(open);
	for (int i = 0; i < v->count; i++) {
		/* Print value contained within */
		lval_print(e, v->cell[i]);
		/* Don't print trailing space if last element */
		if (i != (v->count - 1)) {
			putchar(' ');
		}
	}
	putchar(close);
}

/* Print an lval */
void lval_print(lenv* e, lval* v) {
	switch (v->type) {
		case LVAL_NUM:
			printf("%li", v->num);
			break;
		case LVAL_ERR:
			printf("Error: %s", v->err);
			break;
		case LVAL_SYM:
			printf("%s", v->sym);
			break;
		case LVAL_FUN: {
			char* func = find_builtin(e, v->fun);
			printf("<function: %s>", func);
			break;
		}
		case LVAL_SEXPR:
			lval_expr_print(e, v, '(', ')');
			break;
		case LVAL_QEXPR:
			lval_expr_print(e, v, '{', '}');
			break;
		case LVAL_EXIT:
			printf("<exit>");
			break;
		default:
			printf("uh oh");
	}
};

/* Print an lval followed by a newline */
void lval_println(lenv* e, lval* v) {
	lval_print(e, v);
	putchar('\n');
};

lval* lval_pop(lval* v, int i) {
	/* Find the item at i */
	lval* x = v->cell[i];

	/* Shift the memory after the item at i over the top */
	memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval*) * (v->count - i - 1));

	/* Decrease the count of items in the list */
	v->count--;

	/* Reallocate the memory used */
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	return x;
}

lval* lval_take(lval* v, int i) {
	lval* x = lval_pop(v, i);
	lval_del(v);
	return x;
}

lval* builtin_head(lenv* e, lval* a) {
	/* Check error conditions */
	LASSERT_NUM_ARGS(a, 1, "head");
	LASSERT_TYPE(a, LVAL_QEXPR, "head");
	LASSERT_NOT_EMPTY_LIST(a, "head");

	/* Otherwise take first argument */
	lval* v = lval_take(a, 0);

	/* Delete all elements that are not head and return */
	while (v->count > 1) {
		lval_del(lval_pop(v, 1));
	}

	return v;
}

lval* builtin_tail(lenv* e, lval* a) {
	/* Check error conditions */
	LASSERT_NUM_ARGS(a, 1, "tail");
	LASSERT_TYPE(a, LVAL_QEXPR, "tail");
	LASSERT_NOT_EMPTY_LIST(a, "tail");

	/* Otherwise take first argument */
	lval* v = lval_take(a, 0);

	/* Delete first element and return */
	lval_del(lval_pop(v, 0));
	return v;
}

lval* builtin_list(lenv* e, lval* a) {
	a->type = LVAL_QEXPR;
	return a;
}

lval* builtin_eval(lenv* e, lval* a) {
	LASSERT_NUM_ARGS(a, 1, "eval");
	LASSERT_TYPE(a, LVAL_QEXPR, "eval");

	lval* x = lval_take(a, 0);
	x->type = LVAL_SEXPR;
	return lval_eval(e, x);
}

lval* lval_join(lval* x, lval* y) {
	/* For each cell in y add it to x */
	while (y->count) {
		x = lval_add(x, lval_pop(y, 0));
	}

	/* Delete the empty y and return x */
	lval_del(y);
	return x;
}

lval* builtin_join(lenv* e, lval* a) {
	for (int i = 0; i < a->count; i++) {
		LASSERT_TYPE(a, LVAL_QEXPR, "join");
	}

	lval* x = lval_take(a, 0);

	while (a->count) {
		x = lval_join(x, lval_pop(a, 0));
	}

	return x;
}

lval* builtin_cons(lenv* e, lval* a) {
	LASSERT_NUM_ARGS(a, 2, "cons");
	lval* v = lval_pop(a, 0);
	lval* q = lval_take(a, 0);
	LASSERT_TYPE(q, LVAL_QEXPR, "cons");

	/* Reallocate the memory used */
	q->cell = realloc(q->cell, sizeof(lval*) * (q->count + 1));
	/* Shift the memory up one */
	if (q->count) {
		memmove(&q->cell[1], &q->cell[0], sizeof(lval*) * q->count);
	}

	/* Increase the count of items in the list */
	q->count++;

	/* Prepend value */
	q->cell[0] = v;
	return q;
}

lval* builtin_len(lenv* e, lval* a) {
	LASSERT_NUM_ARGS(a, 1, "len");
	LASSERT_TYPE(a, LVAL_QEXPR, "len");
	lval* q = lval_take(a, 0);

	return lval_num(q->count);
}

lval* builtin_init(lenv* e, lval* a) {
	LASSERT_NUM_ARGS(a, 1, "init");
	LASSERT_TYPE(a, LVAL_QEXPR, "init");
	LASSERT_NOT_EMPTY_LIST(a, "init");
	lval* q = lval_take(a, 0);

	/* Decrease the count of items in the list */
	q->count--;
	/* Reallocate the memory used */
	q->cell = realloc(q->cell, sizeof(lval*) * (q->count));

	return q;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
	/* Ensure all arguments are numbers */
	for (int i = 0; i < a->count; i++) {
		LASSERT(a->cell[i], a->cell[i]->type == LVAL_NUM, "Cannot perform operation. Expected Number argument at position %i, got %s.", i, ltype_name(a->cell[i]->type));
	}

	/* Pop the first element */
	lval* x = lval_pop(a, 0);

	/* If no arguments and sub then perform unary negation */
	if ((strcmp(op, "-") == 0) && a->count == 0) {
		x->num = -x->num;
	}

	/* While there are still elements remaining */
	while (a->count > 0) {
		/* Pop the next element */
		lval* y = lval_pop(a, 0);

		if (strcmp(op, "+") == 0) { x->num += y->num; }
		if (strcmp(op, "-") == 0) { x->num -= y->num; }
		if (strcmp(op, "*") == 0) { x->num *= y->num; }
		if (strcmp(op, "/") == 0) {
			if (y->num == 0) {
				lval_del(x);
				lval_del(y);
				return lval_err("Division by zero.");
			}
			x->num /= y->num;
		}
		if (strcmp(op, "%") == 0) { x->num %= y->num; }
		if (strcmp(op, "^") == 0) { x->num ^= y->num; }

		lval_del(y);
	}

	lval_del(a);
	return x;
}

lval* builtin_add(lenv* e, lval* a) {
	return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
	return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
	return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
	return builtin_op(e, a, "/");
}

lval* builtin_mod(lenv* e, lval* a) {
	return builtin_op(e, a, "%");
}

lval* builtin_xor(lenv* e, lval* a) {
	return builtin_op(e, a, "^");
}

lval* lval_eval_sexpr(lenv* e, lval* v) {
	/* Evaluate children */
	for (int i = 0; i < v->count; i++) {
		v->cell[i] = lval_eval(e, v->cell[i]);
	}

	/* Error checking */
	for (int i = 0; i < v->count; i++) {
		if (v->cell[i]->type == LVAL_ERR) {
			return lval_take(v, i);
		}
	}

	/* Empty expression */
	if (v->count == 0) {
		return v;
	}
	/* Single expression, except exit and deflist*/
	int is_exit = (v->cell[0]->fun == lenv_get(e, lval_sym("exit"))->fun);
	int is_deflist = (v->cell[0]->fun == lenv_get(e, lval_sym("deflist"))->fun);
	if (v->count == 1 && !is_exit & !is_deflist) {
		return lval_take(v, 0);
	}

	/* Ensure first element is a function after evaluation */
	lval* f = lval_pop(v, 0);
	if (f->type != LVAL_FUN) {
		lval_del(f);
		lval_del(v);
		return lval_err("First element is not a function. Got %s.", ltype_name(f->type));
	}

	/* If so call function to get result */
	lval* result = f->fun(e, v);
	lval_del(f);
	return result;
}

struct lenv {
	int count;
	char** syms;
	lval** vals;

	int builtins_count;
	char** builtins;
};

lenv* lenv_new(void) {
	lenv* e = malloc(sizeof(lenv));
	e->count = 0;
	e->syms = NULL;
	e->vals = NULL;
	e->builtins_count = 0;
	e->builtins = NULL;
	return e;
}

void lenv_del(lenv* e) {
	for (int i = 0; i < e->count; ++i) {
		free(e->syms[i]);
		lval_del(e->vals[i]);
	}
	free(e->syms);
	free(e->vals);
	free(e);
}

lval* lenv_get(lenv* e, lval* k) {
	for (int i = 0; i < e->count; ++i) {
		/* Check if the stored string matches the symbol string */
		/* If it does, return a copy of the value */
		if (strcmp(e->syms[i], k->sym) == 0) {
			return lval_copy(e->vals[i]);
		}
	}
	return lval_err("Unbound symbol '%s'", k->sym);
}

void lenv_put(lenv* e, lval* k, lval* v) {
	/* Check if variable already exists */
	for (int i = 0; i < e->count; ++i) {
		/* If variable is found, delete it and replace */
		if (strcmp(e->syms[i], k->sym) == 0) {
			lval_del(e->vals[i]);
			e->vals[i] = lval_copy(v);
			return;
		}
	}

	/* If no existing entry is found, allocate space for new entry */
	e->count++;
	e->vals = realloc(e->vals, sizeof(lval*) * e->count);
	e->syms = realloc(e->syms, sizeof(char*) * e->count);

	/* Copy contents of lval and symbol string into new location */
	e->vals[e->count - 1] = lval_copy(v);
	e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
	strcpy(e->syms[e->count - 1], k->sym);
}

void lenv_put_builtin(lenv* e, lval* k) {
	e->builtins_count++;
	e->builtins = realloc(e->builtins, sizeof(char*) * e->builtins_count);
	e->builtins[e->builtins_count - 1] = malloc(strlen(k->sym) + 1);
	strcpy(e->builtins[e->builtins_count - 1], k->sym);
}

lval* lval_eval(lenv* e, lval* v) {
	if (v->type == LVAL_SYM) {
		lval* x = lenv_get(e, v);
		lval_del(v);
		return x;
	}
	if (v->type == LVAL_SEXPR) {
		return lval_eval_sexpr(e, v);
	}
	return v;
}

lval* builtin_def(lenv* e, lval* a) {
	LASSERT_TYPE(a, LVAL_QEXPR, "def");

	/* First argument is symbol list */
	lval* syms = a->cell[0];

	/* Ensure all elements of first list are symbols */
	for (int i = 0; i < syms->count; ++i) {
		LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbol. Got %s.", ltype_name(syms->cell[i]->type));
	}

	/* Ensure no elements are builtins */
	for (int i = 0; i < syms->count; ++i) {
		for (int j = 0; j < e->builtins_count; ++j) {
			LASSERT(a, strcmp(syms->cell[i]->sym, e->builtins[j]) != 0, "Function 'def' cannot redefine builtin '%s'", e->builtins[j]);
		}
	}

	/* Check correct number of symbols and values */
	LASSERT(a, syms->count == a->count - 1, "Function 'def' cannot define incorrect number of values to symbols. Got %i symbols but %i values.", syms->count, a->count - 1);

	/* Assign copies of values to symbols */
	for (int i = 0; i < syms->count; ++i) {
		lenv_put(e, syms->cell[i], a->cell[i+1]);
	}

	lval_del(a);
	return lval_sexpr();
}

lval* builtin_exit(lenv* e, lval* a) {
	return lval_exit();
}

lval* builtin_deflist(lenv* e, lval* a) {
	for (int i = 0; i < e->count; ++i) {
		printf("%s\t", e->syms[i]);
	}
	printf("\n");
	return lval_sexpr();
}

char* find_builtin(lenv* e, lbuiltin b) {
	for (int i = 0; i < e->count; ++i) {
		if (e->vals[i]->fun == b) {
			return e->syms[i];
		}
	}
	return "unknown";
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
	lval* k = lval_sym(name);
	lval* v = lval_fun(func);
	lenv_put(e, k, v);
	lenv_put_builtin(e, k);
	lval_del(k);
	lval_del(v);
}

void lenv_add_builtins(lenv* e) {
	/* List functions */
	lenv_add_builtin(e, "list", builtin_list);
	lenv_add_builtin(e, "head", builtin_head);
	lenv_add_builtin(e, "tail", builtin_tail);
	lenv_add_builtin(e, "eval", builtin_eval);
	lenv_add_builtin(e, "join", builtin_join);
	lenv_add_builtin(e, "cons", builtin_cons);
	lenv_add_builtin(e, "len", builtin_len);
	lenv_add_builtin(e, "init", builtin_init);

	/* Mathematical functions */
	lenv_add_builtin(e, "+", builtin_add);
	lenv_add_builtin(e, "-", builtin_sub);
	lenv_add_builtin(e, "*", builtin_mul);
	lenv_add_builtin(e, "/", builtin_div);
	lenv_add_builtin(e, "%", builtin_mod);
	lenv_add_builtin(e, "^", builtin_xor);

	/* Variable functions */
	lenv_add_builtin(e, "def", builtin_def);
	lenv_add_builtin(e, "deflist", builtin_deflist);

	/* Application functions */
	lenv_add_builtin(e, "exit", builtin_exit);
}

int main(int argc, char** argv) {
	/* Create some parsers */
	mpc_parser_t* Number = mpc_new("number");
	mpc_parser_t* Symbol = mpc_new("symbol");
	mpc_parser_t* Sexpr = mpc_new("sexpr");
	mpc_parser_t* Qexpr = mpc_new("qexpr");
	mpc_parser_t* Expr = mpc_new("expr");
	mpc_parser_t* Lispy = mpc_new("lispy");

	/* Define them with the following language */
	mpca_lang(MPCA_LANG_DEFAULT,
		"																				\
			number	: /-?[0-9]+/ ;														\
			symbol	: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&][a-zA-Z0-9_+\\-*\\/\\\\=<>!&]*/ ;	\
			sexpr	: '(' <expr>* ')' ;													\
			qexpr	: '{' <expr>* '}' ;													\
			expr	: <number> | <symbol> | <sexpr> | <qexpr> ;							\
			lispy	: /^/ <expr>* /$/ ;													\
		",
		Number, Symbol, Sexpr, Qexpr, Expr, Lispy
	);

	puts("Lispy version 0.0.0.0.7");
	puts("Press Ctrl+c to exit\n");

	lenv* e = lenv_new();
	lenv_add_builtins(e);

	int repeat = 1;

	while (repeat) {
		/* Now in either case readline will be correctly defined */
		char* input = readline("lispy> ");
		add_history(input);

		/* Attempt to parse the user input */
		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Lispy, &r)) {
			lval* x = lval_eval(e, lval_read(r.output));
			if (x->type == LVAL_EXIT) {
				repeat = 0;
			} else {
				lval_println(e, x);
			}
			lval_del(x);
			mpc_ast_delete(r.output);
		} else {
			/* Otherwise print the error */
			mpc_err_print(r.error);
			mpc_err_delete(r.error);
		}

		free(input);
	}

	/* Undefine and delete our parsers */
	mpc_cleanup(4, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

	return 0;
}
