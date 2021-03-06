#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "../util/alloc.h"
#include "../util/util.h"
#include "data_structs.h"
#include "macros.h"
#include "sym.h"
#include "../util/platform.h"
#include "struct.h"
#include "enum.h"

void where_new(struct where *w)
{
	extern int current_line, current_chr;
	extern const char *current_fname;

	w->line  = current_line;
	w->chr   = current_chr;
	w->fname = current_fname;
}

decl_ptr *decl_ptr_new()
{
	decl_ptr *dp = umalloc(sizeof *dp);
	where_new(&dp->where);
	return dp;
}

decl *decl_new()
{
	decl *d = umalloc(sizeof *d);
	where_new(&d->where);
	d->type = type_new();
	return d;
}

decl *decl_new_where(where *w)
{
	decl *d = decl_new();
	memcpy(&d->where,       w, sizeof w);
	memcpy(&d->type->where, w, sizeof w);
	return d;
}

array_decl *array_decl_new()
{
	array_decl *ad = umalloc(sizeof *ad);
	return ad;
}

type *type_new()
{
	type *t = umalloc(sizeof *t);
	where_new(&t->where);
	t->spec = spec_none;
	t->primitive = type_unknown;
	return t;
}

type *type_copy(type *t)
{
	type *ret = umalloc(sizeof *ret);
	memcpy(ret, t, sizeof *ret);
	return ret;
}

decl_ptr *decl_ptr_copy(decl_ptr *dp)
{
	decl_ptr *ret = umalloc(sizeof *ret);
	memcpy(ret, dp, sizeof *ret);
	if(dp->child)
		ret->child = decl_ptr_copy(dp->child);
	/* leave func and spel, tis fine */
	return ret;
}

decl *decl_copy(decl *d)
{
	decl *ret = umalloc(sizeof *ret);
	memcpy(ret, d, sizeof *ret);
	ret->type = type_copy(d->type);
	if(d->decl_ptr)
		ret->decl_ptr = decl_ptr_copy(d->decl_ptr);
	/*ret->spel = NULL;*/
	return ret;
}

funcargs *funcargs_new()
{
	funcargs *f = umalloc(sizeof *f);
	where_new(&f->where);
	return f;
}

void funcargs_free(funcargs *args, int free_decls)
{
	if(free_decls){
		int i;
		for(i = 0; args->arglist[i]; i++)
			decl_free(args->arglist[i]);
	}
	free(args);
}

int type_size(const type *t)
{
	switch(t->primitive){
		case type_char:
		case type_void:
			return 1;

		case type_enum:
		case type_int:
			/* FIXME: 4 for int */
			return platform_word_size();

		case type_typedef:
			return decl_size(t->tdef);

		case type_struct:
			return struct_size(t->struc);

		case type_unknown:
			break;
	}

	ICE("type %s in type_size()", type_to_str(t));
	return -1;
}

int decl_size(decl *d)
{
	if(decl_has_array(d)){
		const int siz = type_size(d->type);
		decl_ptr *dp;
		int ret = 0;

		for(dp = d->decl_ptr; dp; dp = dp->child)
			if(dp->array_size){
				/* should've been folded fully */
				long v = dp->array_size->val.iv.val;
				if(!v)
					v = platform_word_size(); /* int x[0] - the 0 is a sentinel */
				ret += v * siz;
			}

		return ret;
	}

	if(d->decl_ptr) /* pointer */
		return platform_word_size();

	if(d->field_width)
		return d->field_width;

	return type_size(d->type);
}

int type_equal(const type *a, const type *b, int strict)
{
	/*
	 * basic const checking, doesn't work with
	 * const char *const x, etc..
	 */
	if(strict && (b->spec & spec_const) && (a->spec & spec_const) == 0)
		return 0; /* we can assign from const to non-const, but not vice versa - FIXME should be elsewhere? */

	return strict ? a->primitive == b->primitive : 1; /* int == char */
}

int decl_ptr_equal(decl_ptr *dpa, decl_ptr *dpb)
{
	/* if we are assigning from const, target must be const */
	if(dpb->is_const ? dpa->is_const : 0)
		return 0;

	if(dpa->child)
		return dpb->child && decl_ptr_equal(dpa->child, dpb->child);

	return !dpb->child;
}

int decl_equal(decl *a, decl *b, enum decl_cmp mode)
{
#define VOID_PTR(d) (                   \
			d->type->primitive == type_void   \
			&&  d->decl_ptr                   \
			&& !d->decl_ptr->child            \
		)

	if((mode & DECL_CMP_ALLOW_VOID_PTR) && (VOID_PTR(a) || VOID_PTR(b)))
		return 1; /* one side is void * */

	if(!type_equal(a->type, b->type, mode & DECL_CMP_STRICT_PRIMITIVE))
		return 0;

	if(a->decl_ptr){
		if(b->decl_ptr)
			return decl_ptr_equal(a->decl_ptr, b->decl_ptr);
	}else if(!b->decl_ptr){
		return 1;
	}

	return 0;
}

void function_empty_args(funcargs *func)
{
	if(func->arglist){
		UCC_ASSERT(!func->arglist[1], "empty_args called when it shouldn't be");

		decl_free(func->arglist[0]);
		free(func->arglist);
		func->arglist = NULL;
	}
	func->args_void = 0;
}

const char *op_to_str(const enum op_type o)
{
	switch(o){
		CASE_STR_PREFIX(op, multiply);
		CASE_STR_PREFIX(op, divide);
		CASE_STR_PREFIX(op, plus);
		CASE_STR_PREFIX(op, minus);
		CASE_STR_PREFIX(op, modulus);
		CASE_STR_PREFIX(op, deref);
		CASE_STR_PREFIX(op, eq);
		CASE_STR_PREFIX(op, ne);
		CASE_STR_PREFIX(op, le);
		CASE_STR_PREFIX(op, lt);
		CASE_STR_PREFIX(op, ge);
		CASE_STR_PREFIX(op, gt);
		CASE_STR_PREFIX(op, or);
		CASE_STR_PREFIX(op, xor);
		CASE_STR_PREFIX(op, and);
		CASE_STR_PREFIX(op, orsc);
		CASE_STR_PREFIX(op, andsc);
		CASE_STR_PREFIX(op, not);
		CASE_STR_PREFIX(op, bnot);
		CASE_STR_PREFIX(op, shiftl);
		CASE_STR_PREFIX(op, shiftr);
		CASE_STR_PREFIX(op, struct_ptr);
		CASE_STR_PREFIX(op, struct_dot);
		CASE_STR_PREFIX(op, unknown);
	}
	return NULL;
}

const char *spec_to_str(const enum type_spec s)
{
	switch(s){
		CASE_STR_PREFIX(spec, const);
		CASE_STR_PREFIX(spec, extern);
		CASE_STR_PREFIX(spec, static);
		CASE_STR_PREFIX(spec, signed);
		CASE_STR_PREFIX(spec, unsigned);
		CASE_STR_PREFIX(spec, auto);
		CASE_STR_PREFIX(spec, typedef);
		case spec_none: return "";
	}
	return NULL;
}

const char *spec_to_str_full(const enum type_spec s)
{
	static char buf[SPEC_STATIC_BUFSIZ];
	char *bufp = buf;
	int i;

	*buf = '\0';

	for(i = 0; i < SPEC_MAX; i++)
		if(s & (1 << i))
			bufp += snprintf(bufp, sizeof buf - (bufp - buf), "%s ", spec_to_str(1 << i));

	return buf;
}

int op_is_cmp(enum op_type o)
{
	switch(o){
		case op_eq:
		case op_ne:
		case op_le:
		case op_lt:
		case op_ge:
		case op_gt:
			return 1;
		default:
			break;
	}
	return 0;
}

int decl_ptr_depth(decl *d)
{
	decl_ptr *dp;
	int i = 0;

	for(dp = d->decl_ptr; dp; dp = dp->child)
		i++;

	return i;
}

decl_ptr **decl_leaf(decl *d)
{
	decl_ptr **dp;
	UCC_ASSERT(d, "null decl param");
	for(dp = &d->decl_ptr; *dp; dp = &(*dp)->child);
	return dp;
}

funcargs *decl_funcargs(decl *d)
{
	/* either ->func on the decl, or on a decl_ptr (in the case of a funcptr) */
	decl_ptr *dp;

	if(d->funcargs)
		return d->funcargs;

	for(dp = d->decl_ptr; dp; dp = dp->child)
		if(dp->fptrargs)
			return dp->fptrargs;
	return NULL;
}

int decl_is_callable(decl *d)
{
	return !!decl_funcargs(d);
}

int decl_has_array(decl *d)
{
	decl_ptr *dp;

	for(dp = d->decl_ptr; dp; dp = dp->child)
		if(dp->array_size)
			return 1;
	return 0;
}

int decl_is_const(decl *d)
{
	decl_ptr *dp = *decl_leaf(d);
	if(dp)
		return dp->is_const;
	return 0;/*d->type->spec & spec_const; TODO */
}

int decl_is_func_ptr(decl *d)
{
	return !!decl_funcargs(d);
}

decl *decl_ptr_depth_inc(decl *d)
{
	*decl_leaf(d) = decl_ptr_new();
	return d;
}

decl *decl_ptr_depth_dec(decl *d)
{
	d->decl_ptr = d->decl_ptr->child;
	return d;
}

decl *decl_func_deref(decl *d)
{
	return d;
}

const char *type_to_str(const type *t)
{
#define BUF_SIZE (sizeof(buf) - (bufp - buf))
	static char buf[TYPE_STATIC_BUFSIZ];
	char *bufp = buf;

	if(t->tdef)
		return type_to_str(t->tdef->type);

	if(t->spec)
		bufp += snprintf(bufp, BUF_SIZE, "%s", spec_to_str_full(t->spec));

	if(t->struc){
		snprintf(bufp, BUF_SIZE, "struct %s", t->struc->spel);
	}else if(t->enu){
		snprintf(bufp, BUF_SIZE, "enum %s", t->enu->spel);
	}else{
		switch(t->primitive){
#define APPEND(t) case type_ ## t: snprintf(bufp, BUF_SIZE, "%s", #t); break
			APPEND(int);
			APPEND(char);
			APPEND(void);
			case type_unknown:
				ICE("unknown type primitive (%s)", where_str(&t->where));
			case type_typedef:
				ICE("typedef without ->tdef");
			case type_enum:
				ICE("enum without ->enu");
			case type_struct:
				snprintf(bufp, BUF_SIZE, "incomplete-struct %s", t->spel);
				/*ICE("struct without ->struc");*/
				break;
#undef APPEND
		}
	}

	return buf;
}

const char *decl_to_str(decl *d)
{
	static char buf[DECL_STATIC_BUFSIZ];
	unsigned int i = 0;
	decl_ptr *dp;

#define BUF_ADD(...) i += snprintf(buf + i, sizeof buf - i, __VA_ARGS__)

	BUF_ADD("%s", type_to_str(d->type));

	for(dp = d->decl_ptr; dp; dp = dp->child)
		BUF_ADD("%s*%s%s%s%s",
				dp->fptrargs   ? "("  : "",
				dp->is_const   ? "K"  : "",
				dp->fptrargs   ? "()" : "",
				dp->array_size ? "[]" : "",
				dp->fptrargs   ? ")"  : "");

	buf[i] = '\0';

	if(d->funcargs)
		BUF_ADD("()");

	return buf;
}
