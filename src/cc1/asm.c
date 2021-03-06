#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "../util/util.h"
#include "data_structs.h"
#include "cc1.h"
#include "sym.h"
#include "asm.h"
#include "../util/platform.h"
#include "../util/alloc.h"
#include "struct.h"

static int label_last = 1, str_last = 1, switch_last = 1, flow_last = 1;

char *asm_label_code(const char *fmt)
{
	int len;
	char *ret;

	len = strlen(fmt) + 10;
	ret = umalloc(len + 1);

	snprintf(ret, len, ".%s.%d", fmt, label_last++);

	return ret;
}

char *asm_label_array(int str)
{
	char *ret = umalloc(16);
	snprintf(ret, 16, "%s.%d", str ? "str" : "array", str_last++);
	return ret;
}

char *asm_label_static_local(const char *funcsp, const char *spel)
{
	char *ret;

	UCC_ASSERT(funcsp, "no spel for %s", __func__);

	ret = umalloc(strlen(funcsp) + strlen(spel) + 9);
	sprintf(ret, "%s.static_%s", funcsp, spel);
	return ret;
}

char *asm_label_goto(char *lbl)
{
	char *ret = umalloc(strlen(lbl) + 6);
	sprintf(ret, ".lbl_%s", lbl);
	return ret;
}

char *asm_label_case(enum asm_label_type lbltype, int val)
{
	char *ret = umalloc(15 + 32);
	switch(lbltype){
		case CASE_DEF:
			sprintf(ret, ".case_%d_default", switch_last);
			break;

		case CASE_CASE:
		case CASE_RANGE:
		{
			const char *extra = "";
			if(val < 0){
				val = -val;
				extra = "m";
			}
			sprintf(ret, ".case%s_%d_%s%d", lbltype == CASE_RANGE ? "_rng" : "", switch_last, extra, val);
			break;
		}
	}
	switch_last++;
	return ret;

}

char *asm_label_flow(const char *fmt)
{
	char *ret = umalloc(16 + strlen(fmt));
	sprintf(ret, ".flow_%s_%d", fmt, flow_last++);
	return ret;
}

void asm_sym(enum asm_sym_type t, sym *s, const char *reg)
{
	const int is_global = s->type == sym_global || (s->decl->type->spec & (spec_extern | spec_static));
	char *const dsp = s->decl->spel;
	int is_auto = s->type == sym_local;
	char  stackbrackets[16];
	char *brackets;

	if(is_global){
		const int bracket_len = strlen(dsp) + 16;

		brackets = umalloc(bracket_len + 1);

		if(t == ASM_LEA || s->decl->func_code){
			snprintf(brackets, bracket_len, "%s", dsp); /* int (*p)() = printf; for example */
			/*
			 * either:
			 *   we want             lea rax, [a]
			 *   and convert this to mov rax, a   // this is because Macho-64 is an awful binary format
			 * force a mov for funcs (i.e. &func == func)
			 */
			t = ASM_LOAD;
		}else{
			const char *type_s = "";

			if(asm_type_size(s->decl) == ASM_SIZE_WORD)
				type_s = "qword ";

			/* get warnings for "lea rax, [qword tim]", just do "lea rax, [tim]" */
			snprintf(brackets, bracket_len, "[%s%s]",
					t == ASM_LEA ? "" : type_s, dsp);
		}
	}else{
		brackets = stackbrackets;
		snprintf(brackets, sizeof stackbrackets, "[rbp %c %d]",
				is_auto ? '-' : '+',
				((is_auto ? 1 : 2) * platform_word_size()) + s->offset);
	}

	asm_temp(1, "%s %s, %s ; %s%s",
			t == ASM_LEA ? "lea"    : "mov",
			t == ASM_SET ? brackets : reg,
			t == ASM_SET ? reg      : brackets,
			t == ASM_LEA ? "&"      : "",
			dsp
			);

	if(brackets != stackbrackets)
		free(brackets);
}

void asm_new(enum asm_type t, void *p)
{
	switch(t){
		case asm_assign:
			asm_temp(1, "pop rax");
			break;

		case asm_call:
			asm_temp(1, "call %s", (const char *)p);
			break;

		case asm_load_ident:
			asm_temp(1, "load %s", (const char *)p);
			break;

		case asm_load_val:
			asm_temp(1, "load val %d", *(int *)p);
			break;

		case asm_op:
			asm_temp(1, "%s", op_to_str(*(enum op_type *)p));
			break;

		case asm_pop:
			asm_temp(1, "pop");
			break;

		case asm_push:
			asm_temp(1, "push");
			break;

		case asm_addrof:
			fprintf(stderr, "BUH?? (addrof)\n");
			break;
	}
}

void asm_label(const char *lbl)
{
	asm_temp(0, "%s:", lbl);
}

void asm_out_intval(FILE *f, intval *iv)
{
	char fmt[4];
	char *p = fmt;

	*p++ = '%';
	if(iv->suffix & VAL_LONG)
		*p++ = 'l';

	strcpy(p, iv->suffix & VAL_UNSIGNED ? "u" : "d");

	fprintf(f, fmt, iv->val);
}

void asm_declare_single_part(FILE *f, expr *e)
{
	if(!e->f_gen_1)
		ICE("unexpected global initaliser %s (no gen_1())", e->f_str());

	e->f_gen_1(e, f);
}

enum asm_size asm_type_size(decl *d)
{
	if(decl_ptr_depth(d)){
		return ASM_SIZE_WORD;
	}else{
		switch(d->type->primitive){
			case type_enum:
			case type_int:
				return ASM_SIZE_WORD;

			case type_char:
				return ASM_SIZE_1;

			case type_void:
				ICE("type primitive is void");

			case type_typedef:
				return asm_type_size(d->type->tdef);

			case type_struct:
				ICE("asm_type_size of a struct - can't be word nor byte");

			case type_unknown:
				ICE("type primitive not set");
		}
	}

	ICE("asm_type_size switch error");
	return ASM_SIZE_WORD;
}

char asm_type_ch(decl *d)
{
	return asm_type_size(d) == ASM_SIZE_WORD ? 'q' : 'b';
}

void asm_declare_single(FILE *f, decl *d)
{
	fprintf(f, "%s d%c ", d->spel, asm_type_ch(d));

	asm_declare_single_part(f, d->init);

	fputc('\n', f);
}

void asm_declare_array(enum section_type output, const char *lbl, array_decl *ad)
{
	int i;

	fprintf(cc_out[output], "%s d%c ", lbl, ad->type == array_str ? 'b' : 'q');

	for(i = 0; i < ad->len; i++){
		if(ad->type == array_str)
			fprintf(cc_out[output], "%d", ad->data.str[i]);
		else
			asm_declare_single_part(cc_out[output], ad->data.exprs[i]);

		if(i < ad->len - 1)
			fputs(", ", cc_out[output]);
	}

	fputc('\n', cc_out[output]);
}

void asm_tempfv(FILE *f, int indent, const char *fmt, va_list l)
{
	if(indent)
		fputc('\t', f);

	vfprintf(f, fmt, l);

	fputc('\n', f);
}

void asm_temp(int indent, const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	asm_tempfv(cc_out[SECTION_TEXT], indent, fmt, l);
	va_end(l);
}

void asm_tempf(FILE *f, int indent, const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	asm_tempfv(f, indent, fmt, l);
	va_end(l);
}
