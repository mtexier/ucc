#ifndef SYM_H
#define SYM_H

struct sym
{
	int offset; /* stack offset */
	enum sym_type
	{
		sym_global,
		sym_local,
		sym_arg
	} type;

	decl *decl;

	/* static analysis */
	int nreads, nwrites;
};

struct symtable
{
	int auto_total_size;
	symtable *parent, **children;

	decl      **decls;
	struct_st **structs;
	enum_st   **enums;
	decl      **typedefs;
};

sym *sym_new(decl *d, enum sym_type t);

symtable *symtab_new(symtable *parent);
void      symtab_set_parent(symtable *child, symtable *parent);

symtable *symtab_root(symtable *child);

#define SYMTAB_APPEND  0
#define SYMTAB_PREPEND 1

#define SYMTAB_NO_SYM   0
#define SYMTAB_WITH_SYM 1

#define SYMTAB_ADD(tab, decl, type) symtab_add(tab, decl, type, SYMTAB_WITH_SYM, SYMTAB_APPEND)

sym  *symtab_add(   symtable *, decl *, enum sym_type, int with_sym, int prepend);
sym  *symtab_search(symtable *, const char *);
sym  *symtab_has(   symtable *, decl *);

const char *sym_to_str(enum sym_type);

#define sym_free(s) free(s)

#endif
