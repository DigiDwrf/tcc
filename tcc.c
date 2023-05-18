/*
 *  TCC - Tiny C Compiler
 *
 *  Copyright (c) 2001-2004 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "tcc.h"

#ifdef TCC_TARGET_I386
#include "i386-gen.c"
#endif

#ifdef TCC_TARGET_ARM
#include "arm-gen.c"
#endif

#ifdef TCC_TARGET_C67
#include "c67-gen.c"
#endif

#ifdef TCC_TARGET_X86_64
#include "x86_64-gen.c"
#endif

#ifdef TCC_TARGET_816
#include "816-gen.c"
#endif

#ifdef CONFIG_TCC_STATIC

#define RTLD_LAZY 0x001
#define RTLD_NOW 0x002
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT NULL

/* dummy function for profiling */
void *dlopen(const char *filename, int flag)
{
    return NULL;
}

void dlclose(void *p) {}

const char *dlerror(void)
{
    return "error";
}

typedef struct TCCSyms
{
    char *str;
    void *ptr;
} TCCSyms;

#define TCCSYM(a) \
    {             \
        #a,       \
        &a,       \
    },

/* add the symbol you want here if no dynamic linking is done */
static TCCSyms tcc_syms[] = {
#if !defined(CONFIG_TCCBOOT)
    TCCSYM(printf) TCCSYM(fprintf) TCCSYM(fopen) TCCSYM(fclose)
#endif
        {NULL, NULL},
};

void *resolve_sym(TCCState *s1, const char *symbol, int type)
{
    TCCSyms *p;
    p = tcc_syms;
    while (p->str != NULL) {
        if (!strcmp(p->str, symbol))
            return p->ptr;
        p++;
    }
    return NULL;
}

#elif !defined(_WIN32)

#include <dlfcn.h>

void *resolve_sym(TCCState *s1, const char *sym, int type)
{
    return dlsym(RTLD_DEFAULT, sym);
}

#endif

/********************************************************/

/* we use our own 'finite' function to avoid potential problems with
   non standard math libs */
/* XXX: endianness dependent */
int ieee_finite(double d)
{
    int *p = (int *) &d;
    return ((unsigned) ((p[1] | 0x800fffff) + 1)) >> 31;
}

/* copy a string and truncate it. */
static char *pstrcpy(char *buf, int buf_size, const char *s)
{
    char *q, *q_end;
    int c;

    if (buf_size > 0) {
        q = buf;
        q_end = buf + buf_size - 1;
        while (q < q_end) {
            c = *s++;
            if (c == '\0')
                break;
            *q++ = c;
        }
        *q = '\0';
    }
    return buf;
}

/* strcat and truncate. */
static char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

#ifndef LIBTCC
static int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}
#endif

#ifdef _WIN32
#define IS_PATHSEP(c) (c == '/' || c == '\\')
#define IS_ABSPATH(p) (IS_PATHSEP(p[0]) || (p[0] && p[1] == ':' && IS_PATHSEP(p[2])))
#define PATHCMP stricmp
#else
#define IS_PATHSEP(c) (c == '/')
#define IS_ABSPATH(p) IS_PATHSEP(p[0])
#define PATHCMP strcmp
#endif

/* extract the basename of a file */
static char *tcc_basename(const char *name)
{
    char *p = strchr(name, 0);
    while (p > name && !IS_PATHSEP(p[-1]))
        --p;
    return p;
}

static char *tcc_fileextension(const char *name)
{
    char *b = tcc_basename(name);
    char *e = strrchr(b, '.');
    return e ? e : strchr(b, 0);
}

#ifdef _WIN32
char *normalize_slashes(char *path)
{
    char *p;
    for (p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
    return path;
}

void tcc_set_lib_path_w32(TCCState *s)
{
    /* on win32, we suppose the lib and includes are at the location
       of 'tcc.exe' */
    char path[1024], *p;
    GetModuleFileNameA(NULL, path, sizeof path);
    p = tcc_basename(normalize_slashes(strlwr(path)));
    if (p - 5 > path && 0 == strncmp(p - 5, "/bin/", 5))
        p -= 5;
    else if (p > path)
        p--;
    *p = 0;
    tcc_set_lib_path(s, path);
}
#endif

void set_pages_executable(void *ptr, unsigned long length)
{
#ifdef _WIN32
    unsigned long old_protect;
    VirtualProtect(ptr, length, PAGE_EXECUTE_READWRITE, &old_protect);
#else
    unsigned long start, end;
    start = (unsigned long) ptr & ~(PAGESIZE - 1);
    end = (unsigned long) ptr + length;
    end = (end + PAGESIZE - 1) & ~(PAGESIZE - 1);
    mprotect((void *) start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
}

/* memory management */
#ifdef MEM_DEBUG
int mem_cur_size;
int mem_max_size;
unsigned malloc_usable_size(void *);
#endif

static inline void tcc_free(void *ptr)
{
#ifdef MEM_DEBUG
    mem_cur_size -= malloc_usable_size(ptr);
#endif
    free(ptr);
}

static void *tcc_malloc(unsigned long size)
{
    void *ptr;
    ptr = malloc(size);
    if (!ptr && size)
        error("memory full");
#ifdef MEM_DEBUG
    mem_cur_size += malloc_usable_size(ptr);
    if (mem_cur_size > mem_max_size)
        mem_max_size = mem_cur_size;
#endif
    return ptr;
}

static void *tcc_mallocz(unsigned long size)
{
    void *ptr;
    ptr = tcc_malloc(size);
    memset(ptr, 0, size);
    return ptr;
}

static inline void *tcc_realloc(void *ptr, unsigned long size)
{
    void *ptr1;
#ifdef MEM_DEBUG
    mem_cur_size -= malloc_usable_size(ptr);
#endif
    ptr1 = realloc(ptr, size);
#ifdef MEM_DEBUG
    /* NOTE: count not correct if alloc error, but not critical */
    mem_cur_size += malloc_usable_size(ptr1);
    if (mem_cur_size > mem_max_size)
        mem_max_size = mem_cur_size;
#endif
    return ptr1;
}

static char *tcc_strdup(const char *str)
{
    char *ptr;
    ptr = tcc_malloc(strlen(str) + 1);
    strcpy(ptr, str);
    return ptr;
}

#define free(p) use_tcc_free(p)
#define malloc(s) use_tcc_malloc(s)
#define realloc(p, s) use_tcc_realloc(p, s)

static void dynarray_add(void ***ptab, int *nb_ptr, void *data)
{
    int nb, nb_alloc;
    void **pp;

    nb = *nb_ptr;
    pp = *ptab;
    /* every power of two we double array size */
    if ((nb & (nb - 1)) == 0) {
        if (!nb)
            nb_alloc = 1;
        else
            nb_alloc = nb * 2;
        pp = tcc_realloc(pp, nb_alloc * sizeof(void *));
        if (!pp)
            error("memory full");
        *ptab = pp;
    }
    pp[nb++] = data;
    *nb_ptr = nb;
}

static void dynarray_reset(void *pp, int *n)
{
    void **p;
    for (p = *(void ***) pp; *n; ++p, --*n)
        if (*p)
            tcc_free(*p);
    tcc_free(*(void **) pp);
    *(void **) pp = NULL;
}

/* symbol allocator */
static Sym *__sym_malloc(void)
{
    Sym *sym_pool, *sym, *last_sym;
    int i;

    sym_pool = tcc_malloc(SYM_POOL_NB * sizeof(Sym));
    dynarray_add(&sym_pools, &nb_sym_pools, sym_pool);

    last_sym = sym_free_first;
    sym = sym_pool;
    for (i = 0; i < SYM_POOL_NB; i++) {
        sym->next = last_sym;
        last_sym = sym;
        sym++;
    }
    sym_free_first = last_sym;
    return last_sym;
}

static inline Sym *sym_malloc(void)
{
    Sym *sym;
    sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();
    sym_free_first = sym->next;
    return sym;
}

static inline void sym_free(Sym *sym)
{
    sym->next = sym_free_first;
    sym_free_first = sym;
}

Section *new_section(TCCState *s1, const char *name, int sh_type, int sh_flags)
{
    Section *sec;

    sec = tcc_mallocz(sizeof(Section) + strlen(name));
    strcpy(sec->name, name);
    sec->sh_type = sh_type;
    sec->sh_flags = sh_flags;
    switch (sh_type) {
    case SHT_HASH:
    case SHT_REL:
    case SHT_RELA:
    case SHT_DYNSYM:
    case SHT_SYMTAB:
    case SHT_DYNAMIC:
        sec->sh_addralign = 4;
        break;
    case SHT_STRTAB:
        sec->sh_addralign = 1;
        break;
    default:
        sec->sh_addralign = 32; /* default conservative alignment */
        break;
    }

    if (sh_flags & SHF_PRIVATE) {
        dynarray_add((void ***) &s1->priv_sections, &s1->nb_priv_sections, sec);
    } else {
        sec->sh_num = s1->nb_sections;
        dynarray_add((void ***) &s1->sections, &s1->nb_sections, sec);
    }

    return sec;
}

static void free_section(Section *s)
{
    tcc_free(s->data);
}

/* realloc section and set its content to zero */
static void section_realloc(Section *sec, unsigned long new_size)
{
    unsigned long size;
    unsigned char *data;

    size = sec->data_allocated;
    if (size == 0)
        size = 1;
    while (size < new_size)
        size = size * 2;
    data = tcc_realloc(sec->data, size);
    if (!data)
        error("memory full");
    memset(data + sec->data_allocated, 0, size - sec->data_allocated);
    sec->data = data;
    sec->data_allocated = size;
}

/* reserve at least 'size' bytes in section 'sec' from
   sec->data_offset. */
static void *section_ptr_add(Section *sec, unsigned long size)
{
    unsigned long offset, offset1;

    offset = sec->data_offset;
    offset1 = offset + size;
    if (offset1 > sec->data_allocated)
        section_realloc(sec, offset1);
    sec->data_offset = offset1;
    return sec->data + offset;
}

/* return a reference to a section, and create it if it does not
   exists */
Section *find_section(TCCState *s1, const char *name)
{
    Section *sec;
    int i;
    for (i = 1; i < s1->nb_sections; i++) {
        sec = s1->sections[i];
        if (!strcmp(name, sec->name))
            return sec;
    }
    /* sections are created as PROGBITS */
    return new_section(s1, name, SHT_PROGBITS, SHF_ALLOC);
}

#define SECTION_ABS ((void *) 1)

/* update sym->c so that it points to an external symbol in section
   'section' with value 'value' */
static void put_extern_sym2(
    Sym *sym, Section *section, unsigned long value, unsigned long size, int can_add_underscore)
{
    int sym_type, sym_bind, sh_num, info, other, attr;
    ElfW(Sym) * esym;
    const char *name;
    char buf1[256];

    if (section == NULL)
        sh_num = SHN_UNDEF;
    else if (section == SECTION_ABS)
        sh_num = SHN_ABS;
    else
        sh_num = section->sh_num;

    other = attr = 0;

    if ((sym->type.t & VT_BTYPE) == VT_FUNC) {
        sym_type = STT_FUNC;
#ifdef TCC_TARGET_PE
        if (sym->type.ref)
            attr = sym->type.ref->r;
        if (FUNC_EXPORT(attr))
            other |= 1;
        if (FUNC_CALL(attr) == FUNC_STDCALL)
            other |= 2;
#endif
    } else {
        sym_type = STT_OBJECT;
    }

    if (sym->type.t & VT_STATIC)
        sym_bind = STB_LOCAL;
    else
        sym_bind = STB_GLOBAL;

    if (!sym->c) {
        name = get_tok_str(sym->v, NULL);
#ifdef CONFIG_TCC_BCHECK
        if (do_bounds_check) {
            char buf[32];

            /* XXX: avoid doing that for statics ? */
            /* if bound checking is activated, we change some function
               names by adding the "__bound" prefix */
            switch (sym->v) {
#if 0
            /* XXX: we rely only on malloc hooks */
            case TOK_malloc:
            case TOK_free:
            case TOK_realloc:
            case TOK_memalign:
            case TOK_calloc:
#endif
            case TOK_memcpy:
            case TOK_memmove:
            case TOK_memset:
            case TOK_strlen:
            case TOK_strcpy:
            case TOK__alloca:
                strcpy(buf, "__bound_");
                strcat(buf, name);
                name = buf;
                break;
            }
        }
#endif

#ifdef TCC_TARGET_PE
        if ((other & 2) && can_add_underscore) {
            sprintf(buf1, "_%s@%d", name, FUNC_ARGS(attr));
            name = buf1;
        } else
#endif
            if (tcc_state->leading_underscore && can_add_underscore) {
            buf1[0] = '_';
            pstrcpy(buf1 + 1, sizeof(buf1) - 1, name);
            name = buf1;
        }
#ifdef TCC_TARGET_816
        if (sym->type.t & VT_STATIC) {
            if ((sym->type.t & VT_STATICLOCAL) && current_fn[0] != 0)
                sprintf(buf1, "%s_FUNC_%s_", static_prefix, current_fn);
            else
                strcpy(buf1, static_prefix);
            strcat(buf1, name);
            name = buf1;
        }
#endif
        info = ELFW(ST_INFO)(sym_bind, sym_type);
        sym->c = add_elf_sym(symtab_section, value, size, info, other, sh_num, name);
    } else {
        esym = &((ElfW(Sym) *) symtab_section->data)[sym->c];
        esym->st_value = value;
        esym->st_size = size;
        esym->st_shndx = sh_num;
        esym->st_other |= other;
    }
}

static void put_extern_sym(Sym *sym, Section *section, unsigned long value, unsigned long size)
{
    put_extern_sym2(sym, section, value, size, 1);
}

/* add a new relocation entry to symbol 'sym' in section 's' */
static void greloc(Section *s, Sym *sym, unsigned long offset, int type)
{
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    /* now we can add ELF relocation info */
    put_elf_reloc(symtab_section, s, offset, type, sym->c);
}

static inline int isid(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline int isnum(int c)
{
    return c >= '0' && c <= '9';
}

static inline int isoct(int c)
{
    return c >= '0' && c <= '7';
}

static inline int toup(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    else
        return c;
}

static void strcat_vprintf(char *buf, int buf_size, const char *fmt, va_list ap)
{
    int len;
    len = strlen(buf);
    vsnprintf(buf + len, buf_size - len, fmt, ap);
}

static void strcat_printf(char *buf, int buf_size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    strcat_vprintf(buf, buf_size, fmt, ap);
    va_end(ap);
}

void error1(TCCState *s1, int is_warning, const char *fmt, va_list ap)
{
    char buf[2048];
    BufferedFile **f;

    buf[0] = '\0';
    if (file) {
        for (f = s1->include_stack; f < s1->include_stack_ptr; f++)
            strcat_printf(buf,
                          sizeof(buf),
                          "In file included from %s:%d:\n",
                          (*f)->filename,
                          (*f)->line_num);
        if (file->line_num > 0) {
            strcat_printf(buf, sizeof(buf), "%s:%d: ", file->filename, file->line_num);
        } else {
            strcat_printf(buf, sizeof(buf), "%s: ", file->filename);
        }
    } else {
        strcat_printf(buf, sizeof(buf), "tcc: ");
    }
    if (is_warning)
        strcat_printf(buf, sizeof(buf), "warning: ");
    strcat_vprintf(buf, sizeof(buf), fmt, ap);

    if (!s1->error_func) {
        /* default case: stderr */
        fprintf(stderr, "%s\n", buf);
    } else {
        s1->error_func(s1->error_opaque, buf);
    }
    if (!is_warning || s1->warn_error)
        s1->nb_errors++;
}

#ifdef LIBTCC
void tcc_set_error_func(TCCState *s,
                        void *error_opaque,
                        void (*error_func)(void *opaque, const char *msg))
{
    s->error_opaque = error_opaque;
    s->error_func = error_func;
}
#endif

/* error without aborting current compilation */
void error_noabort(const char *fmt, ...)
{
    TCCState *s1 = tcc_state;
    va_list ap;

    va_start(ap, fmt);
    error1(s1, 0, fmt, ap);
    va_end(ap);
}

void error(const char *fmt, ...)
{
    TCCState *s1 = tcc_state;
    va_list ap;

    va_start(ap, fmt);
    error1(s1, 0, fmt, ap);
    va_end(ap);
    /* better than nothing: in some cases, we accept to handle errors */
    if (s1->error_set_jmp_enabled) {
#ifdef TCC_TARGET_816
        // Alek 201125 hangs on winxp longjmp(s1->error_jmp_buf, 1);
        exit(1);
#else
        longjmp(s1->error_jmp_buf, 1);
#endif
    } else {
        /* XXX: eliminate this someday */
        exit(1);
    }
}

void expect(const char *msg)
{
    error("%s expected", msg);
}

void warning(const char *fmt, ...)
{
    TCCState *s1 = tcc_state;
    va_list ap;

    if (s1->warn_none)
        return;

    va_start(ap, fmt);
    error1(s1, 1, fmt, ap);
    va_end(ap);
}

void skip(int c)
{
    if (tok != c)
        error("'%c' expected", c);
    next();
}

static void test_lvalue(void)
{
    if (!(vtop->r & VT_LVAL))
        expect("lvalue");
}

/* allocate a new token */
static TokenSym *tok_alloc_new(TokenSym **pts, const char *str, int len)
{
    TokenSym *ts, **ptable;
    int i;

    if (tok_ident >= SYM_FIRST_ANOM)
        error("memory full");

    /* expand token table if needed */
    i = tok_ident - TOK_IDENT;
    if ((i % TOK_ALLOC_INCR) == 0) {
        ptable = tcc_realloc(table_ident, (i + TOK_ALLOC_INCR) * sizeof(TokenSym *));
        if (!ptable)
            error("memory full");
        table_ident = ptable;
    }

    ts = tcc_malloc(sizeof(TokenSym) + len);
    table_ident[i] = ts;
    ts->tok = tok_ident++;
    ts->sym_define = NULL;
    ts->sym_label = NULL;
    ts->sym_struct = NULL;
    ts->sym_identifier = NULL;
    ts->len = len;
    ts->hash_next = NULL;
    memcpy(ts->str, str, len);
    ts->str[len] = '\0';
    *pts = ts;
    return ts;
}

#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) *263 + (c))

/* find a token and add it if not found */
static TokenSym *tok_alloc(const char *str, int len)
{
    TokenSym *ts, **pts;
    int i;
    unsigned int h;

    h = TOK_HASH_INIT;
    for (i = 0; i < len; i++)
        h = TOK_HASH_FUNC(h, ((unsigned char *) str)[i]);
    h &= (TOK_HASH_SIZE - 1);

    pts = &hash_ident[h];
    for (;;) {
        ts = *pts;
        if (!ts)
            break;
        if (ts->len == len && !memcmp(ts->str, str, len))
            return ts;
        pts = &(ts->hash_next);
    }
    return tok_alloc_new(pts, str, len);
}

/* CString handling */

static void cstr_realloc(CString *cstr, int new_size)
{
    int size;
    void *data;

    size = cstr->size_allocated;
    if (size == 0)
        size = 8; /* no need to allocate a too small first string */
    while (size < new_size)
        size = size * 2;
    data = tcc_realloc(cstr->data_allocated, size);
    if (!data)
        error("memory full");
    cstr->data_allocated = data;
    cstr->size_allocated = size;
    cstr->data = data;
}

/* add a byte */
static inline void cstr_ccat(CString *cstr, int ch)
{
    int size;
    size = cstr->size + 1;
    if (size > cstr->size_allocated)
        cstr_realloc(cstr, size);
    ((unsigned char *) cstr->data)[size - 1] = ch;
    cstr->size = size;
}

static void cstr_cat(CString *cstr, const char *str)
{
    int c;
    for (;;) {
        c = *str;
        if (c == '\0')
            break;
        cstr_ccat(cstr, c);
        str++;
    }
}

/* add a wide char */
static void cstr_wccat(CString *cstr, int ch)
{
    int size;
    size = cstr->size + sizeof(nwchar_t);
    if (size > cstr->size_allocated)
        cstr_realloc(cstr, size);
    *(nwchar_t *) (((unsigned char *) cstr->data) + size - sizeof(nwchar_t)) = ch;
    cstr->size = size;
}

static void cstr_new(CString *cstr)
{
    memset(cstr, 0, sizeof(CString));
}

/* free string and reset it to NULL */
static void cstr_free(CString *cstr)
{
    tcc_free(cstr->data_allocated);
    cstr_new(cstr);
}

#define cstr_reset(cstr) cstr_free(cstr)

/* XXX: unicode ? */
static void add_char(CString *cstr, int c)
{
    if (c == '\'' || c == '\"' || c == '\\') {
        /* XXX: could be more precise if char or string */
        cstr_ccat(cstr, '\\');
    }
    if (c >= 32 && c <= 126) {
        cstr_ccat(cstr, c);
    } else {
        cstr_ccat(cstr, '\\');
        if (c == '\n') {
            cstr_ccat(cstr, 'n');
        } else {
            cstr_ccat(cstr, '0' + ((c >> 6) & 7));
            cstr_ccat(cstr, '0' + ((c >> 3) & 7));
            cstr_ccat(cstr, '0' + (c & 7));
        }
    }
}

/* XXX: buffer overflow */
/* XXX: float tokens */
char *get_tok_str(int v, CValue *cv)
{
    static char buf[STRING_MAX_SIZE + 1];
    static CString cstr_buf;
    CString *cstr;
    unsigned char *q;
    char *p;
    int i, len;

    /* NOTE: to go faster, we give a fixed buffer for small strings */
    cstr_reset(&cstr_buf);
    cstr_buf.data = buf;
    cstr_buf.size_allocated = sizeof(buf);
    p = buf;

    switch (v) {
    case TOK_CINT:
    case TOK_CUINT:
        /* XXX: not quite exact, but only useful for testing */
        sprintf(p, "%u", cv->ui);
        break;
    case TOK_CLLONG:
    case TOK_CULLONG:
        /* XXX: not quite exact, but only useful for testing  */
        sprintf(p, "%Lu", cv->ull);
        break;
    case TOK_LCHAR:
        cstr_ccat(&cstr_buf, 'L');
    case TOK_CCHAR:
        cstr_ccat(&cstr_buf, '\'');
        add_char(&cstr_buf, cv->i);
        cstr_ccat(&cstr_buf, '\'');
        cstr_ccat(&cstr_buf, '\0');
        break;
    case TOK_PPNUM:
        cstr = cv->cstr;
        len = cstr->size - 1;
        for (i = 0; i < len; i++)
            add_char(&cstr_buf, ((unsigned char *) cstr->data)[i]);
        cstr_ccat(&cstr_buf, '\0');
        break;
    case TOK_LSTR:
        cstr_ccat(&cstr_buf, 'L');
    case TOK_STR:
        cstr = cv->cstr;
        cstr_ccat(&cstr_buf, '\"');
        if (v == TOK_STR) {
            len = cstr->size - 1;
            for (i = 0; i < len; i++)
                add_char(&cstr_buf, ((unsigned char *) cstr->data)[i]);
        } else {
            len = (cstr->size / sizeof(nwchar_t)) - 1;
            for (i = 0; i < len; i++)
                add_char(&cstr_buf, ((nwchar_t *) cstr->data)[i]);
        }
        cstr_ccat(&cstr_buf, '\"');
        cstr_ccat(&cstr_buf, '\0');
        break;
    case TOK_LT:
        v = '<';
        goto addv;
    case TOK_GT:
        v = '>';
        goto addv;
    case TOK_DOTS:
        return strcpy(p, "...");
    case TOK_A_SHL:
        return strcpy(p, "<<=");
    case TOK_A_SAR:
        return strcpy(p, ">>=");
    default:
        if (v < TOK_IDENT) {
            /* search in two bytes table */
            q = tok_two_chars;
            while (*q) {
                if (q[2] == v) {
                    *p++ = q[0];
                    *p++ = q[1];
                    *p = '\0';
                    return buf;
                }
                q += 3;
            }
        addv:
            *p++ = v;
            *p = '\0';
        } else if (v < tok_ident) {
            return table_ident[v - TOK_IDENT]->str;
        } else if (v >= SYM_FIRST_ANOM) {
            /* special name for anonymous symbol */
#ifdef TCC_TARGET_816
            sprintf(p,
                    "L.%s%d",
                    sztmpnam,
                    v - SYM_FIRST_ANOM); // Alekmaul 201125, add temp file name to token name
#else
            sprintf(p, "L.%u", v - SYM_FIRST_ANOM);
#endif
        } else {
            /* should never happen */
            return NULL;
        }
        break;
    }
    return cstr_buf.data;
}

/* push, without hashing */
static Sym *sym_push2(Sym **ps, int v, int t, long c)
{
    Sym *s;
    s = sym_malloc();
    s->v = v;
    s->type.t = t;
    s->c = c;
    s->next = NULL;
    /* add in stack */
    s->prev = *ps;
    *ps = s;
    return s;
}

/* find a symbol and return its associated structure. 's' is the top
   of the symbol stack */
static Sym *sym_find2(Sym *s, int v)
{
    while (s) {
        if (s->v == v)
            return s;
        s = s->prev;
    }
    return NULL;
}

/* structure lookup */
static inline Sym *struct_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned) v >= (unsigned) (tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_struct;
}

/* find an identifier */
static inline Sym *sym_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned) v >= (unsigned) (tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}

/* push a given symbol on the symbol stack */
static Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    TokenSym *ts;

    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
    s->r = r;
    /* don't record fields or anonymous symbols */
    /* XXX: simplify */
    if (!(v & SYM_FIELD) && (v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        /* record symbol in token array */
        ts = table_ident[(v & ~SYM_STRUCT) - TOK_IDENT];
        if (v & SYM_STRUCT)
            ps = &ts->sym_struct;
        else
            ps = &ts->sym_identifier;
        s->prev_tok = *ps;
        *ps = s;
    }
    return s;
}

/* push a global identifier */
static Sym *global_identifier_push(int v, int t, int c)
{
    Sym *s, **ps;
    s = sym_push2(&global_stack, v, t, c);
    /* don't record anonymous symbol */
    if (v < SYM_FIRST_ANOM) {
        ps = &table_ident[v - TOK_IDENT]->sym_identifier;
        /* modify the top most local identifier, so that
           sym_identifier will point to 's' when popped */
        while (*ps != NULL)
            ps = &(*ps)->prev_tok;
        s->prev_tok = NULL;
        *ps = s;
    }
    return s;
}

/* pop symbols until top reaches 'b' */
static void sym_pop(Sym **ptop, Sym *b)
{
    Sym *s, *ss, **ps;
    TokenSym *ts;
    int v;

    s = *ptop;
    while (s != b) {
        ss = s->prev;
        v = s->v;
        /* remove symbol in token array */
        /* XXX: simplify */
        if (!(v & SYM_FIELD) && (v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
            ts = table_ident[(v & ~SYM_STRUCT) - TOK_IDENT];
            if (v & SYM_STRUCT)
                ps = &ts->sym_struct;
            else
                ps = &ts->sym_identifier;
            *ps = s->prev_tok;
        }
        sym_free(s);
        s = ss;
    }
    *ptop = b;
}

/* I/O layer */

BufferedFile *tcc_open(TCCState *s1, const char *filename)
{
    int fd;
    BufferedFile *bf;

    if (strcmp(filename, "-") == 0)
        fd = 0, filename = "stdin";
    else
        fd = open(filename, O_RDONLY | O_BINARY);
    if ((verbose == 2 && fd >= 0) || verbose == 3)
        printf("%s %*s%s\n",
               fd < 0 ? "nf" : "->",
               (int) (s1->include_stack_ptr - s1->include_stack),
               "",
               filename);
    if (fd < 0)
        return NULL;
    bf = tcc_malloc(sizeof(BufferedFile));
    bf->fd = fd;
    bf->buf_ptr = bf->buffer;
    bf->buf_end = bf->buffer;
    bf->buffer[0] = CH_EOB; /* put eob symbol */
    pstrcpy(bf->filename, sizeof(bf->filename), filename);
#ifdef _WIN32
    normalize_slashes(bf->filename);
#endif
    bf->line_num = 1;
    bf->ifndef_macro = 0;
    bf->ifdef_stack_ptr = s1->ifdef_stack_ptr;
    //    printf("opening '%s'\n", filename);
    return bf;
}

void tcc_close(BufferedFile *bf)
{
    total_lines += bf->line_num;
    close(bf->fd);
    tcc_free(bf);
}

#include "tccpp.c"
#include "tccgen.c"

/* better than nothing, but needs extension to handle '-E' option
   correctly too */
static void preprocess_init(TCCState *s1)
{
    s1->include_stack_ptr = s1->include_stack;
    /* XXX: move that before to avoid having to initialize
       file->ifdef_stack_ptr ? */
    s1->ifdef_stack_ptr = s1->ifdef_stack;
    file->ifdef_stack_ptr = s1->ifdef_stack_ptr;

    /* XXX: not ANSI compliant: bound checking says error */
    vtop = vstack - 1;
    s1->pack_stack[0] = 0;
    s1->pack_stack_ptr = s1->pack_stack;
}

/* compile the C file opened in 'file'. Return non zero if errors. */
static int tcc_compile(TCCState *s1)
{
    Sym *define_start;
    char buf[512];
    volatile int section_sym;

#ifdef INC_DEBUG
    printf("%s: **** new file\n", file->filename);
#endif
    preprocess_init(s1);

    cur_text_section = NULL;
    funcname = "";
    anon_sym = SYM_FIRST_ANOM;

    /* file info: full path + filename */
    section_sym = 0; /* avoid warning */
    if (do_debug) {
        section_sym = put_elf_sym(symtab_section,
                                  0,
                                  0,
                                  ELFW(ST_INFO)(STB_LOCAL, STT_SECTION),
                                  0,
                                  text_section->sh_num,
                                  NULL);
        getcwd(buf, sizeof(buf));
#ifdef _WIN32
        normalize_slashes(buf);
#endif
        pstrcat(buf, sizeof(buf), "/");
        put_stabs_r(buf, N_SO, 0, 0, text_section->data_offset, text_section, section_sym);
        put_stabs_r(file->filename, N_SO, 0, 0, text_section->data_offset, text_section, section_sym);
    }
    /* an elf symbol of type STT_FILE must be put so that STB_LOCAL
       symbols can be safely used */
    put_elf_sym(symtab_section, 0, 0, ELFW(ST_INFO)(STB_LOCAL, STT_FILE), 0, SHN_ABS, file->filename);

    /* define some often used types */
    int_type.t = VT_INT;
#ifdef TCC_TARGET_816
    ptr_type.t = VT_PTR;
#endif

    char_pointer_type.t = VT_BYTE;
    mk_pointer(&char_pointer_type);

    func_old_type.t = VT_FUNC;
    func_old_type.ref = sym_push(SYM_FIELD, &int_type, FUNC_CDECL, FUNC_OLD);

#if defined(TCC_ARM_EABI) && defined(TCC_ARM_VFP)
    float_type.t = VT_FLOAT;
    double_type.t = VT_DOUBLE;

    func_float_type.t = VT_FUNC;
    func_float_type.ref = sym_push(SYM_FIELD, &float_type, FUNC_CDECL, FUNC_OLD);
    func_double_type.t = VT_FUNC;
    func_double_type.ref = sym_push(SYM_FIELD, &double_type, FUNC_CDECL, FUNC_OLD);
#endif

#if 0
    /* define 'void *alloca(unsigned int)' builtin function */
    {
        Sym *s1;

        p = anon_sym++;
        sym = sym_push(p, mk_pointer(VT_VOID), FUNC_CDECL, FUNC_NEW);
        s1 = sym_push(SYM_FIELD, VT_UNSIGNED | VT_INT, 0, 0);
        s1->next = NULL;
        sym->next = s1;
        sym_push(TOK_alloca, VT_FUNC | (p << VT_STRUCT_SHIFT), VT_CONST, 0);
    }
#endif

    define_start = define_stack;
    nocode_wanted = 1;

    if (setjmp(s1->error_jmp_buf) == 0) {
        s1->nb_errors = 0;
        s1->error_set_jmp_enabled = 1;

        ch = file->buf_ptr[0];
        tok_flags = TOK_FLAG_BOL | TOK_FLAG_BOF;
        parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM;
        next();
        decl(VT_CONST);
        if (tok != TOK_EOF)
            expect("declaration");

        /* end of translation unit info */
        if (do_debug) {
            put_stabs_r(NULL, N_SO, 0, 0, text_section->data_offset, text_section, section_sym);
        }
    }
    s1->error_set_jmp_enabled = 0;

    /* reset define stack, but leave -Dsymbols (may be incorrect if
       they are undefined) */
    free_defines(define_start);

    gen_inline_functions();

#ifndef TCC_TARGET_816
    sym_pop(&global_stack, NULL);
    sym_pop(&local_stack, NULL);
#endif

    return s1->nb_errors != 0 ? -1 : 0;
}

/* Preprocess the current file */
/* XXX: add line and file infos,
 * XXX: add options to preserve spaces (partly done, only spaces in macro are
 *      not preserved)
 */
static int tcc_preprocess(TCCState *s1)
{
    Sym *define_start;
    BufferedFile *file_ref;
    int token_seen, line_ref;

    preprocess_init(s1);
    define_start = define_stack;
    ch = file->buf_ptr[0];
    tok_flags = TOK_FLAG_BOL | TOK_FLAG_BOF;
    parse_flags = PARSE_FLAG_ASM_COMMENTS | PARSE_FLAG_PREPROCESS | PARSE_FLAG_LINEFEED
                  | PARSE_FLAG_SPACES;
    token_seen = 0;
    line_ref = 0;
    file_ref = NULL;

    for (;;) {
        next();
        if (tok == TOK_EOF) {
            break;
        } else if (tok == TOK_LINEFEED) {
            if (!token_seen)
                continue;
            ++line_ref;
            token_seen = 0;
        } else if (!token_seen) {
            int d = file->line_num - line_ref;
            if (file != file_ref || d < 0 || d >= 8)
                fprintf(s1->outfile, "# %d \"%s\"\n", file->line_num, file->filename);
            else
                while (d)
                    fputs("\n", s1->outfile), --d;
            line_ref = (file_ref = file)->line_num;
            token_seen = 1;
        }
        fputs(get_tok_str(tok, &tokc), s1->outfile);
    }
    free_defines(define_start);
    return 0;
}

#ifdef LIBTCC
int tcc_compile_string(TCCState *s, const char *str)
{
    BufferedFile bf1, *bf = &bf1;
    int ret, len;
    char *buf;

    /* init file structure */
    bf->fd = -1;
    /* XXX: avoid copying */
    len = strlen(str);
    buf = tcc_malloc(len + 1);
    if (!buf)
        return -1;
    memcpy(buf, str, len);
    buf[len] = CH_EOB;
    bf->buf_ptr = buf;
    bf->buf_end = buf + len;
    pstrcpy(bf->filename, sizeof(bf->filename), "<string>");
    bf->line_num = 1;
    file = bf;
    ret = tcc_compile(s);
    file = NULL;
    tcc_free(buf);

    /* currently, no need to close */
    return ret;
}
#endif

/* define a preprocessor symbol. A value can also be provided with the '=' operator */
void tcc_define_symbol(TCCState *s1, const char *sym, const char *value)
{
    BufferedFile bf1, *bf = &bf1;

    pstrcpy(bf->buffer, IO_BUF_SIZE, sym);
    pstrcat(bf->buffer, IO_BUF_SIZE, " ");
    /* default value */
    if (!value)
        value = "1";
    pstrcat(bf->buffer, IO_BUF_SIZE, value);

    /* init file structure */
    bf->fd = -1;
    bf->buf_ptr = bf->buffer;
    bf->buf_end = bf->buffer + strlen(bf->buffer);
    *bf->buf_end = CH_EOB;
    bf->filename[0] = '\0';
    bf->line_num = 1;
    file = bf;

    s1->include_stack_ptr = s1->include_stack;

    /* parse with define parser */
    ch = file->buf_ptr[0];
    next_nomacro();
    parse_define();
    file = NULL;
}

/* undefine a preprocessor symbol */
void tcc_undefine_symbol(TCCState *s1, const char *sym)
{
    TokenSym *ts;
    Sym *s;
    ts = tok_alloc(sym, strlen(sym));
    s = define_find(ts->tok);
    /* undefine symbol by putting an invalid name */
    if (s)
        define_undef(s);
}

#ifdef CONFIG_TCC_ASM

#ifdef TCC_TARGET_I386
#include "i386-asm.c"
#endif
#include "tccasm.c"

#else
static void asm_instr(void)
{
    error("inline asm() not supported");
}
static void asm_global_instr(void)
{
    error("inline asm() not supported");
}
#endif

#include "tccelf.c"

#ifdef TCC_TARGET_COFF
#include "tcccoff.c"
#endif

#ifdef TCC_TARGET_PE
#include "tccpe.c"
#endif

#ifndef TCC_TARGET_816
#ifdef CONFIG_TCC_BACKTRACE
/* print the position in the source file of PC value 'pc' by reading
   the stabs debug information */
static void rt_printline(unsigned long wanted_pc)
{
    Stab_Sym *sym, *sym_end;
    char func_name[128], last_func_name[128];
    unsigned long func_addr, last_pc, pc;
    const char *incl_files[INCLUDE_STACK_SIZE];
    int incl_index, len, last_line_num, i;
    const char *str, *p;

    fprintf(stderr, "0x%08lx:", wanted_pc);

    func_name[0] = '\0';
    func_addr = 0;
    incl_index = 0;
    last_func_name[0] = '\0';
    last_pc = 0xffffffff;
    last_line_num = 1;
    sym = (Stab_Sym *) stab_section->data + 1;
    sym_end = (Stab_Sym *) (stab_section->data + stab_section->data_offset);
    while (sym < sym_end) {
        switch (sym->n_type) {
            /* function start or end */
        case N_FUN:
            if (sym->n_strx == 0) {
                /* we test if between last line and end of function */
                pc = sym->n_value + func_addr;
                if (wanted_pc >= last_pc && wanted_pc < pc)
                    goto found;
                func_name[0] = '\0';
                func_addr = 0;
            } else {
                str = stabstr_section->data + sym->n_strx;
                p = strchr(str, ':');
                if (!p) {
                    pstrcpy(func_name, sizeof(func_name), str);
                } else {
                    len = p - str;
                    if (len > sizeof(func_name) - 1)
                        len = sizeof(func_name) - 1;
                    memcpy(func_name, str, len);
                    func_name[len] = '\0';
                }
                func_addr = sym->n_value;
            }
            break;
            /* line number info */
        case N_SLINE:
            pc = sym->n_value + func_addr;
            if (wanted_pc >= last_pc && wanted_pc < pc)
                goto found;
            last_pc = pc;
            last_line_num = sym->n_desc;
            /* XXX: slow! */
            strcpy(last_func_name, func_name);
            break;
            /* include files */
        case N_BINCL:
            str = stabstr_section->data + sym->n_strx;
        add_incl:
            if (incl_index < INCLUDE_STACK_SIZE) {
                incl_files[incl_index++] = str;
            }
            break;
        case N_EINCL:
            if (incl_index > 1)
                incl_index--;
            break;
        case N_SO:
            if (sym->n_strx == 0) {
                incl_index = 0; /* end of translation unit */
            } else {
                str = stabstr_section->data + sym->n_strx;
                /* do not add path */
                len = strlen(str);
                if (len > 0 && str[len - 1] != '/')
                    goto add_incl;
            }
            break;
        }
        sym++;
    }

    /* second pass: we try symtab symbols (no line number info) */
    incl_index = 0;
    {
        ElfW(Sym) * sym, *sym_end;
        int type;

        sym_end = (ElfW(Sym) *) (symtab_section->data + symtab_section->data_offset);
        for (sym = (ElfW(Sym) *) symtab_section->data + 1; sym < sym_end; sym++) {
            type = ELFW(ST_TYPE)(sym->st_info);
            if (type == STT_FUNC) {
                if (wanted_pc >= sym->st_value && wanted_pc < sym->st_value + sym->st_size) {
                    pstrcpy(last_func_name,
                            sizeof(last_func_name),
                            strtab_section->data + sym->st_name);
                    goto found;
                }
            }
        }
    }
    /* did not find any info: */
    fprintf(stderr, " ???\n");
    return;
found:
    if (last_func_name[0] != '\0') {
        fprintf(stderr, " %s()", last_func_name);
    }
    if (incl_index > 0) {
        fprintf(stderr, " (%s:%d", incl_files[incl_index - 1], last_line_num);
        for (i = incl_index - 2; i >= 0; i--)
            fprintf(stderr, ", included from %s", incl_files[i]);
        fprintf(stderr, ")");
    }
    fprintf(stderr, "\n");
}

#ifdef __i386__
/* fix for glibc 2.1 */
#ifndef REG_EIP
#define REG_EIP EIP
#define REG_EBP EBP
#endif

/* return the PC at frame level 'level'. Return non zero if not found */
static int rt_get_caller_pc(unsigned long *paddr, ucontext_t *uc, int level)
{
    unsigned long fp;
    int i;

    if (level == 0) {
#if defined(__FreeBSD__)
        *paddr = uc->uc_mcontext.mc_eip;
#elif defined(__dietlibc__)
        *paddr = uc->uc_mcontext.eip;
#else
        *paddr = uc->uc_mcontext.gregs[REG_EIP];
#endif
        return 0;
    } else {
#if defined(__FreeBSD__)
        fp = uc->uc_mcontext.mc_ebp;
#elif defined(__dietlibc__)
        fp = uc->uc_mcontext.ebp;
#else
        fp = uc->uc_mcontext.gregs[REG_EBP];
#endif
        for (i = 1; i < level; i++) {
            /* XXX: check address validity with program info */
            if (fp <= 0x1000 || fp >= 0xc0000000)
                return -1;
            fp = ((unsigned long *) fp)[0];
        }
        *paddr = ((unsigned long *) fp)[1];
        return 0;
    }
}
#elif defined(__x86_64__)
/* return the PC at frame level 'level'. Return non zero if not found */
static int rt_get_caller_pc(unsigned long *paddr, ucontext_t *uc, int level)
{
    unsigned long fp;
    int i;

    if (level == 0) {
        /* XXX: only support linux */
        *paddr = uc->uc_mcontext.gregs[REG_RIP];
        return 0;
    } else {
        fp = uc->uc_mcontext.gregs[REG_RBP];
        for (i = 1; i < level; i++) {
            /* XXX: check address validity with program info */
            if (fp <= 0x1000)
                return -1;
            fp = ((unsigned long *) fp)[0];
        }
        *paddr = ((unsigned long *) fp)[1];
        return 0;
    }
}
#else
#warning add arch specific rt_get_caller_pc()
static int rt_get_caller_pc(unsigned long *paddr, ucontext_t *uc, int level)
{
    return -1;
}
#endif

/* emit a run time error at position 'pc' */
void rt_error(ucontext_t *uc, const char *fmt, ...)
{
    va_list ap;
    unsigned long pc;
    int i;

    va_start(ap, fmt);
    fprintf(stderr, "Runtime error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    for (i = 0; i < num_callers; i++) {
        if (rt_get_caller_pc(&pc, uc, i) < 0)
            break;
        if (i == 0)
            fprintf(stderr, "at ");
        else
            fprintf(stderr, "by ");
        rt_printline(pc);
    }
    exit(255);
    va_end(ap);
}

/* signal handler for fatal errors */
static void sig_error(int signum, siginfo_t *siginf, void *puc)
{
    ucontext_t *uc = puc;

    switch (signum) {
    case SIGFPE:
        switch (siginf->si_code) {
        case FPE_INTDIV:
        case FPE_FLTDIV:
            rt_error(uc, "division by zero");
            break;
        default:
            rt_error(uc, "floating point exception");
            break;
        }
        break;
    case SIGBUS:
    case SIGSEGV:
        if (rt_bound_error_msg && *rt_bound_error_msg)
            rt_error(uc, *rt_bound_error_msg);
        else
            rt_error(uc, "dereferencing invalid pointer");
        break;
    case SIGILL:
        rt_error(uc, "illegal instruction");
        break;
    case SIGABRT:
        rt_error(uc, "abort() called");
        break;
    default:
        rt_error(uc, "caught signal %d", signum);
        break;
    }
    exit(255);
}

#endif

/* copy code into memory passed in by the caller and do all relocations
   (needed before using tcc_get_symbol()).
   returns -1 on error and required size if ptr is NULL */
int tcc_relocate(TCCState *s1, void *ptr)
{
    Section *s;
    unsigned long offset, length, mem;
    int i;

    if (0 == s1->runtime_added) {
        s1->runtime_added = 1;
        s1->nb_errors = 0;
#ifdef TCC_TARGET_PE
        pe_add_runtime(s1);
        relocate_common_syms();
        tcc_add_linker_symbols(s1);
#else
        tcc_add_runtime(s1);
        relocate_common_syms();
        tcc_add_linker_symbols(s1);
        build_got_entries(s1);
#endif
    }

    offset = 0, mem = (unsigned long) ptr;
    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (0 == (s->sh_flags & SHF_ALLOC))
            continue;
        length = s->data_offset;
        s->sh_addr = mem ? (mem + offset + 15) & ~15 : 0;
        offset = (offset + length + 15) & ~15;
    }

    /* relocate symbols */
    relocate_syms(s1, 1);
    if (s1->nb_errors)
        return -1;

#ifdef TCC_TARGET_X86_64
    s1->runtime_plt_and_got_offset = 0;
    s1->runtime_plt_and_got = (char *) (mem + offset);
    /* double the size of the buffer for got and plt entries
       XXX: calculate exact size for them? */
    offset *= 2;
#endif

    if (0 == mem)
        return offset + 15;

    /* relocate each section */
    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (s->reloc)
            relocate_section(s1, s);
    }

    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (0 == (s->sh_flags & SHF_ALLOC))
            continue;
        length = s->data_offset;
        // printf("%-12s %08x %04x\n", s->name, s->sh_addr, length);
        ptr = (void *) s->sh_addr;
        if (NULL == s->data || s->sh_type == SHT_NOBITS)
            memset(ptr, 0, length);
        else
            memcpy(ptr, s->data, length);
        /* mark executable sections as executable in memory */
        if (s->sh_flags & SHF_EXECINSTR)
            set_pages_executable(ptr, length);
    }
#ifdef TCC_TARGET_X86_64
    set_pages_executable(s1->runtime_plt_and_got, s1->runtime_plt_and_got_offset);
#endif
    return 0;
}

/* launch the compiled program with the given arguments */
int tcc_run(TCCState *s1, int argc, char **argv)
{
    int (*prog_main)(int, char **);
    void *ptr;
    int ret;

    ret = tcc_relocate(s1, NULL);
    if (ret < 0)
        return -1;
    ptr = tcc_malloc(ret);
    tcc_relocate(s1, ptr);

    prog_main = tcc_get_symbol_err(s1, "main");

    if (do_debug) {
#ifdef CONFIG_TCC_BACKTRACE
        struct sigaction sigact;
        /* install TCC signal handlers to print debug info on fatal
           runtime errors */
        sigact.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigact.sa_sigaction = sig_error;
        sigemptyset(&sigact.sa_mask);
        sigaction(SIGFPE, &sigact, NULL);
        sigaction(SIGILL, &sigact, NULL);
        sigaction(SIGSEGV, &sigact, NULL);
        sigaction(SIGBUS, &sigact, NULL);
        sigaction(SIGABRT, &sigact, NULL);
#else
        error("debug mode not available");
#endif
    }

#ifdef CONFIG_TCC_BCHECK
    if (do_bounds_check) {
        void (*bound_init)(void);

        /* set error function */
        rt_bound_error_msg = tcc_get_symbol_err(s1, "__bound_error_msg");

        /* XXX: use .init section so that it also work in binary ? */
        bound_init = (void *) tcc_get_symbol_err(s1, "__bound_init");
        bound_init();
    }
#endif
    ret = (*prog_main)(argc, argv);
    tcc_free(ptr);
    return ret;
}
#endif

void tcc_memstats(void)
{
#ifdef MEM_DEBUG
    printf("memory in use: %d\n", mem_cur_size);
#endif
}

static void tcc_cleanup(void)
{
    int i, n;

    if (NULL == tcc_state)
        return;
    tcc_state = NULL;

    /* free -D defines */
    free_defines(NULL);

    /* free tokens */
    n = tok_ident - TOK_IDENT;
    for (i = 0; i < n; i++)
        tcc_free(table_ident[i]);
    tcc_free(table_ident);

    /* free sym_pools */
    dynarray_reset(&sym_pools, &nb_sym_pools);
    /* string buffer */
    cstr_free(&tokcstr);
    /* reset symbol stack */
    sym_free_first = NULL;
    /* cleanup from error/setjmp */
    macro_ptr = NULL;
}

TCCState *tcc_new(void)
{
    const char *p, *r;
    TCCState *s;
    TokenSym *ts;
    int i, c;

    tcc_cleanup();

    s = tcc_mallocz(sizeof(TCCState));
    if (!s)
        return NULL;
    tcc_state = s;
    s->output_type = TCC_OUTPUT_MEMORY;

    /* init isid table */
    for (i = CH_EOF; i < 256; i++)
        isidnum_table[i - CH_EOF] = isid(i) || isnum(i);

    /* add all tokens */
    table_ident = NULL;
    memset(hash_ident, 0, TOK_HASH_SIZE * sizeof(TokenSym *));

    tok_ident = TOK_IDENT;
    p = tcc_keywords;
    while (*p) {
        r = p;
        for (;;) {
            c = *r++;
            if (c == '\0')
                break;
        }
        ts = tok_alloc(p, r - p - 1);
        p = r;
    }

    /* we add dummy defines for some special macros to speed up tests
       and to have working defined() */
    define_push(TOK___LINE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___FILE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___DATE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___TIME__, MACRO_OBJ, NULL, NULL);

    /* standard defines */
    tcc_define_symbol(s, "__STDC__", NULL);
    tcc_define_symbol(s, "__STDC_VERSION__", "199901L");
#if defined(TCC_TARGET_I386)
    tcc_define_symbol(s, "__i386__", NULL);
#endif
#if defined(TCC_TARGET_X86_64)
    tcc_define_symbol(s, "__x86_64__", NULL);
#endif
#if defined(TCC_TARGET_ARM)
    tcc_define_symbol(s, "__ARM_ARCH_4__", NULL);
    tcc_define_symbol(s, "__arm_elf__", NULL);
    tcc_define_symbol(s, "__arm_elf", NULL);
    tcc_define_symbol(s, "arm_elf", NULL);
    tcc_define_symbol(s, "__arm__", NULL);
    tcc_define_symbol(s, "__arm", NULL);
    tcc_define_symbol(s, "arm", NULL);
    tcc_define_symbol(s, "__APCS_32__", NULL);
#endif
#ifdef TCC_TARGET_PE
    tcc_define_symbol(s, "_WIN32", NULL);
#elif defined(TCC_TARGET_816)
    tcc_define_symbol(s, "__65816__", NULL);
#else
    tcc_define_symbol(s, "__unix__", NULL);
    tcc_define_symbol(s, "__unix", NULL);
#if defined(__linux)
    tcc_define_symbol(s, "__linux__", NULL);
    tcc_define_symbol(s, "__linux", NULL);
#endif
#endif
    /* tiny C specific defines */
    tcc_define_symbol(s, "__TINYC__", NULL);

    /* tiny C & gcc defines */
    tcc_define_symbol(s, "__SIZE_TYPE__", "unsigned int");
    tcc_define_symbol(s, "__PTRDIFF_TYPE__", "int");
#ifdef TCC_TARGET_PE
    tcc_define_symbol(s, "__WCHAR_TYPE__", "unsigned short");
#else
    tcc_define_symbol(s, "__WCHAR_TYPE__", "int");
#endif
#ifdef TCC_TARGET_816
    tcc_define_symbol(s, "__INT_MAX__", "32767");
    tcc_define_symbol(s, "__LONG_MAX__", "32767");
    tcc_define_symbol(s, "__LONG_LONG_MAX__", "2147483647LL");
    tcc_define_symbol(s, "__FLT_MIN__", "0x0.1p-127");
    tcc_define_symbol(s, "__FLT_MAX__", "0x1.fffffep127");
    tcc_define_symbol(s, "__DBL_MIN__", "0x0.1p-127");
    tcc_define_symbol(s, "__DBL_MAX__", "0x1.fffffep127");
    tcc_define_symbol(s, "__LDBL_MIN__", "0x0.1p-127");
    tcc_define_symbol(s, "__LDBL_MAX__", "0x1.fffffep127");
#endif

#ifndef TCC_TARGET_PE
    /* default library paths */
    tcc_add_library_path(s, CONFIG_SYSROOT "/usr/local/lib");
    tcc_add_library_path(s, CONFIG_SYSROOT "/usr/lib");
    tcc_add_library_path(s, CONFIG_SYSROOT "/lib");
#endif

    /* no section zero */
    dynarray_add((void ***) &s->sections, &s->nb_sections, NULL);

    /* create standard sections */
    text_section = new_section(s, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    data_section = new_section(s, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
#ifdef TCC_TARGET_816
    rodata_section = new_section(s, ".rodata", SHT_PROGBITS, SHF_ALLOC);
#endif
    bss_section = new_section(s, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);

    /* symbols are always generated for linking stage */
    symtab_section = new_symtab(s, ".symtab", SHT_SYMTAB, 0, ".strtab", ".hashtab", SHF_PRIVATE);
    strtab_section = symtab_section->link;

    /* private symbol table for dynamic symbols */
    s->dynsymtab_section = new_symtab(s,
                                      ".dynsymtab",
                                      SHT_SYMTAB,
                                      SHF_PRIVATE,
                                      ".dynstrtab",
                                      ".dynhashtab",
                                      SHF_PRIVATE);
    s->alacarte_link = 1;

#ifdef CHAR_IS_UNSIGNED
    s->char_is_unsigned = 1;
#endif
#if defined(TCC_TARGET_PE) && 0
    /* XXX: currently the PE linker is not ready to support that */
    s->leading_underscore = 1;
#endif
    return s;
}

void tcc_delete(TCCState *s1)
{
    int i;

    tcc_cleanup();

    /* free all sections */
    for (i = 1; i < s1->nb_sections; i++)
        free_section(s1->sections[i]);
    dynarray_reset(&s1->sections, &s1->nb_sections);

    for (i = 0; i < s1->nb_priv_sections; i++)
        free_section(s1->priv_sections[i]);
    dynarray_reset(&s1->priv_sections, &s1->nb_priv_sections);

    /* free any loaded DLLs */
    for (i = 0; i < s1->nb_loaded_dlls; i++) {
        DLLReference *ref = s1->loaded_dlls[i];
        if (ref->handle)
            dlclose(ref->handle);
    }

    /* free loaded dlls array */
    dynarray_reset(&s1->loaded_dlls, &s1->nb_loaded_dlls);

    /* free library paths */
    dynarray_reset(&s1->library_paths, &s1->nb_library_paths);

    /* free include paths */
    dynarray_reset(&s1->cached_includes, &s1->nb_cached_includes);
    dynarray_reset(&s1->include_paths, &s1->nb_include_paths);
    dynarray_reset(&s1->sysinclude_paths, &s1->nb_sysinclude_paths);

    tcc_free(s1);
}

int tcc_add_include_path(TCCState *s1, const char *pathname)
{
    char *pathname1;

    pathname1 = tcc_strdup(pathname);
    dynarray_add((void ***) &s1->include_paths, &s1->nb_include_paths, pathname1);
    return 0;
}

int tcc_add_sysinclude_path(TCCState *s1, const char *pathname)
{
    char *pathname1;

    pathname1 = tcc_strdup(pathname);
    dynarray_add((void ***) &s1->sysinclude_paths, &s1->nb_sysinclude_paths, pathname1);
    return 0;
}

static int tcc_add_file_internal(TCCState *s1, const char *filename, int flags)
{
    const char *ext;
#ifdef TCC_TARGET_816
    int ret;
#else
    ElfW(Ehdr) ehdr;
    int fd, ret;
#endif
    BufferedFile *saved_file;

    /* find source file type with extension */
    ext = tcc_fileextension(filename);
    if (ext[0])
        ext++;

    /* open the file */
    saved_file = file;
    file = tcc_open(s1, filename);
    if (!file) {
        if (flags & AFF_PRINT_ERROR) {
            error_noabort("file '%s' not found", filename);
        }
        ret = -1;
        goto fail1;
    }

    if (flags & AFF_PREPROCESS) {
        ret = tcc_preprocess(s1);
    } else if (!ext[0] || !PATHCMP(ext, "c")) {
        /* C file assumed */
        ret = tcc_compile(s1);
    } else
#ifdef CONFIG_TCC_ASM
        if (!strcmp(ext, "S")) {
        /* preprocessed assembler */
        ret = tcc_assemble(s1, 1);
    } else if (!strcmp(ext, "s")) {
        /* non preprocessed assembler */
        ret = tcc_assemble(s1, 0);
    } else
#endif
#ifdef TCC_TARGET_PE
        if (!PATHCMP(ext, "def")) {
        ret = pe_load_def_file(s1, file->fd);
    } else
#endif
    {
#ifdef TCC_TARGET_816
        error_noabort("unrecognized file type");
        goto fail;
#else
        fd = file->fd;
        /* assume executable format: auto guess file type */
        ret = read(fd, &ehdr, sizeof(ehdr));
        lseek(fd, 0, SEEK_SET);
        if (ret <= 0) {
            error_noabort("could not read header");
            goto fail;
        } else if (ret != sizeof(ehdr)) {
            goto try_load_script;
        }

        if (ehdr.e_ident[0] == ELFMAG0 && ehdr.e_ident[1] == ELFMAG1 && ehdr.e_ident[2] == ELFMAG2
            && ehdr.e_ident[3] == ELFMAG3) {
            file->line_num = 0; /* do not display line number if error */
            if (ehdr.e_type == ET_REL) {
                ret = tcc_load_object_file(s1, fd, 0);
            } else if (ehdr.e_type == ET_DYN) {
                if (s1->output_type == TCC_OUTPUT_MEMORY) {
#ifdef TCC_TARGET_PE
                    ret = -1;
#else
                    void *h;
                    h = dlopen(filename, RTLD_GLOBAL | RTLD_LAZY);
                    if (h)
                        ret = 0;
                    else
                        ret = -1;
#endif
                } else {
                    ret = tcc_load_dll(s1, fd, filename, (flags & AFF_REFERENCED_DLL) != 0);
                }
            } else {
                error_noabort("unrecognized ELF file");
                goto fail;
            }
        } else if (memcmp((char *) &ehdr, ARMAG, 8) == 0) {
            file->line_num = 0; /* do not display line number if error */
            ret = tcc_load_archive(s1, fd);
        } else
#ifdef TCC_TARGET_COFF
            if (*(uint16_t *) (&ehdr) == COFF_C67_MAGIC) {
            ret = tcc_load_coff(s1, fd);
        } else
#endif
#ifdef TCC_TARGET_PE
            if (pe_test_res_file(&ehdr, ret)) {
            ret = pe_load_res_file(s1, fd);
        } else
#endif
        {
            /* as GNU ld, consider it is an ld script if not recognized */
        try_load_script:
            ret = tcc_load_ldscript(s1);
            if (ret < 0) {
                error_noabort("unrecognized file type");
                goto fail;
            }
        }
#endif
    }
the_end:
    tcc_close(file);
fail1:
    file = saved_file;
    return ret;
fail:
    ret = -1;
    goto the_end;
}

int tcc_add_file(TCCState *s, const char *filename)
{
    return tcc_add_file_internal(s, filename, AFF_PRINT_ERROR);
}

int tcc_add_library_path(TCCState *s, const char *pathname)
{
    char *pathname1;

    pathname1 = tcc_strdup(pathname);
    dynarray_add((void ***) &s->library_paths, &s->nb_library_paths, pathname1);
    return 0;
}

/* find and load a dll. Return non zero if not found */
/* XXX: add '-rpath' option support ? */
static int tcc_add_dll(TCCState *s, const char *filename, int flags)
{
    char buf[1024];
    int i;

    for (i = 0; i < s->nb_library_paths; i++) {
        snprintf(buf, sizeof(buf), "%s/%s", s->library_paths[i], filename);
        if (tcc_add_file_internal(s, buf, flags) == 0)
            return 0;
    }
    return -1;
}

/* the library name is the same as the argument of the '-l' option */
int tcc_add_library(TCCState *s, const char *libraryname)
{
    char buf[1024];
    int i;

    /* first we look for the dynamic library if not static linking */
    if (!s->static_link) {
#ifdef TCC_TARGET_PE
        snprintf(buf, sizeof(buf), "%s.def", libraryname);
#else
        snprintf(buf, sizeof(buf), "lib%s.so", libraryname);
#endif
        if (tcc_add_dll(s, buf, 0) == 0)
            return 0;
    }

    /* then we look for the static library */
    for (i = 0; i < s->nb_library_paths; i++) {
        snprintf(buf, sizeof(buf), "%s/lib%s.a", s->library_paths[i], libraryname);
        if (tcc_add_file_internal(s, buf, 0) == 0)
            return 0;
    }
    return -1;
}

int tcc_add_symbol(TCCState *s, const char *name, void *val)
{
    add_elf_sym(symtab_section,
                (unsigned long) val,
                0,
                ELFW(ST_INFO)(STB_GLOBAL, STT_NOTYPE),
                0,
                SHN_ABS,
                name);
    return 0;
}

int tcc_set_output_type(TCCState *s, int output_type)
{
    char buf[1024];

    s->output_type = output_type;

    if (!s->nostdinc) {
        /* default include paths */
        /* XXX: reverse order needed if -isystem support */
#if !defined(TCC_TARGET_PE) && !defined(TCC_TARGET_816)
        tcc_add_sysinclude_path(s, CONFIG_SYSROOT "/usr/local/include");
        tcc_add_sysinclude_path(s, CONFIG_SYSROOT "/usr/include");
#endif
        snprintf(buf, sizeof(buf), "%s/include", tcc_lib_path);
        tcc_add_sysinclude_path(s, buf);
#ifdef TCC_TARGET_PE
        snprintf(buf, sizeof(buf), "%s/include/winapi", tcc_lib_path);
        tcc_add_sysinclude_path(s, buf);
#endif
    }

    /* if bound checking, then add corresponding sections */
#ifdef CONFIG_TCC_BCHECK
    if (do_bounds_check) {
        /* define symbol */
        tcc_define_symbol(s, "__BOUNDS_CHECKING_ON", NULL);
        /* create bounds sections */
        bounds_section = new_section(s, ".bounds", SHT_PROGBITS, SHF_ALLOC);
        lbounds_section = new_section(s, ".lbounds", SHT_PROGBITS, SHF_ALLOC);
    }
#endif

    if (s->char_is_unsigned) {
        tcc_define_symbol(s, "__CHAR_UNSIGNED__", NULL);
    }

    /* add debug sections */
    if (do_debug) {
        /* stab symbols */
        stab_section = new_section(s, ".stab", SHT_PROGBITS, 0);
        stab_section->sh_entsize = sizeof(Stab_Sym);
        stabstr_section = new_section(s, ".stabstr", SHT_STRTAB, 0);
        put_elf_str(stabstr_section, "");
        stab_section->link = stabstr_section;
        /* put first entry */
        put_stabs("", 0, 0, 0, 0);
    }

    /* add libc crt1/crti objects */
#ifndef TCC_TARGET_PE
    if ((output_type == TCC_OUTPUT_EXE || output_type == TCC_OUTPUT_DLL) && !s->nostdlib) {
        if (output_type != TCC_OUTPUT_DLL)
            tcc_add_file(s, CONFIG_TCC_CRT_PREFIX "/crt1.o");
        tcc_add_file(s, CONFIG_TCC_CRT_PREFIX "/crti.o");
    }
#endif

#ifdef TCC_TARGET_PE
    snprintf(buf, sizeof(buf), "%s/lib", tcc_lib_path);
    tcc_add_library_path(s, buf);
#endif

    return 0;
}

#define WD_ALL 0x0001    /* warning is activated when using -Wall */
#define FD_INVERT 0x0002 /* invert value before storing */

typedef struct FlagDef
{
    uint16_t offset;
    uint16_t flags;
    const char *name;
} FlagDef;

static const FlagDef warning_defs[] = {
    {offsetof(TCCState, warn_unsupported), 0, "unsupported"},
    {offsetof(TCCState, warn_write_strings), 0, "write-strings"},
    {offsetof(TCCState, warn_error), 0, "error"},
    {offsetof(TCCState, warn_implicit_function_declaration),
     WD_ALL,
     "implicit-function-declaration"},
};

static int set_flag(TCCState *s, const FlagDef *flags, int nb_flags, const char *name, int value)
{
    int i;
    const FlagDef *p;
    const char *r;

    r = name;
    if (r[0] == 'n' && r[1] == 'o' && r[2] == '-') {
        r += 3;
        value = !value;
    }
    for (i = 0, p = flags; i < nb_flags; i++, p++) {
        if (!strcmp(r, p->name))
            goto found;
    }
    return -1;
found:
    if (p->flags & FD_INVERT)
        value = !value;
    *(int *) ((uint8_t *) s + p->offset) = value;
    return 0;
}

/* set/reset a warning */
int tcc_set_warning(TCCState *s, const char *warning_name, int value)
{
    int i;
    const FlagDef *p;

    if (!strcmp(warning_name, "all")) {
        for (i = 0, p = warning_defs; i < countof(warning_defs); i++, p++) {
            if (p->flags & WD_ALL)
                *(int *) ((uint8_t *) s + p->offset) = 1;
        }
        return 0;
    } else {
        return set_flag(s, warning_defs, countof(warning_defs), warning_name, value);
    }
}

static const FlagDef flag_defs[] = {
    {offsetof(TCCState, char_is_unsigned), 0, "unsigned-char"},
    {offsetof(TCCState, char_is_unsigned), FD_INVERT, "signed-char"},
    {offsetof(TCCState, nocommon), FD_INVERT, "common"},
    {offsetof(TCCState, leading_underscore), 0, "leading-underscore"},
};

/* set/reset a flag */
int tcc_set_flag(TCCState *s, const char *flag_name, int value)
{
    return set_flag(s, flag_defs, countof(flag_defs), flag_name, value);
}

/* set CONFIG_TCCDIR at runtime */
void tcc_set_lib_path(TCCState *s, const char *path)
{
    tcc_lib_path = tcc_strdup(path);
}

#if !defined(LIBTCC)

static int64_t getclock_us(void)
{
#ifdef _WIN32
    struct _timeb tb;
    _ftime(&tb);
    return (tb.time * 1000LL + tb.millitm) * 1000LL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
}

void help(void)
{
    printf("tcc version " TCC_VERSION
           " - Tiny C Compiler - Copyright (C) 2001-2006 Fabrice Bellard\n"
#ifdef TCC_TARGET_816
           "Modified for PVSneslib by Alekmaul in 2021\n"
#endif
           "usage: tcc [-v] [-c] [-o outfile] [-Bdir] [-bench] [-Idir] [-Dsym[=val]] [-Usym]\n"
           "           [-Wwarn] [-g] [-b] [-bt N] [-Ldir] [-llib] [-shared] [-soname name]\n"
           "           [-static] [infile1 infile2...] [-run infile args...]\n"
           "\n"
           "General options:\n"
           "  -v          display current version, increase verbosity\n"
           "  -c          compile only - generate an object file\n"
           "  -o outfile  set output filename\n"
           "  -Bdir       set tcc internal library path\n"
           "  -bench      output compilation statistics\n"
           "  -run        run compiled source\n"
           "  -fflag      set or reset (with 'no-' prefix) 'flag' (see man page)\n"
           "  -Wwarning   set or reset (with 'no-' prefix) 'warning' (see man page)\n"
           "  -w          disable all warnings\n"
           "Preprocessor options:\n"
           "  -E          preprocess only\n"
           "  -Idir       add include path 'dir'\n"
           "  -Dsym[=val] define 'sym' with value 'val'\n"
           "  -Usym       undefine 'sym'\n"
           "Linker options:\n"
           "  -Ldir       add library path 'dir'\n"
           "  -llib       link with dynamic or static library 'lib'\n"
           "  -shared     generate a shared library\n"
           "  -soname     set name for shared library to be used at runtime\n"
           "  -static     static linking\n"
           "  -rdynamic   export all global symbols to dynamic linker\n"
           "  -r          generate (relocatable) object file\n"
           "Debugger options:\n"
           "  -g          generate runtime debug info\n"
#ifdef CONFIG_TCC_BCHECK
           "  -b          compile with built-in memory and bounds checker (implies -g)\n"
#endif
           "  -bt N       show N callers in stack traces\n");
}

#define TCC_OPTION_HAS_ARG 0x0001
#define TCC_OPTION_NOSEP 0x0002 /* cannot have space before option and arg */

typedef struct TCCOption
{
    const char *name;
    uint16_t index;
    uint16_t flags;
} TCCOption;

enum {
    TCC_OPTION_HELP,
    TCC_OPTION_I,
    TCC_OPTION_D,
    TCC_OPTION_U,
    TCC_OPTION_L,
    TCC_OPTION_B,
    TCC_OPTION_l,
    TCC_OPTION_bench,
    TCC_OPTION_bt,
    TCC_OPTION_b,
    TCC_OPTION_g,
    TCC_OPTION_c,
    TCC_OPTION_static,
    TCC_OPTION_shared,
    TCC_OPTION_soname,
    TCC_OPTION_o,
    TCC_OPTION_r,
    TCC_OPTION_Wl,
    TCC_OPTION_W,
    TCC_OPTION_O,
    TCC_OPTION_m,
    TCC_OPTION_f,
    TCC_OPTION_nostdinc,
    TCC_OPTION_nostdlib,
    TCC_OPTION_print_search_dirs,
    TCC_OPTION_rdynamic,
    TCC_OPTION_run,
    TCC_OPTION_v,
    TCC_OPTION_w,
    TCC_OPTION_pipe,
    TCC_OPTION_E,
};

static const TCCOption tcc_options[] = {
    {"h", TCC_OPTION_HELP, 0},
    {"?", TCC_OPTION_HELP, 0},
    {"I", TCC_OPTION_I, TCC_OPTION_HAS_ARG},
    {"D", TCC_OPTION_D, TCC_OPTION_HAS_ARG},
    {"U", TCC_OPTION_U, TCC_OPTION_HAS_ARG},
    {"L", TCC_OPTION_L, TCC_OPTION_HAS_ARG},
    {"B", TCC_OPTION_B, TCC_OPTION_HAS_ARG},
    {"l", TCC_OPTION_l, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"bench", TCC_OPTION_bench, 0},
    {"bt", TCC_OPTION_bt, TCC_OPTION_HAS_ARG},
#ifdef CONFIG_TCC_BCHECK
    {"b", TCC_OPTION_b, 0},
#endif
    {"g", TCC_OPTION_g, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"c", TCC_OPTION_c, 0},
    {"static", TCC_OPTION_static, 0},
    {"shared", TCC_OPTION_shared, 0},
    {"soname", TCC_OPTION_soname, TCC_OPTION_HAS_ARG},
    {"o", TCC_OPTION_o, TCC_OPTION_HAS_ARG},
    {"run", TCC_OPTION_run, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"rdynamic", TCC_OPTION_rdynamic, 0},
    {"r", TCC_OPTION_r, 0},
    {"Wl,", TCC_OPTION_Wl, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"W", TCC_OPTION_W, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"O", TCC_OPTION_O, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"m", TCC_OPTION_m, TCC_OPTION_HAS_ARG},
    {"f", TCC_OPTION_f, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"nostdinc", TCC_OPTION_nostdinc, 0},
    {"nostdlib", TCC_OPTION_nostdlib, 0},
    {"print-search-dirs", TCC_OPTION_print_search_dirs, 0},
    {"v", TCC_OPTION_v, TCC_OPTION_HAS_ARG | TCC_OPTION_NOSEP},
    {"w", TCC_OPTION_w, 0},
    {"pipe", TCC_OPTION_pipe, 0},
    {"E", TCC_OPTION_E, 0},
    {NULL},
};

/* convert 'str' into an array of space separated strings */
static int expand_args(char ***pargv, const char *str)
{
    const char *s1;
    char **argv, *arg;
    int argc, len;

    argc = 0;
    argv = NULL;
    for (;;) {
        while (is_space(*str))
            str++;
        if (*str == '\0')
            break;
        s1 = str;
        while (*str != '\0' && !is_space(*str))
            str++;
        len = str - s1;
        arg = tcc_malloc(len + 1);
        memcpy(arg, s1, len);
        arg[len] = '\0';
        dynarray_add((void ***) &argv, &argc, arg);
    }
    *pargv = argv;
    return argc;
}

static char **files;
static int nb_files, nb_libraries;
static int multiple_files;
static int print_search_dirs;
static int output_type;
static int reloc_output;
static const char *outfile;

int parse_args(TCCState *s, int argc, char **argv)
{
    int optind;
    const TCCOption *popt;
    const char *optarg, *p1, *r1;
    char *r;

#ifdef TCC_TARGET_816
    s->output_format = TCC_OUTPUT_FORMAT_BINARY;
#endif

    optind = 0;
    while (optind < argc) {
        r = argv[optind++];
        if (r[0] != '-' || r[1] == '\0') {
            /* add a new file */
            dynarray_add((void ***) &files, &nb_files, r);
            if (!multiple_files) {
                optind--;
                /* argv[0] will be this file */
                break;
            }
        } else {
            /* find option in table (match only the first chars */
            popt = tcc_options;
            for (;;) {
                p1 = popt->name;
                if (p1 == NULL)
                    error("invalid option -- '%s'", r);
                r1 = r + 1;
                for (;;) {
                    if (*p1 == '\0')
                        goto option_found;
                    if (*r1 != *p1)
                        break;
                    p1++;
                    r1++;
                }
                popt++;
            }
        option_found:
            if (popt->flags & TCC_OPTION_HAS_ARG) {
                if (*r1 != '\0' || (popt->flags & TCC_OPTION_NOSEP)) {
                    optarg = r1;
                } else {
                    if (optind >= argc)
                        error("argument to '%s' is missing", r);
                    optarg = argv[optind++];
                }
            } else {
                if (*r1 != '\0')
                    return 0;
                optarg = NULL;
            }

            switch (popt->index) {
            case TCC_OPTION_HELP:
                return 0;

            case TCC_OPTION_I:
                if (tcc_add_include_path(s, optarg) < 0)
                    error("too many include paths");
                break;
            case TCC_OPTION_D: {
                char *sym, *value;
                sym = (char *) optarg;
                value = strchr(sym, '=');
                if (value) {
                    *value = '\0';
                    value++;
                }
                tcc_define_symbol(s, sym, value);
            } break;
            case TCC_OPTION_U:
                tcc_undefine_symbol(s, optarg);
                break;
            case TCC_OPTION_L:
                tcc_add_library_path(s, optarg);
                break;
            case TCC_OPTION_B:
                /* set tcc utilities path (mainly for tcc development) */
                tcc_set_lib_path(s, optarg);
                break;
            case TCC_OPTION_l:
                dynarray_add((void ***) &files, &nb_files, r);
                nb_libraries++;
                break;
            case TCC_OPTION_bench:
                do_bench = 1;
                break;
#ifdef CONFIG_TCC_BACKTRACE
            case TCC_OPTION_bt:
                num_callers = atoi(optarg);
                break;
#endif
#ifdef CONFIG_TCC_BCHECK
            case TCC_OPTION_b:
                do_bounds_check = 1;
                do_debug = 1;
                break;
#endif
            case TCC_OPTION_g:
                do_debug = 1;
                break;
            case TCC_OPTION_c:
                multiple_files = 1;
                output_type = TCC_OUTPUT_OBJ;
                break;
            case TCC_OPTION_static:
                s->static_link = 1;
                break;
            case TCC_OPTION_shared:
                output_type = TCC_OUTPUT_DLL;
                break;
            case TCC_OPTION_soname:
                s->soname = optarg;
                break;
            case TCC_OPTION_o:
                multiple_files = 1;
                outfile = optarg;
                break;
            case TCC_OPTION_r:
                /* generate a .o merging several output files */
                reloc_output = 1;
                output_type = TCC_OUTPUT_OBJ;
                break;
            case TCC_OPTION_nostdinc:
                s->nostdinc = 1;
                break;
            case TCC_OPTION_nostdlib:
                s->nostdlib = 1;
                break;
            case TCC_OPTION_print_search_dirs:
                print_search_dirs = 1;
                break;
            case TCC_OPTION_run: {
                int argc1;
                char **argv1;
                argc1 = expand_args(&argv1, optarg);
                if (argc1 > 0) {
                    parse_args(s, argc1, argv1);
                }
                multiple_files = 0;
                output_type = TCC_OUTPUT_MEMORY;
            } break;
            case TCC_OPTION_v:
                do {
                    if (0 == verbose++)
                        printf("tcc version %s\n", TCC_VERSION);
                } while (*optarg++ == 'v');
                break;
            case TCC_OPTION_f:
                if (tcc_set_flag(s, optarg, 1) < 0 && s->warn_unsupported)
                    goto unsupported_option;
                break;
            case TCC_OPTION_W:
                if (tcc_set_warning(s, optarg, 1) < 0 && s->warn_unsupported)
                    goto unsupported_option;
                break;
            case TCC_OPTION_w:
                s->warn_none = 1;
                break;
            case TCC_OPTION_rdynamic:
                s->rdynamic = 1;
                break;
            case TCC_OPTION_Wl: {
                const char *p;
                if (strstart(optarg, "-Ttext,", &p)) {
                    s->text_addr = strtoul(p, NULL, 16);
                    s->has_text_addr = 1;
                } else if (strstart(optarg, "--oformat,", &p)) {
                    if (strstart(p, "elf32-", NULL)) {
                        s->output_format = TCC_OUTPUT_FORMAT_ELF;
                    } else if (!strcmp(p, "binary")) {
                        s->output_format = TCC_OUTPUT_FORMAT_BINARY;
                    } else
#ifdef TCC_TARGET_COFF
                        if (!strcmp(p, "coff")) {
                        s->output_format = TCC_OUTPUT_FORMAT_COFF;
                    } else
#endif
                    {
                        error("target %s not found", p);
                    }
                } else {
                    error("unsupported linker option '%s'", optarg);
                }
            } break;
            case TCC_OPTION_E:
                output_type = TCC_OUTPUT_PREPROCESS;
                break;
            default:
                if (s->warn_unsupported) {
                unsupported_option:
                    warning("unsupported option '%s'", r);
                }
                break;
            }
        }
    }
    return optind + 1;
}

int main(int argc, char **argv)
{
    int i;
    TCCState *s;
    int nb_objfiles, ret, optind;
    char objfilename[1024];
    int64_t start_time = 0;

    s = tcc_new();

#ifdef TCC_TARGET_816
    strcpy(sztmpnam, &tmpnam(NULL)[1]);   // Alekmaul 201125, create temp file name for token name
    for (i = 0; sztmpnam[i] != '\0'; i++) // Alekmaul 201212, ughly change for linux system
    {
        if (sztmpnam[i] == '/') {
            sztmpnam[i] = 'x';
        }
        if (sztmpnam[i] == '.') {
            sztmpnam[i] = 'x';
        }
    }
#endif
#ifdef _WIN32
    tcc_set_lib_path_w32(s);
#endif
    output_type = TCC_OUTPUT_EXE;
    outfile = NULL;
    multiple_files = 1;
    files = NULL;
    nb_files = 0;
    nb_libraries = 0;
    reloc_output = 0;
    print_search_dirs = 0;
    ret = 0;

    optind = parse_args(s, argc - 1, argv + 1);
    if (print_search_dirs) {
        /* enough for Linux kernel */
        printf("install: %s/\n", tcc_lib_path);
        return 0;
    }
    if (optind == 0 || nb_files == 0) {
        if (optind && verbose)
            return 0;
        help();
        return 1;
    }

    nb_objfiles = nb_files - nb_libraries;

    /* if outfile provided without other options, we output an
       executable */
    if (outfile && output_type == TCC_OUTPUT_MEMORY)
        output_type = TCC_OUTPUT_EXE;

    /* check -c consistency : only single file handled. XXX: checks file type */
    if (output_type == TCC_OUTPUT_OBJ && !reloc_output) {
        /* accepts only a single input file */
        if (nb_objfiles != 1)
            error("cannot specify multiple files with -c");
        if (nb_libraries != 0)
            error("cannot specify libraries with -c");
    }

    if (output_type == TCC_OUTPUT_PREPROCESS) {
        if (!outfile) {
            s->outfile = stdout;
        } else {
            s->outfile = fopen(outfile, "w");
            if (!s->outfile)
                error("could not open '%s", outfile);
        }
    } else if (output_type != TCC_OUTPUT_MEMORY) {
        if (!outfile) {
            /* compute default outfile name */
            char *ext;
            const char *name = strcmp(files[0], "-") == 0 ? "a" : tcc_basename(files[0]);
            pstrcpy(objfilename, sizeof(objfilename), name);
            ext = tcc_fileextension(objfilename);
#ifdef TCC_TARGET_PE
            if (output_type == TCC_OUTPUT_DLL)
                strcpy(ext, ".dll");
            else if (output_type == TCC_OUTPUT_EXE)
                strcpy(ext, ".exe");
            else
#endif
                if (output_type == TCC_OUTPUT_OBJ && !reloc_output && *ext)
                strcpy(ext, ".o");
            else
                pstrcpy(objfilename, sizeof(objfilename), "a.out");
            outfile = objfilename;
        }
    }

    if (do_bench) {
        start_time = getclock_us();
    }

    tcc_set_output_type(s, output_type);

    /* compile or add each files or library */
    for (i = 0; i < nb_files && ret == 0; i++) {
        const char *filename;

        filename = files[i];
        if (output_type == TCC_OUTPUT_PREPROCESS) {
            if (tcc_add_file_internal(s, filename, AFF_PRINT_ERROR | AFF_PREPROCESS) < 0)
                ret = 1;
        } else if (filename[0] == '-' && filename[1]) {
            if (tcc_add_library(s, filename + 2) < 0)
                error("cannot find %s", filename);
        } else {
            if (1 == verbose)
                printf("-> %s\n", filename);
            if (tcc_add_file(s, filename) < 0)
                ret = 1;
        }
    }

    /* free all files */
    tcc_free(files);

    if (ret)
        goto the_end;

    if (do_bench) {
        double total_time;
        total_time = (double) (getclock_us() - start_time) / 1000000.0;
        if (total_time < 0.001)
            total_time = 0.001;
        if (total_bytes < 1)
            total_bytes = 1;
        printf("%d idents, %d lines, %d bytes, %0.3f s, %d lines/s, %0.1f MB/s\n",
               tok_ident - TOK_IDENT,
               total_lines,
               total_bytes,
               total_time,
               (int) (total_lines / total_time),
               total_bytes / total_time / 1000000.0);
    }

#ifndef TCC_TARGET_816
    if (s->output_type == TCC_OUTPUT_PREPROCESS) {
        if (outfile)
            fclose(s->outfile);
    } else if (s->output_type == TCC_OUTPUT_MEMORY) {
        ret = tcc_run(s, argc - optind, argv + optind);
    } else
#endif
        ret = tcc_output_file(s, outfile) ? 1 : 0;
the_end:
    /* XXX: cannot do it with bound checking because of the malloc hooks */
    if (!do_bounds_check)
        tcc_delete(s);

#ifdef MEM_DEBUG
    if (do_bench) {
        printf("memory: %d bytes, max = %d bytes\n", mem_cur_size, mem_max_size);
    }
#endif
    return ret;
}

#endif
