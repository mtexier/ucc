#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "../util/util.h"
#include "../util/alloc.h"
#include "../util/dynarray.h"
#include "data_structs.h"
#include "typedef.h"

#include "tokenise.h"
#include "tokconv.h"

#include "struct.h"
#include "enum.h"
#include "sym.h"

#include "cc1.h"

#include "parse.h"
#include "parse_type.h"

type *parse_type_struct(void);
type *parse_type_enum(void);
decl_ptr *parse_decl_ptr_array(enum decl_mode mode, char **decl_sp, funcargs **decl_args);

#define INT_TYPE(t) do{ t = type_new(); t->primitive = type_int; }while(0)

void parse_type_preamble(type **tp, char **psp, enum type_primitive primitive)
{
	char *spel;
	type *t;

	spel = NULL;
	t = type_new();
	t->primitive = primitive;

	if(curtok == token_identifier){
		spel = token_current_spel();
		EAT(token_identifier);
	}

	*psp = spel;
	*tp = t;
}

type *parse_type_struct()
{
	type *t;
	char *spel;

	parse_type_preamble(&t, &spel, type_struct);

	if(accept(token_open_block)){
		decl **members = parse_decls(DECL_CAN_DEFAULT | DECL_SPEL_NEED, 1);
		EAT(token_close_block);
		t->struc = struct_add(current_scope, spel, members);
	}else if(!spel){
		die_at(NULL, "expected: struct definition or name");
	}

	t->spel = spel; /* save for lookup */

	return t;
}

type *parse_type_enum()
{
	type *t;
	char *spel;

	parse_type_preamble(&t, &spel, type_enum);

	if(accept(token_open_block)){
		enum_st *en = enum_st_new();

		do{
			expr *e;
			char *sp;

			sp = token_current_spel();
			EAT(token_identifier);

			if(accept(token_assign))
				e = parse_expr_funcallarg(); /* no commas */
			else
				e = NULL;

			enum_vals_add(en, sp, e);

			if(!accept(token_comma))
				break;
		}while(curtok == token_identifier);

		EAT(token_close_block);

		t->enu = enum_add(&current_scope->enums, spel, en);
	}else if(!spel){
		die_at(NULL, "expected: enum definition or name");
	}

	t->spel = spel; /* save for lookup */

	return t;
}

type *parse_type()
{
	enum type_spec spec;
	type *t;
	decl *td;
	int flag;

	spec = 0;
	t = NULL;

	/* read "const", "unsigned", ... and "int"/"long" ... in any order */
	while(td = NULL,
			((flag = curtok_is_type_specifier())
			 || curtok_is_type()
			 || curtok == token_struct
			 || curtok == token_enum
			 || (curtok == token_identifier && (td = typedef_find(current_scope, token_current_spel_peek())))
			 )){

		if(accept(token_struct)){
			return parse_type_struct();
		}else if(accept(token_enum)){
			return parse_type_enum();
		}else if(flag){
			const enum type_spec this = curtok_to_type_specifier();

			UCC_ASSERT(this != spec_none, "spec none where spec expected");

			/* we can't check in fold, since 1 & 1 & 1 is still just 1 */
			if(this & spec)
				die_at(NULL, "duplicate type specifier \"%s\"", spec_to_str(spec));

			spec |= this;
			EAT(curtok);
		}else if(td){
			/* typedef name */

			if(t){
				/* "int x" - we are at x, which is also a typedef somewhere */
				cc1_warn_at(NULL, 0, WARN_IDENT_TYPEDEF, "identifier is a typedef name");
				break;
			}

			t = type_new();
			t->primitive = type_typedef;
			t->tdef = td;

			EAT(token_identifier);
			break;

		}else{
			/* curtok_is_type */
			if(t){
				die_at(NULL, "second type name unexpected");
			}else{
				t = type_new();
				t->primitive = curtok_to_type_primitive();
				UCC_ASSERT(t->primitive != type_unknown, "unknown type where type expected");
				EAT(curtok);
			}
		}
	}

	if(!t && spec)
		/* unsigned x; */
		INT_TYPE(t);

	if(t)
		t->spec = spec;

	return t;
}

funcargs *parse_func_arglist()
{
	/*
	 * either:
	 *
	 * [<type>] <name>( [<type> [<name>]]... )
	 * {
	 * }
	 *
	 * with optional {}
	 *
	 * or
	 *
	 * [<type>] <name>( [<name>...] )
	 *   <type> <name>, ...;
	 *   <type> <name>, ...;
	 * {
	 * }
	 *
	 * non-optional code
	 *
	 * i.e.
	 *
	 * int x(int y);
	 * int x(int y){}
	 *
	 * or
	 *
	 * int x(y)
	 *   int y;
	 * {
	 * }
	 *
	 */
	funcargs *args;
	decl *argdecl;

	args = funcargs_new();

	if(curtok == token_close_paren)
		goto empty_func;

	argdecl = parse_decl_single(DECL_CAN_DEFAULT);

	if(argdecl){
		for(;;){
			dynarray_add((void ***)&args->arglist, argdecl);

			if(curtok == token_close_paren)
				break;

			EAT(token_comma);

			if(accept(token_elipsis)){
				args->variadic = 1;
				break;
			}

			/* continue loop */
			/* actually, we don't need a type here, default to int, i think */
			argdecl = parse_decl_single(DECL_CAN_DEFAULT);
		}

		if(dynarray_count((void *)args->arglist) == 1 &&
				                      args->arglist[0]->type->primitive == type_void &&
				                     !args->arglist[0]->decl_ptr && /* manual checks, since decl_*() assert */
				                     !args->arglist[0]->spel){
			/* x(void); */
			function_empty_args(args);
			args->args_void = 1; /* (void) vs () */
		}

	}else{
		int i, n_spels, n_decls;
		char **spells;
		decl **argar;

		spells = NULL;

		ICE("TODO: old functions (on %s%s%s)",
				token_to_str(curtok),
				curtok == token_identifier ? ": " : "",
				curtok == token_identifier ? token_current_spel_peek() : ""
				);

		do{
			if(curtok != token_identifier)
				EAT(token_identifier); /* error */

			dynarray_add((void ***)&spells, token_current_spel());
			EAT(token_identifier);

			if(accept(token_close_paren))
				break;
			EAT(token_comma);

		}while(1);

		/* parse decls, then check they correspond */
		argar = PARSE_DECLS();

		n_decls = dynarray_count((void *)argar);
		n_spels = dynarray_count((void *)spells);

		if(n_decls > n_spels)
			die_at(argar ? &argar[0]->where : &args->where, "old-style function decl: mismatching argument counts");

		for(i = 0; i < n_spels; i++){
			int j, found;

			found = 0;
			for(j = 0; j < n_decls; j++)
				if(!strcmp(spells[i], argar[j]->spel)){
					if(argar[j]->init)
						die_at(&argar[j]->where, "parameter \"%s\" is initialised", argar[j]->spel);

					found = 1;
					break;
				}

			if(!found){
				/*
					* void f(x){ ... }
					* - x is implicitly int
					*/
				decl *d = decl_new();
				d->type->primitive = type_int;
				d->spel = spells[i];
				spells[i] = NULL; /* prevent free */
				dynarray_add((void ***)&argar, d);
			}
		}

		/* no need to check the other way around, since the counts are equal */
		if(spells)
			dynarray_free((void ***)&spells, free);

		args->arglist = argar;

		if(curtok != token_open_block)
			die_at(&args->where, "no code for old-style function");
		//args->code = parse_code();
	}

empty_func:

	return args;
}

decl_ptr *parse_decl_ptr_rec(enum decl_mode mode, char **decl_sp, funcargs **decl_args)
{
	decl_ptr *ret = NULL;

	if(accept(token_open_paren)){
		ret = parse_decl_ptr_array(mode, decl_sp, decl_args);
		EAT(token_close_paren);

	}else if(accept(token_multiply)){
		ret = decl_ptr_new();

		if(accept(token_const))
			ret->is_const = 1;

		ret->child = parse_decl_ptr_array(mode, decl_sp, decl_args); /* check if we have anything else */

	}else{
		if(curtok == token_identifier){
			if(mode & DECL_SPEL_NO)
				die_at(NULL, "identifier unexpected");

			if(*decl_sp)
				die_at(NULL, "already got identifier for decl");
			*decl_sp = token_current_spel();
			EAT(token_identifier);

		}else if(mode & DECL_SPEL_NEED){
			die_at(NULL, "need identifier for decl");
		}

		/*
		 * here is the end of the line, from now on,
		 * we are coming out of the recursion
		 * so we check for the initial decl func parms
		 */
		if(accept(token_open_paren)){
			*decl_args = parse_func_arglist();
			EAT(token_close_paren);
		}
	}

	if(ret && accept(token_open_paren)){
		/*
			* e.g.:
			* int (*x)(
			*/
		if(ret->fptrargs)
			goto func_ret_func;

		ret->fptrargs = parse_func_arglist();
		EAT(token_close_paren);

		if(accept(token_open_paren))
func_ret_func:
			die_at(&ret->where, "can't have function returning function");
	}

	return ret;
}

decl_ptr *parse_decl_ptr_array(enum decl_mode mode, char **decl_sp, funcargs **decl_args)
{
	decl_ptr *dp = parse_decl_ptr_rec(mode, decl_sp, decl_args);

	while(accept(token_open_square)){
		decl_ptr *dp_new;
		expr *size;

		if(accept(token_close_square)){
			/* take size as zero */
			size = expr_new_val(0);
		}else{
			/* fold.c checks for const-ness */
			size = parse_expr();
			EAT(token_close_square);
		}

		dp_new = decl_ptr_new();

		/* is this right? t'other way around? append to leaf? */
		dp_new->child = dp;
		dp_new->array_size = size;

		dp = dp_new;
	}

	return dp;
}

decl *parse_decl(type *t, enum decl_mode mode)
{
	char *spel = NULL;
	funcargs *args = NULL;
	decl_ptr *dp;
	decl *d;

	dp = parse_decl_ptr_array(mode, &spel, &args);

	if(t->tdef){
		/* get the typedef stuff now */
		d = decl_copy(t->tdef);

		d->type->tdef = NULL;
		d->type->spec |= t->spec;

		*decl_leaf(d) = dp;
	}else{
		d = decl_new();
		d->decl_ptr = dp;
		d->type = type_copy(t);
	}

	d->spel     = spel;
	d->funcargs = args;

	if(accept(token_assign))
		d->init = parse_expr_funcallarg(); /* int x = 5, j; - don't grab the comma expr */
	else if(d->funcargs && curtok == token_open_block)
		d->func_code = parse_code();

	return d;
}

decl *parse_decl_single(enum decl_mode mode)
{
	type *t = parse_type();

	if(!t){
		if(mode & DECL_CAN_DEFAULT){
			INT_TYPE(t);
			cc1_warn_at(&t->where, 0, WARN_IMPLICIT_INT, "defaulting type to int");
		}else{
			return NULL;
		}
	}

	if(t->spec & spec_typedef)
		die_at(&t->where, "typedef unexpected");

	return parse_decl(t, mode);
}

decl **parse_decls(const int can_default, const int accept_field_width)
{
	const enum decl_mode parse_flag = can_default ? DECL_CAN_DEFAULT : 0;
	decl **decls = NULL;
	decl *last;
	int are_tdefs;

	/* read a type, then *spels separated by commas, then a semi colon, then repeat */
	for(;;){
		type *t;

		last = NULL;
		are_tdefs = 0;

		t = parse_type();

		if(!t){
			/* can_default makes sure we don't parse { int *p; *p = 5; } the latter as a decl */
			if(parse_possible_decl() && can_default){
				INT_TYPE(t);
				cc1_warn_at(&t->where, 0, WARN_IMPLICIT_INT, "defaulting type to int");
			}else{
				return decls;
			}
		}

		if(t->spec & spec_typedef)
			are_tdefs = 1;
		else if((t->struc || t->enu) && !parse_possible_decl())
			goto next; /* struct { int i; }; - continue to next one */

		do{
			decl *d = parse_decl(t, parse_flag);

			if(!d)
				break;

			if(!d->spel){
				/*
				 * int; - error
				 * struct A; - fine
				 */
				decl_free_notype(d);
				if(t->primitive == type_struct)
					goto next;
				die_at(&d->where, "identifier expected after decl");
			}

			dynarray_add(are_tdefs
					? (void ***)&current_scope->typedefs
					:  (void ***)&decls,
					d);

			if(are_tdefs)
				if(d->funcargs)
					die_at(&d->where, "can't have a typedef function");

			if(accept_field_width && accept(token_colon)){
				/* normal decl, check field spec */
				d->field_width = currentval.val;
				if(d->field_width <= 0)
					die_at(&d->where, "field width must be positive");
				EAT(token_integer);
			}

			last = d;
		}while(accept(token_comma));

		if(last && !last->func_code){
next:
			EAT(token_semicolon);
		}
	}
}
