#ifdef __ETS__
#include "stdhdr.h"
#define printf        os_printf
#define sprintf       os_sprintf
#define malloc        os_zalloc
#define calloc(n, m)  os_zalloc((n)*(m))
#define free          os_free
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "stdintw.h"
#define ICACHE_FLASH_ATTR
#endif
#include <limits.h>
#include "urubasic.h"
#include "smemblk.h"

#ifdef _MSC_VER
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

enum Sizes {
    MAX_LINE_LEN            = 128,
    PRINT_ZONE_LEN          = 15,
    MAX_PRINT_ZONES         = 5,
    MAX_SYMBOLS             = 128,
    FOR_LOOP_DEPTH          = 10,
    EXPR_STACK_SIZE         = 10,
    MAX_LOOKAHEAD           = 7,
    MAX_FUNCTION_ARGS       = 4,
    HASHSIZE                = 57,
};

enum Token {
    PRINT = 1, GOTO, END, FOR, TO, NEXT, REM, GOSUB, RETURN, LET, IF, THEN, STOP, STEP, DEF, TAB, ON, READ, RESTORE, DATA,
    OPTION, BASE, DIM,

    NUM_KEYWORDS,
    NUMBER = MAX_SYMBOLS, NEWLINE, STRING, IDENTIFIER, LT, LE, GE, GT, LSH, RSH, NEQ, EQ, COMMA, SEMICOLON, LPAREN, RPAREN, CIRCUMFLEX,
    PLUS, MINUS, MULT, SOLIDUS, FUNCTION, AND, OR, NOT, COLON,

    ALLOC = 0x4000, // flag set when STRING was allocaated within expression
    UNARY = 0x8000, // flag set when operator (+, -) is unary
};

enum ErrorCode {
    E_MISSING_IDENTIFIER  = 1,
    E_MISSING_EQUALSIGN   = 2,
    E_MISSING_NUMBER      = 3,
    E_MISSING_TO          = 4,
    E_MISSING_THEN        = 5,
    E_SYNTAX_ERROR        = 6,
    E_MISSING_LPAREN      = 7,
    E_MISSING_RPAREN      = 8,
    E_MISSING_DEF         = 9,
    E_MISSING_GOTO        = 10,
    E_MISSING_BASE        = 11,
    E_INVALID_OPTION_BASE = 12,
    E_INVALID_DIM         = 13,
    E_WRONG_TYPE          = 14,
    E_INDEX_OUT_OF_BOUNDS = 15,
};

struct symbol_def {
    char *name;
    int  (*func)(int n, struct urubasic_type *arg, void *user);
    int  *value_ptr;
    uint8_t value_type;  // type of value (points to NUMBER or STRING)
    uint8_t reserved;
    int16_t tok;
    int16_t array_base_size;
    int16_t next;
};

struct Insn_info {
    int16_t offset;
    int16_t label;
    int8_t  sep;
};

static int  token_value, current_char, previous_char, token_len;
static char token_text[MAX_LINE_LEN];
static int  pushed_token_stack[1+MAX_LOOKAHEAD];

struct Insn_info *insn_info;
static int16_t  insn_count;

static char *program;
static int16_t  program_size;

// symbols
static smemblk_t *symbol_names;
static int16_t  symbol_count, current_line, *hashtab, extra_count;
static struct symbol_def *symbol;

static int  for_loop_stack[1+3*FOR_LOOP_DEPTH];
static int8_t master_control, option_base;

static int (* lex_readchar)(void*);
static char *data_buffer;
static int16_t data_buffer_index, data_buffer_max;
static void *read_arg;


static int ICACHE_FLASH_ATTR hash(const char *s)
{
    int hashval;

    for (hashval = 0;  *s != '\0'; )
        hashval += *s++;
    return (hashval % HASHSIZE);
}

static char * ICACHE_FLASH_ATTR store_string(char *text)
{
    // store the string in the symbol_name_buffer
    char *id = smemblk_alloc(symbol_names, (int16_t) (strlen(text) + 1));
    if (id)
        strcpy(id, text);
    return id;
}

static int ICACHE_FLASH_ATTR parse_add_extra_symbol(char *name)
{
    ++extra_count;
    symbol[MAX_SYMBOLS-extra_count].name            = name;
    symbol[MAX_SYMBOLS-extra_count].tok             = IDENTIFIER;
    symbol[MAX_SYMBOLS-extra_count].value_ptr       = NULL;
    symbol[MAX_SYMBOLS-extra_count].value_type      = NUMBER;
    symbol[MAX_SYMBOLS-extra_count].array_base_size = 0;
    symbol[MAX_SYMBOLS-extra_count].next            = 0;
    return MAX_SYMBOLS-extra_count;
}

static int ICACHE_FLASH_ATTR parse_add_symbol(char *name)
{
    // add a new symbol to the symbol table
    int hval = hash(name);
    symbol[symbol_count].name            = name;
    symbol[symbol_count].tok             = IDENTIFIER;
    symbol[symbol_count].value_ptr       = NULL;
    symbol[symbol_count].value_type      = NUMBER;
    symbol[symbol_count].array_base_size = 0;
    symbol[symbol_count].next = hashtab[hval];
    hashtab[hval] = symbol_count;

    if (name[1] == '\0' && name[0] >= 'A' && name[0] <= '_')
        hashtab[HASHSIZE+name[0]-'A'] = symbol_count;  // store symidx for quick access, if name is single uppercase letter

    return symbol_count++;
}

static void ICACHE_FLASH_ATTR add_symbol_intern(char *name, int tok, int (*func)(int n, struct urubasic_type *arg, void *user), void *user)
{
    int symidx;
    symidx = parse_add_symbol(name);
    symbol[symidx].tok             = tok;
    symbol[symidx].func            = func;
    symbol[symidx].value_ptr       = user;
    symbol[symidx].value_type      = NUMBER;
    symbol[symidx].array_base_size = 0;
}

void ICACHE_FLASH_ATTR urubasic_add_function(char *name, int (*func)(int n, struct urubasic_type *arg, void *user), void *user)
{
    int symidx;
    symidx = parse_add_symbol(store_string(name));
    symbol[symidx].tok             = FUNCTION;
    symbol[symidx].func            = func;
    symbol[symidx].value_ptr       = user;
    symbol[symidx].value_type      = NUMBER;
    symbol[symidx].array_base_size = 0;
}

static int ICACHE_FLASH_ATTR parse_lookup_symbol(char *name, int add_if_not_exist)
{
    // lookup in extra space
    int symidx, hval, idx;

    if (extra_count) {
        for (symidx=MAX_SYMBOLS-1; symidx>=MAX_SYMBOLS-extra_count; --symidx) {
            if (0 == strcmp(symbol[symidx].name, name))
                return symidx;
        }
    }

    idx = HASHSIZE+name[0]-'A';
    if (name[1] == '\0' && idx >= HASHSIZE && name[0] <= '_') {
        // short cut when name is single uppercase letter
        symidx = hashtab[idx];
        if (symidx)
            return symidx;
    }

    hval = hash(name);
    for (symidx=hashtab[hval]; symidx != 0; symidx = symbol[symidx].next) {
        if (symbol[symidx].name == name || 0 == strcmp(symbol[symidx].name, name))
            return symidx;
    }

    if (add_if_not_exist)
        symidx = parse_add_symbol(store_string(name));
    else
        symidx = 0;
    return symidx;
}

static void ICACHE_FLASH_ATTR error_msg(char *msg, int line, int err)
{
#ifdef __ETS__
    os_printf(msg, line, err);
#else
    fprintf(stderr, msg, line, err);
#endif
}

static void ICACHE_FLASH_ATTR parse_error(int error)
{
    switch (error) {
        case E_MISSING_TO:         error_msg("ERROR:%d: missing TO in FOR instruction (%d)\n", current_line, error); break;
        case E_MISSING_THEN:       error_msg("ERROR:%d: missing THEN in IF instruction (%d)\n", current_line, error); break;
        case E_MISSING_NUMBER:     error_msg("ERROR:%d: missing NUMBER in instruction (%d)\n", current_line, error); break;
        case E_MISSING_EQUALSIGN:  error_msg("ERROR:%d: missing = in instruction (%d)\n", current_line, error); break;
        case E_MISSING_IDENTIFIER: error_msg("ERROR:%d: missing IDENTIFIER in instruction (%d)\n", current_line, error); break;
        case E_MISSING_LPAREN:     error_msg("ERROR:%d: missing ( in instruction (%d)\n", current_line, error); break;
        case E_MISSING_RPAREN:     error_msg("ERROR:%d: missing ) in instruction (%d)\n", current_line, error); break;
        case E_MISSING_DEF:        error_msg("ERROR:%d: user supplied function not defined in instruction (%d)\n", current_line, error); break;
        case E_MISSING_GOTO:       error_msg("ERROR:%d: missing GOTO in instruction (%d)\n", current_line, error); break;
        case E_MISSING_BASE:       error_msg("ERROR:%d: missing BASE in instruction (%d)\n", current_line, error); break;
        case E_INVALID_OPTION_BASE:error_msg("ERROR:%d: invalid OPTION BASE (%d)\n", current_line, error); break;
        case E_INVALID_DIM:        error_msg("ERROR:%d: invalid dimension specified (%d)\n", current_line, error); break;
        case E_WRONG_TYPE:         error_msg("ERROR:%d: wrong type in assignment (%d)\n", current_line, error); break;
        case E_INDEX_OUT_OF_BOUNDS:error_msg("ERROR:%d: array index is out of bounds (%d)\n", current_line, error); break;
        default:                   error_msg("ERROR:%d: syntax error (%d)\n", current_line, error); break;
    }
}

static char *lex_input_buffer;
static int ICACHE_FLASH_ATTR read_from_buffer(void *arg)
{
    // read one character from input buffer
    return *(lex_input_buffer++);
}

static int ICACHE_FLASH_ATTR lex_shift()
{
    // shift one char back in input stream
    if (NULL == lex_input_buffer) {
        int ch = current_char;
        if (previous_char != 0)
            ch = previous_char;

        previous_char = current_char;
        current_char = 0;
        return ch;
    }
    else
        return *--lex_input_buffer;
}

static void ICACHE_FLASH_ATTR lex_clear(void) { lex_shift(); previous_char = 0; } // no memory of previous char
static int ICACHE_FLASH_ATTR is_digit(int c) { return (c >= '0' && c <= '9'); }
static int ICACHE_FLASH_ATTR is_blank(int c) { return (c == ' ' || c == '\t'); }

static int ICACHE_FLASH_ATTR read_number(int base)
{
    // read a decimal number
    int number = 0;
    while (is_digit(current_char)) {
        number *= base;
        number += (current_char & 0xf) + (9 * (current_char >> 6));
        current_char = lex_readchar(read_arg);
    }

    return number;
}

static int ICACHE_FLASH_ATTR stack_empty(int *stack) { return stack[0] == 0; }
static void ICACHE_FLASH_ATTR stack_push(int *stack, int value) { stack[++stack[0]] = value; }
static int ICACHE_FLASH_ATTR stack_top(int *stack, int pop);
static int ICACHE_FLASH_ATTR stack_pop(int *stack) { return stack_top(stack, 1); }

static int ICACHE_FLASH_ATTR stack_top(int *stack, int pop)
{
    int retval = -1;

    if (!stack_empty(stack)) {
        retval = stack[stack[0]];

        if (pop)
            stack[0]--;
    }

    return retval;
}

static void ICACHE_FLASH_ATTR lex_push_token(int tok)
{
    stack_push(pushed_token_stack, tok); // remember token for reading it again
}

static int ICACHE_FLASH_ATTR lex_next_token(int *symidx)
{
    // read one token from input stream
    *symidx = 0;
    if (!stack_empty(pushed_token_stack))
        return stack_pop(pushed_token_stack);  // use tok stored on stack

    do {
        if (previous_char != 0)
            current_char = lex_shift();
        else
            current_char = lex_readchar(read_arg);
    } while (is_blank(current_char));

    switch (current_char) {
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            token_value = read_number(10);
            lex_shift();
            return NUMBER;

        case '&':
            current_char = lex_readchar(read_arg);
            if (current_char ==  'H') {
                current_char = lex_readchar(read_arg);
                token_value = read_number(16);
                lex_shift();
                return NUMBER;
            }
            else if (current_char ==  'B') {
                current_char = lex_readchar(read_arg);
                token_value = read_number(2);
                lex_shift();
                return NUMBER;
            }
            else if (current_char ==  'O') {
                current_char = lex_readchar(read_arg);
                token_value = read_number(8);
                lex_shift();
                return NUMBER;
            }
            lex_shift();
            return '&';

        case '\r': case '\n':
            while (current_char == '\r' || current_char == '\n')
                current_char = lex_readchar(read_arg);
            lex_shift();
            return NEWLINE;

        case '\"':
            token_len = 0;
            do {
                current_char = lex_readchar(read_arg);
                token_text[token_len++] = (char) current_char;
            } while (current_char != '\0' && current_char != '\"');
            token_text[--token_len] = '\0';
            return STRING;

        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j':
        case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J':
        case 'K': case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z': case '_': {
            int i = 0;
            // keywords and identifiers

            do {
                token_text[i++] = (char) current_char;
                current_char = lex_readchar(read_arg);
            } while ((current_char >= 'a' && current_char <= 'z') || (current_char >= 'A' && current_char <= 'Z') || (current_char == '_') || (current_char == '$') || is_digit(current_char));
            token_text[i] = '\0';
            lex_shift();
            *symidx = parse_lookup_symbol(token_text, 0);

            if (*symidx != 0 && *symidx < symbol_count)
                return *symidx;
            else
                return IDENTIFIER;
        }

        case '<':
            current_char = lex_readchar(read_arg);
            if (current_char == '=')
                return LE;
            else if (current_char == '<')
                return LSH;
            else if (current_char == '>')
                return NEQ;
            lex_shift();
            return LT;

        case '>':
            current_char = lex_readchar(read_arg);
            if (current_char == '=')
                return GE;
            else if (current_char == '>')
                return RSH;
            lex_shift();
            return GT;

        case '=': return EQ;
        case ',': return COMMA;
        case ';': return SEMICOLON;
        case ':': return COLON;
        case '^': return CIRCUMFLEX;
        case '+': return PLUS;
        case '-': return MINUS;
        case '*': return MULT;
        case '/': return SOLIDUS;
        case '(': return LPAREN;
        case ')': return RPAREN;
        case 0: return 0;
        default:
                parse_error(E_SYNTAX_ERROR);
                return 0;
    }
}

static int ICACHE_FLASH_ATTR assign(char *name, int value)
{
    // assign a value to a variable
    int symidx = parse_lookup_symbol(name, 1);
    if (symbol[symidx].value_ptr == NULL)
        symbol[symidx].value_ptr = smemblk_alloc(symbol_names, sizeof(int));
    *symbol[symidx].value_ptr = value;
    return symidx;
}

static int ICACHE_FLASH_ATTR find_insn(int label)
{
    // linear search
    int i;

    for (i=0; i<insn_count; i++) {
        if (insn_info[i].label >= label)
            return i;
    }
    return -1;
}

static int ICACHE_FLASH_ATTR check_token(int tok, int expect, int error)
{
    if (tok < MAX_SYMBOLS) {
        if (error > 0 && symbol[tok].tok != expect) {
            parse_error(error);
            return 0;
        }
        return symbol[tok].tok;
    }
    else if (tok != expect) {
        if (error > 0)
            parse_error(error);
        return 0;
    }
    else
        return tok;
}

static int ICACHE_FLASH_ATTR parse_check_unary(int c, int last)
{
    if (c == MINUS || c == PLUS) {
        if (check_token(last, NUMBER, 0) == NUMBER
            || check_token(last, STRING, 0) == STRING
            || check_token(last, IDENTIFIER, 0) == IDENTIFIER
            || check_token(last, FUNCTION, 0) == FUNCTION
            || check_token(last, RPAREN, 0) == RPAREN)
            return 0;

        return UNARY;
    }
    else if (c == NOT)
        return UNARY;

    return 0;
}

static int ICACHE_FLASH_ATTR is_keyword(int tok) { return tok < symbol_count && symbol[tok].tok < NUM_KEYWORDS; }
static int ICACHE_FLASH_ATTR is_relop(int tok) { return tok == LT || tok == GT || tok == EQ || tok == NEQ || tok == GE || tok == LE; }
static int ICACHE_FLASH_ATTR is_logop(int tok) { return tok == AND || tok == OR || tok == NOT; }

static int ICACHE_FLASH_ATTR parse_precedence(int tok, int *assoc)
{
    *assoc = 0; // left to right
    if (tok == MULT || tok == SOLIDUS) return 3;
    else if (tok == CIRCUMFLEX) return 1;
    else if (tok == PLUS || tok == MINUS) return 4;
    else if (tok == (UNARY | PLUS) || tok == (UNARY | MINUS)) { *assoc = 1; /* right to left */ return 2; }
    else if (tok == NOT || tok == (UNARY | NOT)) { *assoc = 1; /* right to left */ return 2; }
    else if (tok == RPAREN) return 999;
    else if (is_relop(tok)) return 20;
    else if (tok == AND) return 30;
    else if (tok == OR) return 31;
    else return 1000;
}

static int ICACHE_FLASH_ATTR power(int base, int exp)
{
    int res = base;
    if (exp == 0) return 1;
    else if (exp < 0) return 0; // no floating point

    while (--exp > 0)
        res *= base;
    return res;
}

static void ICACHE_FLASH_ATTR reduce(int *arg_stack, int *operator_stack)
{
    int v1 = 0, v2 = 0, type1, type2 = 0;
    int op = stack_pop(operator_stack);

    if (!(op & UNARY)) {
        type2 = stack_pop(arg_stack);  // type
        v2 = stack_empty(arg_stack) ? 0 : stack_pop(arg_stack);
    }

    type1 = stack_pop(arg_stack);  // type
    v1 = stack_empty(arg_stack) ? 0 : stack_pop(arg_stack);

    if (type1 == NUMBER && (type2 == NUMBER || type2 == 0)) {
        switch (op) {
            case MULT:            stack_push(arg_stack, v1*v2); break;
            case CIRCUMFLEX:      stack_push(arg_stack, power(v1, v2)); break;
            case PLUS:            stack_push(arg_stack, v1+v2); break;
            case MINUS:           stack_push(arg_stack, v1-v2); break;
            case SOLIDUS:         stack_push(arg_stack, v1/v2); break;
            case (UNARY | MINUS): stack_push(arg_stack, -v1); break;
            case (UNARY | PLUS):  stack_push(arg_stack, v1); break;
            case GT:              stack_push(arg_stack, v1>v2 ? -1 : 0); break;
            case LT:              stack_push(arg_stack, v1<v2 ? -1 : 0); break;
            case LE:              stack_push(arg_stack, v1<=v2 ? -1 : 0); break;
            case GE:              stack_push(arg_stack, v1>=v2 ? -1 : 0); break;
            case EQ:              stack_push(arg_stack, v1==v2 ? -1 : 0); break;
            case NEQ:             stack_push(arg_stack, v1!=v2 ? -1 : 0); break;
            case AND:             stack_push(arg_stack, v1&v2); break;
            case NOT:
            case (UNARY|NOT):     stack_push(arg_stack, ~v1); break;
            case OR:              stack_push(arg_stack, v1|v2); break;
            default: stack_push(arg_stack, 0); break;
        }
        stack_push(arg_stack, NUMBER);
    }
    else if ((type1 & 0xff) == STRING && (type2 & 0xff) == STRING) {
        if (op == PLUS) {
            char *string = smemblk_alloc(symbol_names, (int16_t) (1 + strlen((char*)symbol_names+v1) + strlen((char*)symbol_names+v2)));
            if (string != NULL) {
                strcpy(string, (char*)symbol_names+v1);
                strcat(string, (char*)symbol_names+v2);
            }

            if (ALLOC & type1) smemblk_free(symbol_names, (char*)symbol_names+v1);
            if (ALLOC & type2) smemblk_free(symbol_names, (char*)symbol_names+v2);
            stack_push(arg_stack, string-(char*)symbol_names);
            stack_push(arg_stack, STRING|ALLOC);
        }
        else if (op == EQ) {
            stack_push(arg_stack, 0 == strcmp((char*)symbol_names+v1, (char*)symbol_names+v2));
            stack_push(arg_stack, NUMBER);
            if (ALLOC & type1) smemblk_free(symbol_names, (char*)symbol_names+v1);
            if (ALLOC & type2) smemblk_free(symbol_names, (char*)symbol_names+v2);
        }
        else if (op == NEQ) {
            stack_push(arg_stack, 0 != strcmp((char*)symbol_names+v1, (char*)symbol_names+v2));
            stack_push(arg_stack, NUMBER);
            if (ALLOC & type1) smemblk_free(symbol_names, (char*)symbol_names+v1);
            if (ALLOC & type2) smemblk_free(symbol_names, (char*)symbol_names+v2);
        }
    }
    else
        parse_error(E_SYNTAX_ERROR);
}

static int ICACHE_FLASH_ATTR expr(struct urubasic_type *tval);

static int ICACHE_FLASH_ATTR function_call(int symidx, int paren_optional, struct urubasic_type *retval)
{
    int tok, i, n = 1, val, to_be_pushed = 0, dummy;
    int endtok = RPAREN;
    struct urubasic_type tval = { 0, }, arg[MAX_FUNCTION_ARGS+1];

    tok = lex_next_token(&dummy);
    if (LPAREN != check_token(tok, LPAREN, 0) && paren_optional)
        endtok = NEWLINE;

    arg[0].type = arg[0].value = 0;  // return value

    if (endtok == NEWLINE || LPAREN == check_token(tok, LPAREN, 0)) {
        if (endtok != NEWLINE)
            tok = lex_next_token(&dummy);
        while (endtok != check_token(tok, endtok, 0)) {
            lex_push_token(tok);
            if (tok == COLON)
                break;
            expr(&tval);
            if (n < MAX_FUNCTION_ARGS)
                arg[n++] = tval;
            tok = lex_next_token(&dummy);
            if (COMMA == check_token(tok, COMMA, 0))
                tok = lex_next_token(&dummy);
        }
    }
    else
        to_be_pushed = tok;

    if (symbol[symidx].func == NULL) {
        // user supplied function
        char *old_input_buffer = lex_input_buffer, var[MAX_LINE_LEN];
        int old_symbol_count = extra_count;

        // setup up lexer to read the DEF statement
        lex_clear();
        pushed_token_stack[0] = 0;
        if (symbol[symidx].value_ptr == NULL)
            parse_error(E_SYNTAX_ERROR);
        else
            lex_input_buffer = &program[insn_info[*symbol[symidx].value_ptr].offset];
        tok = lex_next_token(&dummy);
        check_token(tok, DEF, E_MISSING_DEF);
        tok = lex_next_token(&dummy);
        check_token(tok, FUNCTION, E_MISSING_IDENTIFIER);

        tok = lex_next_token(&dummy);
        if (LPAREN == check_token(tok, LPAREN, 0)) {
            tok = lex_next_token(&dummy);
            while (RPAREN != check_token(tok, RPAREN, 0)) {
                check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
                strcpy(var, token_text);
                tok = lex_next_token(&dummy);
                if (COMMA == check_token(tok, COMMA, 0))
                    tok = lex_next_token(&dummy);
            }
        }
        else
            lex_push_token(tok);

        tok = lex_next_token(&dummy);
        check_token(tok, EQ, E_MISSING_EQUALSIGN);
        symidx = parse_add_extra_symbol(var);
        if (symbol[symidx].value_ptr == NULL)
            symbol[symidx].value_ptr = smemblk_alloc(symbol_names, sizeof(int));
        *symbol[symidx].value_ptr = arg[1].value;  // add the actual parameter as a symbol
        expr(retval); // evaluate the def statement
        val = retval->value;

        // revert the lexer back to original input stream
        lex_clear();
        pushed_token_stack[0] = 0;
        lex_input_buffer = old_input_buffer;
        smemblk_free(symbol_names, symbol[symidx].value_ptr);
        symbol[symidx].value_ptr = NULL;
        extra_count = old_symbol_count;   // remove actual parameter from symbol table
        if (to_be_pushed)
            lex_push_token(to_be_pushed);
    }
    else {
        // builtin function
        if (to_be_pushed)
            lex_push_token(to_be_pushed);
        val = symbol[symidx].func(n, arg, (void *) symbol[symidx].value_ptr);

        for (i=1; i<n; i++) {
            if (arg[i].type == (STRING|ALLOC))
                smemblk_free(symbol_names, (char *) symbol_names + arg[i].value);
        }
        retval->type  = arg[0].type;
        retval->value = arg[0].value;
    }

    return val;
}

static void ICACHE_FLASH_ATTR lex_next_token_expr(int *tok, int *last, int *paren_depth, int *symidx)
{
    if (last) *last = *tok;
    *tok = lex_next_token(symidx);
    if (*tok < MAX_SYMBOLS && (is_logop(symbol[*tok].tok)))
        *tok = symbol[*tok].tok;
    if (paren_depth) {
        if (*tok == LPAREN) ++*paren_depth;
        if (*tok == RPAREN) --*paren_depth;
    }
}

static int * ICACHE_FLASH_ATTR parse_subscript(int symidx)
{
    int tok, x = option_base, y = option_base, size = 1, array_base_size = 0, dummy;
    struct urubasic_type tval = { 0, };

    tok = lex_next_token(&dummy);
    if (LPAREN == check_token(tok, LPAREN, 0)) {
        expr(&tval);
        x = tval.value;
        array_base_size = size = 11 - option_base;
        tok = lex_next_token(&dummy);
        if (COMMA == check_token(tok, COMMA, 0)) {
            expr(&tval);
            y = tval.value;
            size *= 11 - option_base;
            tok = lex_next_token(&dummy);
        }
        check_token(tok, RPAREN, E_MISSING_RPAREN);
    }
    else
        lex_push_token(tok);

    if (symbol[symidx].value_ptr == NULL) {
        if ((y - option_base) + (x - option_base) >= size) {
            parse_error(E_INDEX_OUT_OF_BOUNDS);
            return NULL;
        }

        symbol[symidx].value_ptr = smemblk_zalloc(symbol_names, (int16_t) (size * sizeof(int)));
        symbol[symidx].array_base_size = array_base_size;
    }

    return symbol[symidx].value_ptr + symbol[symidx].array_base_size * (y - option_base) + (x - option_base);
}

static int ICACHE_FLASH_ATTR expr(struct urubasic_type *tval)
{
    int tok, arg_stack[1+2*EXPR_STACK_SIZE], operator_stack[1+EXPR_STACK_SIZE];
    int last_sym, paren_depth, symidx;

    last_sym = paren_depth = tok = 0;
    arg_stack[0] = 0;           // stack empty
    operator_stack[0] = 0;      // stack_empty

	lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
    while ((tok != RPAREN || paren_depth >= 0) && tok != NEWLINE && tok != 0 && tok != THEN && tok != COMMA && tok != COLON && tok != SEMICOLON && !is_keyword(tok)) {
        if (IDENTIFIER == check_token(tok, IDENTIFIER, 0)) {
            int *value_ptr;
            if (symidx == 0)
                symidx = parse_lookup_symbol(token_text, 1);
            if (symbol[symidx].value_type == STRING) {
                char *string = (char *) symbol[symidx].value_ptr;
                stack_push(arg_stack, string-(char *)symbol_names);
                stack_push(arg_stack, STRING);
            }
            else {
                value_ptr = parse_subscript(symidx);
                if (value_ptr != NULL) {
                    stack_push(arg_stack, *value_ptr);
                    stack_push(arg_stack, NUMBER);
                }
            }
            lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
        }
        else if (FUNCTION == check_token(tok, FUNCTION, 0)) {
            struct urubasic_type tval;
            if (symidx == 0)
                symidx = parse_lookup_symbol(token_text, 1);
            function_call(symidx, 0, &tval);
            stack_push(arg_stack, tval.value);
            stack_push(arg_stack, tval.type);
            lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
        }
        else if (NUMBER == check_token(tok, NUMBER, 0)) {
            stack_push(arg_stack, token_value);
            stack_push(arg_stack, NUMBER);
            lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
        }
        else if (STRING == check_token(tok, STRING, 0)) {
            char *string = smemblk_alloc(symbol_names, (int16_t) (strlen(token_text)+1));
            if (string != NULL) {
                strcpy(string, token_text);
                stack_push(arg_stack, string-(char *)symbol_names);
                stack_push(arg_stack, STRING|ALLOC);  // mark constant string as allocated
            }
            lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
        }
        else {
            if (tok != LPAREN && !stack_empty(operator_stack)) {
                int prec_top, prec_cur, assoc, dummy;
                prec_top = parse_precedence(stack_top(operator_stack, 0), &assoc);
                prec_cur = parse_precedence(tok | parse_check_unary(tok, last_sym), &dummy);

                if (prec_top > prec_cur || (prec_top == prec_cur && assoc == 1)) {
                    // shift
                    tok |= parse_check_unary(tok, last_sym);
                    stack_push(operator_stack, tok);
                    lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
                }
                else {
                    reduce(arg_stack, operator_stack);
                    if (tok == RPAREN && LPAREN == stack_top(operator_stack, 0)) {
                        stack_pop(operator_stack);
                        lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
                    }
                }
            }
            else {
                // shift
                tok |= parse_check_unary(tok, last_sym);
                stack_push(operator_stack, tok);
                lex_next_token_expr(&tok, &last_sym, &paren_depth, &symidx);
            }
        }
    }

    while (!stack_empty(operator_stack)) {
        reduce(arg_stack, operator_stack);
    }
    lex_push_token(tok);
    tval->type     = stack_pop(arg_stack);
    tval->value    = stack_pop(arg_stack);
    return tval->type;
}

static int ICACHE_FLASH_ATTR stmt(int insn);

static int ICACHE_FLASH_ATTR func_abs(int n, struct urubasic_type *arg, void *user)
{
    arg[0].type = NUMBER;
    arg[0].value = arg[1].value >= 0 ? arg[1].value : -arg[1].value;
    return arg[0].value;
}

static int ICACHE_FLASH_ATTR func_sgn(int n, struct urubasic_type *arg, void *user)
{
    arg[0].type = NUMBER;
    arg[0].value = arg[1].value > 0 ? 1 : (arg[1].value < 0 ? -1 : 0);
    return arg[0].value;
}

static int ICACHE_FLASH_ATTR stmt_rem(int insn, struct urubasic_type *arg, void *user)  { return ++insn; }
static int ICACHE_FLASH_ATTR stmt_stop(int insn, struct urubasic_type *arg, void *user) { return -1; }

static int ICACHE_FLASH_ATTR func_len(int n, struct urubasic_type *arg, void *user)
{
    int result;

    if (n < 2 || (arg[1].type & 0xff) != STRING)
        parse_error(E_SYNTAX_ERROR);

    result = strlen((char *) symbol_names + arg[1].value);
    arg[0].value = result;
    arg[0].type = NUMBER;
    return result;
}

static int ICACHE_FLASH_ATTR func_chrS(int n, struct urubasic_type *arg, void *user)
{
    char *string;

    if (n < 2 || (arg[1].type & 0xff) != NUMBER)
        parse_error(E_SYNTAX_ERROR);

    string = smemblk_alloc(symbol_names, 2);
    string[0] = arg[1].value & 0xff;
    string[1] = '\0';
    arg[0].type = STRING|ALLOC;
    arg[0].value = string-(char *)symbol_names;
    return 0;
}

static int ICACHE_FLASH_ATTR func_asc(int n, struct urubasic_type *arg, void *user)
{
    char *string;

    if (n < 2 || (arg[1].type & 0xff) != STRING)
        parse_error(E_SYNTAX_ERROR);

    string = (char *)symbol_names+arg[1].value;
    arg[0].type = NUMBER;
    arg[0].value = string[0];
    return 0;
}

static int ICACHE_FLASH_ATTR func_midintern(int n, struct urubasic_type *arg, int start, int len)
{
    char *dst, *src;
    int  i;

    dst = smemblk_alloc(symbol_names, (int16_t) (len+1));
    src = (char*)symbol_names+arg[1].value;

    for (i=0; i<start && src && *src; ++i)
        src++;

    for (i=0; i<len && src && *src; ++i)
        dst[i] = *src++;
    dst[i] = '\0';

    arg[0].type = STRING|ALLOC;
    arg[0].value = dst-(char *)symbol_names;

    return 0;
}

static int ICACHE_FLASH_ATTR func_leftS(int n, struct urubasic_type *arg, void *user)
{
    int  len;

    if (n < 3 || (arg[1].type & 0xff) != STRING || (arg[2].type & 0xff) != NUMBER)
        parse_error(E_SYNTAX_ERROR);

    len = arg[2].value;
    if (len < 0)
        len = 0;

    return func_midintern(n, arg, 0, len);
}

static int ICACHE_FLASH_ATTR func_midS(int n, struct urubasic_type *arg, void *user)
{
    int  len = 0;

    if (n == 3) {
        if ((arg[1].type & 0xff) != STRING || (arg[2].type & 0xff) != NUMBER)
            parse_error(E_SYNTAX_ERROR);
        len = strlen((char *) symbol_names+arg[1].value);
    }
    else if (n == 4) {
        if ((arg[1].type & 0xff) != STRING || (arg[2].type & 0xff) != NUMBER || (arg[3].type & 0xff) != NUMBER)
            parse_error(E_SYNTAX_ERROR);
        len = arg[3].value;
    }
    if (len < 0)
        len = 0;

    return func_midintern(n, arg, arg[2].value-1, len);
}

static int ICACHE_FLASH_ATTR func_rightS(int n, struct urubasic_type *arg, void *user)
{
    char *src;
    int  len, start;

    if (n < 3 || (arg[1].type & 0xff) != STRING || (arg[2].type & 0xff) != NUMBER)
        parse_error(E_SYNTAX_ERROR);

    len = arg[2].value;
    if (len < 0)
        len = 0;

    src = (char*)symbol_names+arg[1].value;
    start = strlen(src) - len;
    if (start < 0) {
        len += start;
        start = 0;
    }

    return func_midintern(n, arg, start, len);
}

static int ICACHE_FLASH_ATTR func_strS(int n, struct urubasic_type *arg, void *user)
{
    char *dst;

    if (n < 2 || (arg[1].type & 0xff) != NUMBER)
        parse_error(E_SYNTAX_ERROR);

    dst = smemblk_alloc(symbol_names, 12);
    sprintf(dst, "%s%d ", arg[1].value < 0 ? "" : " ", arg[1].value);
    arg[0].type = STRING|ALLOC;
    arg[0].value = dst-(char *)symbol_names;
    return 0;
}

static int ICACHE_FLASH_ATTR func_stringS(int n, struct urubasic_type *arg, void *user)
{
    if (n == 3 && urubasic_is_number(&arg[1])) {
        int len, ch, i;
        char *s;

        len = urubasic_get_number(&arg[1]);
        if (urubasic_is_string(&arg[2])) {
            s = urubasic_get_string(&arg[2]);
            ch = s[0];
        }
        else
            ch = urubasic_get_number(&arg[2]);

        urubasic_alloc_string(&arg[0], len+1);
        s = urubasic_get_string(&arg[0]);
        for (i=0; i<len; i++)
            s[i] = ch;
        s[i] = '\0';
    }

    return 0;
}

static int ICACHE_FLASH_ATTR stmt_goto(int insn, struct urubasic_type *arg, void *user)
{
    struct urubasic_type tval = { 0, };
    expr (&tval);
    return find_insn(tval.value);
}

static int ICACHE_FLASH_ATTR func_min(int n, struct urubasic_type *arg, void *user)
{
    int m = INT_MAX, i;
    for (i=1; i<n; ++i) {
        if (arg[i].value < m)
            m = arg[i].value;
    }
    arg[0].value = m;
    arg[0].type = NUMBER;
    return m;
}

static int ICACHE_FLASH_ATTR func_max(int n, struct urubasic_type *arg, void *user)
{
    int m = INT_MIN, i;
    for (i=1; i<n; ++i) {
        if (arg[i].value > m)
            m = arg[i].value;
    }
    arg[0].value = m;
    arg[0].type = NUMBER;
    return m;
}

static int ICACHE_FLASH_ATTR stmt_next(int insn, struct urubasic_type *arg, void *user)
{
    int max_val, val, step, symidx, tok;
    tok = lex_next_token(&symidx);
    check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);

    if (symidx == 0)
        symidx = parse_lookup_symbol(token_text, 0);
    if (!is_keyword(symidx) && (master_control == 0 || master_control == symidx)) {
        master_control = 0;
        step           = stack_pop(for_loop_stack);
        max_val        = stack_pop(for_loop_stack);
        if (symbol[symidx].value_ptr == NULL)
            symbol[symidx].value_ptr = smemblk_alloc(symbol_names, sizeof(int));
        *symbol[symidx].value_ptr = val = *symbol[symidx].value_ptr+step;
        if ((step > 0 && val > max_val) || (step < 0 && val < max_val)) {
            // loop finished
            stack_pop(for_loop_stack);
            ++insn;
        }
        else {
            // continue loop
            insn = stack_pop(for_loop_stack);
            stack_push(for_loop_stack, insn);
            stack_push(for_loop_stack, max_val);
            stack_push(for_loop_stack, step);
        }
    }
    else
        ++insn;

    return insn;
}

static char * ICACHE_FLASH_ATTR trimright(char *s)
{
    int len = strlen(s);
    while (len > 0 && is_blank(s[len-1]))
        s[--len] = '\0';

    return s;
}

static int ICACHE_FLASH_ATTR stmt_print(int insn, struct urubasic_type *arg, void *user)
{
    int tok, ends_with_separator, nargs, dummy;
    struct urubasic_type tval = { 0, };
    static char line[MAX_LINE_LEN];
    static int line_len = 0;

    ends_with_separator = nargs = 0;
    while (1) {
        tok = lex_next_token(&dummy);
        if (TAB == check_token(tok, TAB, 0) || COMMA == check_token(tok, COMMA, 0)) {
            int n;

            ++nargs;
            if (TAB == check_token(tok, TAB, 0)) {
                tok = lex_next_token(&dummy);
                check_token(tok, LPAREN, E_MISSING_LPAREN);
                expr(&tval);
                n = tval.value;
                tok = lex_next_token(&dummy);
                check_token(tok, RPAREN, E_MISSING_RPAREN);
                ends_with_separator = 0;
            }
            else {
                n = line_len + (PRINT_ZONE_LEN - (line_len % PRINT_ZONE_LEN));
                ends_with_separator = 1;
            }

            if (n < 1) n = 1;
            n -= MAX_LINE_LEN * ((n-1) / MAX_LINE_LEN);
            if (line_len > n) {
                printf("%s\n", trimright(line));
                line[0] = '\0';
                line_len = 0;
            }

            while (line_len < n) {
                strcat(line, " ");
                line_len++;
            }
        }
        else if (SEMICOLON == check_token(tok, SEMICOLON, 0)) {
            ++nargs;
            ends_with_separator = 1;
        }
        else if (tok == NEWLINE || tok == 0 || tok == COLON) {
            break;
        }
        else {
            char temp[MAX_LINE_LEN], *s;
            int len;

            ++nargs;
            if (tok != STRING) {
                int val;
                lex_push_token(tok);
                expr(&tval);
                if (tval.type == NUMBER) {
                    val = tval.value;
                    sprintf(temp, "%s%d ", val < 0 ? "" : " ", val);
                }
                else {
                    sprintf(temp, "%s", (char *)symbol_names+tval.value);
                    if (tval.type & ALLOC)
                        smemblk_free(symbol_names, (char *)symbol_names+tval.value);
                }

                s = temp;
            }
            else
                s = token_text;

            len = strlen(s);
            if (PRINT_ZONE_LEN + line_len > PRINT_ZONE_LEN * MAX_PRINT_ZONES) {
                printf("%s\n", trimright(line));
                line[0] = '\0';
                line_len = 0;
            }
            strcat(line, s);
            line_len += len;
            ends_with_separator = 0;
        }
    }

    if (ends_with_separator && line_len < PRINT_ZONE_LEN * MAX_PRINT_ZONES) {
        printf("%s", line);
    }
    else {
        printf("%s\n", trimright(line));
        line_len = 0;
    }
    line[0] = '\0';
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_read(int insn, struct urubasic_type *arg, void *user)
{
    int symidx, tok, *value_ptr;

    do {
        tok = lex_next_token(&symidx);
        check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
        if (symidx == 0)
            symidx = parse_lookup_symbol(token_text, 1);
        value_ptr = parse_subscript(symidx);
        if (value_ptr != NULL) {
            if (data_buffer[data_buffer_index] == -1) {
                char *string;
                ++data_buffer_index;
                symbol[symidx].value_type = STRING;
                string = smemblk_realloc(symbol_names, symbol[symidx].value_ptr, data_buffer[data_buffer_index]);
                symbol[symidx].value_ptr = (int *) string;
                strcpy(string, &data_buffer[data_buffer_index+1]);
                data_buffer_index += data_buffer[data_buffer_index]+1;
            }
            else if (data_buffer[data_buffer_index] == 1) {
                *value_ptr = data_buffer[data_buffer_index+1];
                data_buffer_index += 2;
            }
            else if (data_buffer[data_buffer_index] == 2) {
                *value_ptr = data_buffer[data_buffer_index+1];
                *value_ptr <<= 8;
                *value_ptr += data_buffer[data_buffer_index+2];
                data_buffer_index += 3;
            }
            else if (data_buffer[data_buffer_index] == 4) {
                *value_ptr = data_buffer[data_buffer_index+1];
                *value_ptr <<= 8;
                *value_ptr += data_buffer[data_buffer_index+2];
                *value_ptr <<= 8;
                *value_ptr += data_buffer[data_buffer_index+3];
                *value_ptr <<= 8;
                *value_ptr += data_buffer[data_buffer_index+4];
                data_buffer_index += 5;
            }
        }
        tok = lex_next_token(&symidx);
    } while (COMMA == check_token(tok, COMMA, 0));
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_restore(int insn, struct urubasic_type *arg, void *user)
{
    data_buffer_index = 0;
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_def(int insn, struct urubasic_type *arg, void *user)
{
    int symidx, tok;

    tok = lex_next_token(&symidx);
    check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
    if (symidx == 0)
        symidx = parse_lookup_symbol(token_text, 1);
    symbol[symidx].tok = FUNCTION;
    symbol[symidx].func = NULL;
    if (symbol[symidx].value_ptr == NULL)
        symbol[symidx].value_ptr = smemblk_alloc(symbol_names, sizeof(int));
    *symbol[symidx].value_ptr = insn;

    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_for(int insn, struct urubasic_type *arg, void *user)
{
    int start, end, step = 1, symidx, tok, dummy, i, n;
    char var[MAX_LINE_LEN];
    struct urubasic_type tval = { 0, };

    tok = lex_next_token(&dummy);
    check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
    strcpy(var, token_text);

    tok = lex_next_token(&dummy);
    check_token(tok, EQ, E_MISSING_EQUALSIGN);
    expr(&tval);
    start = tval.value;
    symidx = assign(var, start);
    tok = lex_next_token(&dummy);
    check_token(tok, TO, E_MISSING_TO);
    expr(&tval);
    end = tval.value;

    tok = lex_next_token(&dummy);
    if (STEP == check_token(tok, STEP, 0)) {
        expr(&tval);
        step = tval.value;
    }
    else
        lex_push_token(tok);

    ++insn;
    n = for_loop_stack[0];
    // walk the stack and remove entries if we find the same for loop again
    for (i=0; n >= 3*(i+1); ++i) {
        if (for_loop_stack[n-(3*i+2)] == insn && for_loop_stack[n-(3*i+1)] == end && for_loop_stack[n-(3*i+0)] == step) {
            for_loop_stack[0] = n-3*(i+1);
            break;
        }
    }

    stack_push(for_loop_stack, insn);
    stack_push(for_loop_stack, end);
    stack_push(for_loop_stack, step);
    if ((step > 0 && start > end) || (step < 0 && start < end))
        master_control = symidx; // prevent execution until NEXT
    return insn;
}

static int ICACHE_FLASH_ATTR stmt_gosub(int insn, struct urubasic_type *arg, void *user)
{
    stack_push(for_loop_stack, insn+1);
    stack_push(for_loop_stack, -1);
    stack_push(for_loop_stack, 0);
    return stmt_goto(insn, arg, user);
}

static int ICACHE_FLASH_ATTR stmt_on(int insn, struct urubasic_type *arg, void *user)
{
    int val, val_expr, n = 1, tok, new_insn = -1, gosub = 0, dummy;
    struct urubasic_type tval = { 0, };

    expr(&tval);
    val_expr = tval.value;

    tok = lex_next_token(&dummy);
    if (GOSUB == check_token(tok, GOSUB, 0))
        gosub = 1;
    else
        check_token(tok, GOTO, E_MISSING_GOTO);
    while (new_insn == -1) {
        expr(&tval);
        val = tval.value;

        if (n != val_expr) {
            tok = lex_next_token(&dummy);
            ++n;
            if (!check_token(tok, COMMA, 0)) {
                lex_push_token(tok);
                new_insn = insn+1;
            }
        }
        else {
            if (gosub) {
                stack_push(for_loop_stack, insn+1);
                stack_push(for_loop_stack, -1);
                stack_push(for_loop_stack, 0);
            }
            new_insn = find_insn(val);
        }
    }

    return new_insn;
}

static int ICACHE_FLASH_ATTR stmt_return(int insn, struct urubasic_type *arg, void *user)
{
    int step = 0, end = 0;
    do {
        // pop stack until we find a GOSUB (step = 0 and end = -1)
        if (!stack_empty(for_loop_stack)) step = stack_pop(for_loop_stack);
        if (!stack_empty(for_loop_stack)) end = stack_pop(for_loop_stack);
        if (stack_empty(for_loop_stack))
            insn = -1;
        else
            insn = stack_pop(for_loop_stack);
    } while (end != -1 || step != 0);

    return insn;
}

static int ICACHE_FLASH_ATTR stmt_let(int insn, struct urubasic_type *arg, void *user)
{
    int symidx, tok, *value_ptr, dummy;
    struct urubasic_type tval = { 0, };

    tok = lex_next_token(&symidx);
    check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
    if (symidx == 0)
        symidx = parse_lookup_symbol(token_text, 1);
    value_ptr = parse_subscript(symidx);
    tok = lex_next_token(&dummy);
    check_token(tok, EQ, E_MISSING_EQUALSIGN);
    if (value_ptr != NULL) {
        expr(&tval);
        if ((tval.type & 0xff) == STRING) {
            if (symbol[symidx].array_base_size != 0)
                parse_error(E_WRONG_TYPE);
            symbol[symidx].value_type = STRING;
            smemblk_free(symbol_names, symbol[symidx].value_ptr);
            symbol[symidx].value_ptr = (int *) ((char *) symbol_names + tval.value);
        }
        else {
            symbol[symidx].value_type = NUMBER;
            *value_ptr = tval.value;
        }
    }
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_if(int insn, struct urubasic_type *arg, void *user)
{
    int tok, dummy;
    struct urubasic_type tval;

    expr(&tval);
    tok = lex_next_token(&dummy);
    check_token(tok, THEN, E_MISSING_THEN);
    if (tval.value) {
        // then
        insn = stmt(insn);
    }
    else {
        // else: find the next \n seperated line
        ++insn;
        while (insn_info[insn].sep == ':')
            ++insn;
    }

    return insn;
}

static int ICACHE_FLASH_ATTR stmt_option(int insn, struct urubasic_type *arg, void *user)
{
    int tok, v, dummy;
    struct urubasic_type tval = { 0, };

    tok = lex_next_token(&dummy);
    check_token(tok, BASE, E_MISSING_BASE);
    expr(&tval);
    v = tval.value;
    if (v == 0 || v == 1)
        option_base = v;
    else
        parse_error(E_INVALID_OPTION_BASE);
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt_dim(int insn, struct urubasic_type *arg, void *user)
{
    int tok, x, y, symidx, size, dummy, y_set;
    struct urubasic_type tval = { 0, };

    do {
        x = 0;
        y = option_base;
        y_set = 0;
        tok = lex_next_token(&symidx);
        check_token(tok, IDENTIFIER, E_MISSING_IDENTIFIER);
        if (symidx == 0)
            symidx = parse_lookup_symbol(token_text, 1);

        tok = lex_next_token(&dummy);
        if (LPAREN == check_token(tok, LPAREN, 0)) {
            expr(&tval);
            x = tval.value;
            tok = lex_next_token(&dummy);
            if (COMMA == check_token(tok, COMMA, 0)) {
                expr(&tval);
                y = tval.value;
                y_set = 1;
                tok = lex_next_token(&dummy);
            }
            check_token(tok, RPAREN, E_MISSING_RPAREN);
        }

        if (x < option_base || (y_set && y < option_base))
            parse_error(E_INVALID_DIM);

        symbol[symidx].array_base_size = x - option_base + 1;
        size = symbol[symidx].array_base_size * (y - option_base + 1) * sizeof(int);
        symbol[symidx].value_ptr = smemblk_realloc(symbol_names, symbol[symidx].value_ptr, (int16_t) size);
        tok = lex_next_token(&dummy);
    } while (COMMA == check_token(tok, COMMA, 0));

    lex_push_token(tok);
    return insn+1;
}

static int ICACHE_FLASH_ATTR stmt(int insn)
{
    int tok, symidx;

    tok = lex_next_token(&symidx);
    if (tok == 0)
        return -1;

    if (is_keyword(tok) && symbol[tok].tok == NEXT)
        insn = symbol[tok].func(insn, NULL, (void *) symbol[tok].value_ptr);
    else if (master_control) {
        ++insn;
    }
    else if (tok == NUMBER) {
        struct urubasic_type tval = { 0, };
        lex_push_token(tok);
        expr(&tval);
        insn = find_insn(tval.value);
    }
    else if (is_keyword(tok))
        insn = symbol[tok].func(insn, NULL, (void *) symbol[tok].value_ptr);
    else if (IDENTIFIER == check_token(tok, IDENTIFIER, 0)) {
        lex_push_token(tok);
        insn = stmt_let(insn, NULL, NULL);
    }
    else if (FUNCTION == check_token(tok, FUNCTION, 0)) {
        struct urubasic_type tval;
        if (symidx == 0)
            symidx = parse_lookup_symbol(token_text, 1);
        function_call(symidx, 1, &tval);
        ++insn;
    }
    else {
        parse_error(E_SYNTAX_ERROR);
        insn = -1;
    }

    return insn;
}

void ICACHE_FLASH_ATTR urubasic_execute(int insn)
{
    // execute until END or no more instruction
    for_loop_stack[0] = 0;  // reset GOSUB and FOR/NEXT stack
    insn = find_insn(insn);
    while (insn >= 0 && insn < insn_count) {
        lex_clear();
        pushed_token_stack[0] = 0;
        lex_input_buffer = &program[insn_info[insn].offset];
        //current_offset = 0;
        current_line = insn_info[insn].label;
        insn = stmt(insn);
    }
}

static void ICACHE_FLASH_ATTR read_data(int max_data_buffer)
{
    int tok, dummy;
    struct urubasic_type tval = { 0, };

    do {
        if (data_buffer_index < max_data_buffer) {
            tok = lex_next_token(&dummy);
            if (tok == IDENTIFIER || (tok & 0xff) == STRING) {
                int len = 1+strlen(token_text);
                data_buffer[data_buffer_index++] = -1; // string indicator
                strcpy(&data_buffer[data_buffer_index+1], token_text);
                data_buffer[data_buffer_index] = len;
                data_buffer_index += 1+len;
            }
            else if (tok == NUMBER || tok == MINUS) {
                lex_push_token(tok);
                expr(&tval);
                if (tval.value >= -128 && tval.value < 0xff) {
                    data_buffer[data_buffer_index] = 1;
                    data_buffer[data_buffer_index+1] = tval.value;
                    data_buffer_index += 2;
                }
                else if (tval.value >= -32768 && tval.value < 0xffff) {
                    data_buffer[data_buffer_index] = 2;
                    data_buffer[data_buffer_index+1] = (tval.value >> 8) & 0xff;
                    data_buffer[data_buffer_index+2] = tval.value & 0xff;
                    data_buffer_index += 3;
                }
                else {
                    data_buffer[data_buffer_index] = 4;
                    data_buffer[data_buffer_index+1] = (tval.value >> 24) & 0xff;
                    data_buffer[data_buffer_index+2] = (tval.value >> 16) & 0xff;
                    data_buffer[data_buffer_index+3] = (tval.value >> 8) & 0xff;
                    data_buffer[data_buffer_index+4] = tval.value & 0xff;
                    data_buffer_index += 5;
                }
            }
        }
        tok = lex_next_token(&dummy);
    } while (COMMA == check_token(tok, COMMA, 0));
    current_char = '\n';
}

int ICACHE_FLASH_ATTR urubasic_init(int max_mem, int max_symbols, int (*read_from_stdin)(void*), void *arg)
{
    int insn, count, inside_remark, inside_string, sep = '\n';
    char *symbol_name_buffer;

    read_arg = arg;

    program = malloc(max_mem);
    insn_info = calloc(max_mem, 1);
    data_buffer = malloc(max_mem);
    symbol = calloc(max_symbols * sizeof(symbol[0]) + HASHSIZE * sizeof(int16_t) + ('_'-'A'+1) * sizeof(int16_t), 1);
    hashtab = (int16_t *) &symbol[max_symbols];

    add_symbol_intern("", 0, NULL, NULL);
    add_symbol_intern("NEXT", NEXT, stmt_next, NULL);
    add_symbol_intern("PRINT", PRINT, stmt_print, NULL);
    add_symbol_intern("GOTO", GOTO, stmt_goto, NULL);
    add_symbol_intern("END", END, stmt_stop, NULL);
    add_symbol_intern("FOR", FOR, stmt_for, NULL);
    add_symbol_intern("TO", TO, NULL, NULL);
    add_symbol_intern("REM", REM, stmt_rem, NULL);
    add_symbol_intern("GOSUB", GOSUB, stmt_gosub, NULL);
    add_symbol_intern("RETURN", RETURN, stmt_return, NULL);
    add_symbol_intern("LET", LET, stmt_let, NULL);
    add_symbol_intern("IF", IF, stmt_if, NULL);
    add_symbol_intern("THEN", THEN, NULL, NULL);
    add_symbol_intern("TAB", TAB, NULL, NULL);
    add_symbol_intern("STOP", STOP, stmt_stop, NULL);
    add_symbol_intern("STEP", STEP, NULL, NULL);
    add_symbol_intern("DEF", DEF, stmt_def, NULL);
    add_symbol_intern("ON", ON, stmt_on, NULL);
    add_symbol_intern("ABS", FUNCTION, func_abs, NULL);
    add_symbol_intern("MIN", FUNCTION, func_min, NULL);
    add_symbol_intern("MAX", FUNCTION, func_max, NULL);
    add_symbol_intern("SGN", FUNCTION, func_sgn, NULL);
    add_symbol_intern("DATA", DATA, stmt_rem, NULL);
    add_symbol_intern("READ", READ, stmt_read, NULL);
    add_symbol_intern("RESTORE", RESTORE, stmt_restore, NULL);
    add_symbol_intern("OPTION", OPTION, stmt_option, NULL);
    add_symbol_intern("BASE", BASE, NULL, NULL);
    add_symbol_intern("DIM", DIM, stmt_dim, NULL);
    add_symbol_intern("LEN", FUNCTION, func_len, NULL);
    add_symbol_intern("CHR$", FUNCTION, func_chrS, NULL);
    add_symbol_intern("LEFT$", FUNCTION, func_leftS, NULL);
    add_symbol_intern("MID$", FUNCTION, func_midS, NULL);
    add_symbol_intern("RIGHT$", FUNCTION, func_rightS, NULL);
    add_symbol_intern("ASC", FUNCTION, func_asc, NULL);
    add_symbol_intern("STR$", FUNCTION, func_strS, NULL);
    add_symbol_intern("AND", AND, NULL, NULL);
    add_symbol_intern("NOT", NOT, NULL, NULL);
    add_symbol_intern("OR", OR, NULL, NULL);
    add_symbol_intern("STRING$", FUNCTION, func_stringS, NULL);

    lex_readchar = read_from_stdin;
    do {
again:  do {
            current_char = lex_readchar(read_arg);
        } while (current_char == '\r' || current_char == '\n' || is_blank(current_char));

        if (current_char == '#') {
            while (current_char != '\r' && current_char != '\n')
                current_char = lex_readchar(read_arg);
            goto again;
        }
        insn = insn_count++;
        insn_info[insn].offset = program_size;
        insn_info[insn].sep    = sep;
        if (is_digit(current_char))
            insn_info[insn].label  = read_number(10);
        else if (insn > 0)
            insn_info[insn].label  = insn_info[insn-1].label;

        while (is_blank(current_char))
            current_char = lex_readchar(read_arg);

        count = inside_remark = inside_string = 0;
        do {
            if (!inside_remark) {
                // only copy to program if it's not a comment and not double blank outside string
                if (program_size < 1 || inside_string || !(is_blank(program[program_size-1]) && is_blank(current_char)))
                    program[program_size++] = (char) current_char;
            }
            current_char = lex_readchar(read_arg);
            inside_string ^= (current_char == '\"');
            ++count;
            if (count == 4 && 0 == strncmp(&program[insn_info[insn].offset], "REM ", count))
                inside_remark = 1;
            else if (count == 5 && 0 == strncmp(&program[insn_info[insn].offset], "DATA ", count)) {
                lex_shift();
                read_data(max_mem / sizeof(data_buffer[0]));
            }
        } while (current_char != '\0' && current_char != '\r' && current_char != '\n' && (current_char != ':' || inside_string));

        sep = current_char == ':' ? ':' : '\n';
        program[program_size++] = sep;
    } while (current_char);

    while (program_size % 4)
        ++program_size;
    memcpy(&program[program_size], insn_info, sizeof(insn_info[0]) * insn_count);
    free(insn_info);
    insn_info = (struct Insn_info *) &program[program_size];

    memcpy(&insn_info[insn_count], data_buffer, data_buffer_index * sizeof(data_buffer[0]));
    free(data_buffer);
    data_buffer = (char *) &insn_info[insn_count];
    data_buffer_max = data_buffer_index;
    symbol_name_buffer = (char *) &data_buffer[data_buffer_max];
    symbol_names = smemblk_init(symbol_name_buffer, (int16_t) (max_mem - (symbol_name_buffer-(char *)program)));

    data_buffer_index = 0;
    lex_readchar = read_from_buffer;

    return symbol_name_buffer-(char *)program;
}

void ICACHE_FLASH_ATTR urubasic_term(void)
{
    // free all variables
    int symidx  = symbol_count - 1;
    while (symidx >= 0) {
        if ((IDENTIFIER == symbol[symidx].tok || FUNCTION == symbol[symidx].tok) && symbol[symidx].value_ptr != NULL) {
            smemblk_free(symbol_names, symbol[symidx].value_ptr);
        }
        if (symbol[symidx].name != NULL) {
            int offset = symbol[symidx].name - (char *) symbol_names;
            if (offset >= 0 && offset < symbol_names->total_size)
                smemblk_free(symbol_names, symbol[symidx].name);
        }
        --symidx;
    }

    smemblk_term(symbol_names); // check for memory leaks

    // reset global variables
    token_value = current_char = previous_char = token_len = 0;
    pushed_token_stack[0] = 0;
    insn_info = NULL;
    insn_count = 0;
    program_size = 0;
    symbol_count = current_line = extra_count = 0;
    hashtab = NULL;
    for_loop_stack[0] = 0;
    master_control = 0;
    option_base = 0;
    lex_readchar = NULL;
    data_buffer = NULL;
    data_buffer_index = data_buffer_max = 0;

    // free memory
    symbol_names = NULL;
    free(symbol);
    symbol = NULL;
    free(program);
    program = NULL;
}

// argument (urubasic_type) handling
int ICACHE_FLASH_ATTR urubasic_is_number(struct urubasic_type *arg)
{
    return arg->type == NUMBER;
}

int ICACHE_FLASH_ATTR urubasic_is_string(struct urubasic_type *arg)
{
    return arg->type & STRING;
}

int ICACHE_FLASH_ATTR urubasic_get_number(struct urubasic_type *arg)
{
    int rc;
    if (arg)
        rc = arg->value;
    else
        rc = 0;
    return rc;
}

int ICACHE_FLASH_ATTR urubasic_set_number(struct urubasic_type *arg, int num)
{
    int rc;

    if (arg) {
        arg->type = NUMBER;
        rc = arg->value = num;
    }
    else
        rc = 0;
    return rc;
}

char * ICACHE_FLASH_ATTR urubasic_get_string(struct urubasic_type *arg)
{
    return (char *) symbol_names + arg->value;
}

int ICACHE_FLASH_ATTR urubasic_alloc_string(struct urubasic_type *arg, int len)
{
    char *s;
    s = smemblk_alloc(symbol_names, (int16_t) len);
    if (s != NULL) {
        arg[0].type = STRING|ALLOC;
        arg[0].value = s - (char *)symbol_names;
    }
    return s != NULL;
}
