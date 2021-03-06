#include "ops.h"

const char *str_expr_assign()
{
	return "assign";
}

int fold_const_expr_assign(expr *e)
{
	(void)e;
	return 1; /* could check if the assignment subtree is const */
}

static int is_lvalue(expr *e)
{
	/*
	 * valid lvaluess:
	 *
	 *   x              = 5;
	 *   *(expr)        = 5;
	 *   struct.member  = 5;
	 *   struct->member = 5;
	 * and so on
	 *
	 * also can't be const, checked in fold_assign (since we allow const inits)
	 */

	if(expr_kind(e, identifier))
		return !e->tree_type->func_code;

	if(expr_kind(e, op))
		switch(e->op){
			case op_deref:
			case op_struct_ptr:
			case op_struct_dot:
				return 1;
			default:
				break;
		}

	return 0;
}

void fold_expr_assign(expr *e, symtable *stab)
{
	fold_inc_writes_if_sym(e->lhs, stab);

	fold_expr(e->lhs, stab);
	fold_expr(e->rhs, stab);

	/* wait until we get the tree types, etc */
	if(!is_lvalue(e->lhs))
		die_at(&e->lhs->where, "not an lvalue (%s%s%s)",
				e->lhs->f_str(),
				expr_kind(e->lhs, op) ? " - " : "",
				expr_kind(e->lhs, op) ? op_to_str(e->lhs->op) : ""
			);

	if(decl_is_const(e->lhs->tree_type)){
		/* allow const init: */
		if(e->lhs->sym->decl->init != e->rhs)
			die_at(&e->where, "can't modify const expression");
	}


	if(e->lhs->sym)
		/* read the tree_type from what we're assigning to, not the expr */
		e->tree_type = decl_copy(e->lhs->sym->decl);
	else
		e->tree_type = decl_copy(e->lhs->tree_type);

	fold_typecheck(e->lhs, e->rhs, stab, &e->where);

	/* type check */
	fold_decl_equal(e->lhs->tree_type, e->rhs->tree_type,
		&e->where, WARN_ASSIGN_MISMATCH,
				"assignment type mismatch%s%s%s",
				e->lhs->spel ? " (" : "",
				e->lhs->spel ? e->lhs->spel : "",
				e->lhs->spel ? ")" : "");
}

void gen_expr_assign(expr *e, symtable *stab)
{
	if(e->assign_is_post){
		/* if this is the case, ->rhs->lhs is ->lhs, and ->rhs is an addition/subtraction of 1 * something */
		gen_expr(e->lhs, stab);
		asm_temp(1, "; save previous for post assignment");
	}

	gen_expr(e->rhs, stab);
#ifdef USE_MOVE_RAX_RSP
	asm_temp(1, "mov rax, [rsp]");
#endif

	UCC_ASSERT(e->lhs->f_store, "invalid store expression %s (no f_store())", e->lhs->f_str());

	/* store back to the sym's home */
	e->lhs->f_store(e->lhs, stab);

	if(e->assign_is_post){
		asm_temp(1, "pop rax ; the value from ++/--");
		asm_temp(1, "mov rax, [rsp] ; the value we saved");
	}
}

void gen_expr_str_assign(expr *e, symtable *stab)
{
	(void)stab;
	idt_printf("%sassignment, expr:\n", e->assign_is_post ? "post-inc/dec " : "");
	idt_printf("assign to:\n");
	gen_str_indent++;
	print_expr(e->lhs);
	gen_str_indent--;
	idt_printf("assign from:\n");
	gen_str_indent++;
	print_expr(e->rhs);
	gen_str_indent--;
}

expr *expr_new_assign()
{
	expr *e = expr_new_wrapper(assign);
	e->f_const_fold = fold_const_expr_assign;
	e->freestanding = 1;
	return e;
}
