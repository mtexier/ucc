#include <stdlib.h>
#include "ops.h"

const char *str_expr_cast()
{
	return "cast";
}

int fold_const_expr_cast(expr *e)
{
	return const_fold(e->expr);
}

void fold_expr_cast(expr *e, symtable *stab)
{
	fold_expr(e->expr, stab);

	if(expr_kind(e->expr, cast)){
		/* get rid of e->expr, replace with e->expr->rhs */
		expr *del = e->expr;

		e->expr = e->expr->expr;

		/*decl_free(del->tree_type); XXX: memleak */
		expr_free(del);

		fold_expr_cast(e, stab);
	}
}

void gen_expr_cast_1(expr *e, FILE *f)
{
	asm_declare_single_part(f, e->expr);
}

void gen_expr_cast(expr *e, symtable *stab)
{
	/* ignore the lhs, it's just a type spec */
	/* FIXME: size changing? */
	gen_expr(e->expr, stab);
}

void gen_expr_str_cast(expr *e, symtable *stab)
{
	(void)stab;
	idt_printf("cast expr:\n");
	gen_str_indent++;
	print_expr(e->expr);
	gen_str_indent--;
}

expr *expr_new_cast(decl *to)
{
	expr *e = expr_new_wrapper(cast);
	e->tree_type = to;

	e->f_const_fold = fold_const_expr_cast;
	e->f_gen_1      = gen_expr_cast_1;
	return e;
}
