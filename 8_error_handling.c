#include <stdlib.h>
#include "lib/mpc.h"

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32

static char buffer[2048];

/* Fake readline function */
char *readline(char *prompt)
{
	fputs(prompt, stdout);
	fgets(buffer, 2048, stdin);
	char *cpy = malloc(strlen(buffer) + 1);
	strcpy(cpy, buffer);
	cpy[strlen(cpy) - 1] = '\0';
	return cpy;
}

/* Fake add_history function */
void add_history(char *unused) {}

/* Otherwise include the editline headers */
#else
#include <editline/readline.h>
#endif

/* Create enumeration of possible lval types */
enum lval_type
{
	LVAL_NUM,
	LVAL_ERR
};

/* Create enumeration of possible error types */
enum lval_err_type
{
	LERR_DIV_ZERO,
	LERR_BAD_OP,
	LERR_BAD_NUM
};

/* Declare new lval struct */
typedef struct
{
	enum lval_type type;
	union {
		long num;
		enum lval_err_type err;
	};
} lval;

/* Create a new number type lval */
lval lval_num(long x)
{
	lval v;
	v.type = LVAL_NUM;
	v.num = x;
	return v;
}

lval lval_err(int x)
{
	lval v;
	v.type = LVAL_ERR;
	v.err = x;
	return v;
};

/* Print an lval */
void lval_print(lval v)
{
	switch (v.type)
	{
	case LVAL_NUM:
		printf("%li", v.num);
		break;
	case LVAL_ERR:
		switch (v.err)
		{
		case LERR_DIV_ZERO:
			printf("Error: Division by zero!");
			break;
		case LERR_BAD_OP:
			printf("Error: Invalid operator!");
			break;
		case LERR_BAD_NUM:
			printf("Error: Invalid number!");
			break;
		}
		break;
	}
};

/* Print an lval followed by a newline */
void lval_println(lval v)
{
	lval_print(v);
	putchar('\n');
};

lval eval_unary_op(lval x, char *op)
{
	if (strcmp(op, "-") == 0)
	{
		return lval_num(-x.num);
	}

	return lval_err(LERR_BAD_OP);
}

/* Use operator string to see which operation to perform */
lval eval_op(lval x, char *op, lval y)
{
	/* If either value is an error return it */
	if (x.type == LVAL_ERR)
	{
		return x;
	}
	if (y.type == LVAL_ERR)
	{
		return y;
	}

	/* Otherwise do the calculation */
	if (strcmp(op, "+") == 0)
	{
		return lval_num(x.num + y.num);
	}
	if (strcmp(op, "-") == 0)
	{
		return lval_num(x.num - y.num);
	}
	if (strcmp(op, "*") == 0)
	{
		return lval_num(x.num * y.num);
	}
	if (strcmp(op, "/") == 0)
	{
		return y.num == 0
				   ? lval_err(LERR_DIV_ZERO)
				   : lval_num(x.num / y.num);
	}
	if (strcmp(op, "%") == 0)
	{
		return lval_num(x.num % y.num);
	}
	if (strcmp(op, "^") == 0)
	{
		return lval_num(pow(x.num, y.num));
	}
	if (strcmp(op, "min") == 0)
	{
		return lval_num(x.num < y.num ? x.num : y.num);
	}
	if (strcmp(op, "max") == 0)
	{
		return lval_num(x.num > y.num ? x.num : y.num);
	}

	return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t *t)
{
	/* If tagged as number return it directly */
	if (strstr(t->tag, "number"))
	{
		/* Check if there is aome error in conversion */
		errno = 0;
		long x = strtol(t->contents, NULL, 10);
		return errno != ERANGE
				   ? lval_num(x)
				   : lval_err(LERR_BAD_NUM);
	}

	/* The operator is always second child */
	char *op = t->children[1]->contents;

	/* We store the third child in `x` */
	lval x = eval(t->children[2]);

	/* Iterate the remaining children and combine */
	int i = 3;

	if (strcmp(op, "-") == 0 && !strstr(t->children[i]->tag, "expr"))
	{
		return eval_unary_op(x, op);
	}
	while (strstr(t->children[i]->tag, "expr"))
	{
		x = eval_op(x, op, eval(t->children[i]));
		i++;
	}

	return x;
}

int main(int argc, char **argv)
{
	/* Create some parsers */
	mpc_parser_t *Number = mpc_new("number");
	mpc_parser_t *Operator = mpc_new("operator");
	mpc_parser_t *Expr = mpc_new("expr");
	mpc_parser_t *Lispy = mpc_new("lispy");

	/* Define them with the following language */
	mpca_lang(MPCA_LANG_DEFAULT,
			  "															\
			number		: /-?[0-9]+/;								\
			operator	: '+' | '-' | '*' | '/' | '%' | '^'			\
						| \"min\" | \"max\";						\
			expr		: <number> | '(' <operator> <expr>+ ')';	\
			lispy		: /^/ <operator> <expr>+ /$/;				\
		",
			  Number, Operator, Expr, Lispy);

	puts("Lispy version 0.0.0.0.4");
	puts("Press Ctrl+c to exit\n");

	while (1)
	{
		/* Now in either case readline will be correctly defined */
		char *input = readline("lispy> ");
		add_history(input);

		/* Attempt to parse the user input */
		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Lispy, &r))
		{
			lval result = eval(r.output);
			lval_println(result);
			mpc_ast_delete(r.output);
		}
		else
		{
			/* Otherwise print the error */
			mpc_err_print(r.error);
			mpc_err_delete(r.error);
		}

		free(input);
	}

	/* Undefine and delete our parsers */
	mpc_cleanup(4, Number, Operator, Expr, Lispy);

	return 0;
}
