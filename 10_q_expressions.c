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

#define LASSERT(a, cond, err) \
	if(!(cond)) { lval_del(a); return lval_err(err); }

#define LASSERT_NUM_ARGS(a, num, func) \
	if(a->count != num) { lval_del(a); return lval_err("Function '" func "' passed incorrect number of arguments"); }

#define LASSERT_TYPE(a, t, func) \
	if (a->cell[0]->type != t) { lval_del(a); return lval_err("Function '" func "' passed incorrect type"); }

#define LASSERT_NOT_EMPTY_LIST(a, func) \
	if (a->cell[0]->count == 0) { lval_del(a); return lval_err("Function '" func "' passed {}"); }

/* Create enumeration of possible lval types */
enum lval_type {
	LVAL_ERR,
	LVAL_NUM,
	LVAL_SYM,
	LVAL_SEXPR,
	LVAL_QEXPR
};

/* Declare new lval struct */
typedef struct lval {
	enum lval_type type;
	long num;
	char* err;
	char* sym;
	int count;
	struct lval** cell;
} lval;

/* Construct a poninter to a new Number lval */
lval* lval_num(long x) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

/* Construct a pointer to a new Error lval */
lval* lval_err(char* m) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;
	v->err = malloc(strlen(m) + 1);
	strcpy(v->err, m);
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

void lval_del(lval* v) {
	switch(v->type) {
		/* Do nothing special for number type */
		case LVAL_NUM:
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
		: lval_err("invalid number");
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

/* Forward declaration for circular dependency */
void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close) {
	putchar(open);
	for (int i = 0; i < v->count; i++) {
		/* Print value contained within */
		lval_print(v->cell[i]);
		/* Don't print trailing space if last element */
		if (i != (v->count - 1)) {
			putchar(' ');
		}
	}
	putchar(close);
}

/* Print an lval */
void lval_print(lval* v) {
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
		case LVAL_SEXPR:
			lval_expr_print(v, '(', ')');
			break;
		case LVAL_QEXPR:
			lval_expr_print(v, '{', '}');
			break;
	}
};

/* Print an lval followed by a newline */
void lval_println(lval* v) {
	lval_print(v);
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

lval* builtin_head(lval* a) {
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

lval* builtin_tail(lval* a) {
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

lval* builtin_list(lval* a) {
	a->type = LVAL_QEXPR;
	return a;
}

/* Forward declaration */
lval *lval_eval(lval *v);

lval* builtin_eval(lval* a) {
	LASSERT_NUM_ARGS(a, 1, "eval");
	LASSERT_TYPE(a, LVAL_QEXPR, "eval");

	lval* x = lval_take(a, 0);
	x->type = LVAL_SEXPR;
	return lval_eval(x);
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

lval* builtin_join(lval* a) {
	for (int i = 0; i < a->count; i++) {
		LASSERT_TYPE(a, LVAL_QEXPR, "join");
	}

	lval* x = lval_take(a, 0);

	while (a->count) {
		x = lval_join(x, lval_pop(a, 0));
	}

	return x;
}

lval* builtin_cons(lval* a) {
	LASSERT_NUM_ARGS(a, 2, "cons");
	lval* v = lval_pop(a, 0);
	lval* q = lval_take(a, 0);
	LASSERT(q, q->type == LVAL_QEXPR, "Function 'cons' passed incorrect type");

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

lval* builtin_len(lval* a) {
	LASSERT_NUM_ARGS(a, 1, "len");
	LASSERT_TYPE(a, LVAL_QEXPR, "len");
	lval* q = lval_take(a, 0);

	return lval_num(q->count);
}

lval* builtin_init(lval* a) {
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

lval* builtin_op(lval* a, char* op) {
	/* Ensure all arguments are numbers */
	for (int i = 0; i < a->count; i++) {
		if (a->cell[i]->type != LVAL_NUM) {
			lval_del(a);
			return lval_err("Cannot operate on non-number");
		}
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
				return lval_err("Division by zero");
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

lval* builtin(lval* a, char* func) {
	if (strstr("+-/*%^", func)) { return builtin_op(a, func); }
	if (strcmp("list", func) == 0) { return builtin_list(a); }
	if (strcmp("head", func) == 0) { return builtin_head(a); }
	if (strcmp("tail", func) == 0) { return builtin_tail(a); }
	if (strcmp("join", func) == 0) { return builtin_join(a); }
	if (strcmp("eval", func) == 0) { return builtin_eval(a); }
	if (strcmp("cons", func) == 0) { return builtin_cons(a); }
	if (strcmp("len", func) == 0) { return builtin_len(a); }
	if (strcmp("init", func) == 0) { return builtin_init(a); }

	lval_del(a);
	return lval_err("Unknown function");
}

lval* lval_eval_sexpr(lval* v) {
	/* Evaluate children */
	for (int i = 0; i < v->count; i++) {
		v->cell[i] = lval_eval(v->cell[i]);
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
	/* Single expression */
	if (v->count == 1) {
		return lval_take(v, 0);
	}

	/* Ensure first element is a symbol */
	lval* f = lval_pop(v, 0);
	if (f->type != LVAL_SYM) {
		lval_del(f);
		lval_del(v);
		return lval_err("S-expression does not start with symbol");
	}

	/* Call builtin with operator */
	lval* result = builtin(v, f->sym);
	lval_del(f);
	return result;
}

lval* lval_eval(lval* v) {
	/* Evaluate S-expressions, everything else remains the same */
	return v->type == LVAL_SEXPR
		? lval_eval_sexpr(v)
		: v;
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
		"																		\
			number	: /-?[0-9]+/ ;												\
			symbol	: '+' | '-' | '*' | '/' | '%' | '^' 						\
					| \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\"		\
					| \"cons\" | \"len\" | \"init\" ;							\
			sexpr	: '(' <expr>* ')' ;											\
			qexpr	: '{' <expr>* '}' ;											\
			expr	: <number> | <symbol> | <sexpr> | <qexpr> ;					\
			lispy	: /^/ <expr>* /$/ ;											\
		",
		Number, Symbol, Sexpr, Qexpr, Expr, Lispy
	);

	puts("Lispy version 0.0.0.0.6");
	puts("Press Ctrl+c to exit\n");

	while (1) {
		/* Now in either case readline will be correctly defined */
		char* input = readline("lispy> ");
		add_history(input);

		/* Attempt to parse the user input */
		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Lispy, &r)) {
			lval* x = lval_eval(lval_read(r.output));
			lval_println(x);
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
