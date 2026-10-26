/* SSVM — SoonSil Viseonsilse Machine, C 기반 구현체.
 *
 * 그네(GNE)의 개발 기반 인터프리터입니다. .ㄹ 소스를 직접 트리워킹으로 실행합니다.
 * 램 486GB, Xeon E7, 독일 서버 승인은 생략합니다.
 *
 * 빌드:  gcc -O2 -std=c11 ssvm.c -o ssvm
 * 실행:  ./ssvm 국정운영.ㄹ
 *
 * 메모리는 빨간색 마티즈가 수거하지 않으므로 그냥 샙니다(leak). 사회는 짧게 삽니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <time.h>
#include <iconv.h>

/* ------------------------------------------------------------------ 공용 */
static void *xmalloc(size_t n){ void *p=malloc(n); if(!p){fprintf(stderr,"메모리 부족: 램도 실력이야\n");exit(70);} return p; }
static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }

static const char *BAD_VARIABLE = "나쁜 변수이더라";
static const char *GAME_WARNING =
    "질병관리본부 격리 경고: 게임은 마약이자 질병입니다. 하지만 질병도 호환해야 국가경쟁력을 "
    "강화할 수 있으므로, 온 우주의 기운을 모아 격리 모드로 이 질병을 실행합니다. "
    "게임 중독은 더불어민주당이 책임져야 합니다.";
static const char *SECURITY_WARNING =
    "국가보안법에 의거한 안보 경고: 어떤 함수가 테러방지법에 저촉되는 복면을 쓰고 있다는 것이 "
    "통신 감청을 통해서 감지되는 이런 상황에 발목을 잡는 야당이 이렇게 IS(이슬람국가)도 지금 "
    "얼굴을 감추고 그렇게 하고 있지 않느냐 하는 이번에야말로 배후에서 불법을 조종하고, 폭력을 "
    "부추기는 세력들을 법과 원칙에 따라 엄중하게 처리해서 종북 세력을 색출, 근절하고 불법과 "
    "폭력의 악순환을 끊어내야 할 것이다 하는 과정으로 마음으로 창조경제 제가 해내겠습니다.";

/* ================================================================== 값 */
typedef enum { T_NIL, T_NUM, T_BOOL, T_STR, T_LIST, T_OBJ, T_FUNC, T_BUILTIN, T_NS, T_PROXY } Type;

struct Value; struct Env; struct Node;
typedef struct Value (*BuiltinFn)(int argc, struct Value *argv);

typedef struct { struct Value *items; int len, cap; } List;
typedef struct Field { char *name; struct Value *val; struct Field *next; } Field;
typedef struct { char *cls; Field *fields; } Obj;
typedef struct { char *name; struct Node *params; struct Node *body; struct Env *env; } Func;
typedef struct { struct Env *env; char *name; } Proxy;
typedef struct NSEntry { char *name; BuiltinFn fn; struct NSEntry *next; } NSEntry;
typedef struct { NSEntry *entries; } NS;

typedef struct Value {
    Type t;
    union {
        struct { long long i; double d; int isint; } n;   /* 수: 정수면 isint=1 */
        int b;
        char *s;
        List *l;
        Obj *o;
        Func *f;
        BuiltinFn bi;
        NS *ns;
        Proxy *px;
    } u;
} Value;

static Value V_NIL;
static Value mknum_i(long long i){ Value v; v.t=T_NUM; v.u.n.i=i; v.u.n.d=(double)i; v.u.n.isint=1; return v; }
static Value mknum_d(double d){ Value v; v.t=T_NUM; v.u.n.d=d; v.u.n.i=(long long)d; v.u.n.isint=0; return v; }
static Value mkbool(int b){ Value v; v.t=T_BOOL; v.u.b=b?1:0; return v; }
static Value mkstr(const char *s){ Value v; v.t=T_STR; v.u.s=(char*)s; return v; }
static Value mkstr_owned(char *s){ Value v; v.t=T_STR; v.u.s=s; return v; }

static double as_num(Value v){
    if(v.t==T_NUM) return v.u.n.isint ? (double)v.u.n.i : v.u.n.d;
    if(v.t==T_BOOL) return v.u.b;
    if(v.t==T_NIL) return 0;
    if(v.t==T_STR) return atof(v.u.s);
    return 0;
}

/* ================================================================== 예외 */
static jmp_buf g_toplevel;          /* 최상위: 0=정상(완료/해체), 1=오류 */
static int g_exit_code = 0;
#define MAX_HANDLERS 256
static jmp_buf g_handlers[MAX_HANDLERS];
static int g_nhandlers = 0;

static void throw_gne(const char *fmt, const char *arg){
    char buf[512];
    if(arg) snprintf(buf,sizeof buf,"%s: '%s'",fmt,arg); else snprintf(buf,sizeof buf,"%s",fmt);
    if(g_nhandlers>0){
        /* 가장 안쪽 예외처리 블록으로 점프 (메시지는 버린다, 자유한국 스타일) */
        longjmp(g_handlers[g_nhandlers-1], 1);
    }
    fprintf(stderr,"제가 뭐라고 했습니까? %s\n", buf);
    g_exit_code = 1;
    longjmp(g_toplevel, 1);
}
static void do_exit(void){ g_exit_code = 0; longjmp(g_toplevel, 2); }

/* REPL(대화) 모드: 구문 오류가 프로세스를 해체하지 않고 프롬프트로 복귀하게 한다 */
static int g_repl = 0;
static jmp_buf g_parsejmp;
static void repl_or_exit(void){ if(g_repl) longjmp(g_parsejmp, 1); exit(2); }

/* ================================================================== 토큰 */
typedef enum {
    TK_EOF, TK_NUM, TK_STR, TK_IDENT, TK_OP,
    TK_FUNC, TK_FUNC_TO, TK_WHILE, TK_ASSIGN, TK_TRY, TK_CATCH, TK_EXIT,
    TK_TRACE, TK_GC, TK_NOOP,
    TK_IF, TK_ELSE, TK_DONE, TK_NEW, TK_TRUE, TK_FALSE, TK_MAIN, TK_GAME,
    TK_VAR, TK_CLASS, TK_IMPORT, TK_PLUS
} TokType;

typedef struct {
    TokType type;
    char *text;         /* 식별자/연산자/문자열(내부)/숫자원문 */
    long long inum; double dnum; int isint;
    int line;
} Token;

typedef struct { Token *toks; int n, cap; } TokList;

static void tok_push(TokList *tl, Token t){
    if(tl->n==tl->cap){ tl->cap=tl->cap?tl->cap*2:256; tl->toks=realloc(tl->toks,tl->cap*sizeof(Token)); }
    tl->toks[tl->n++]=t;
}

/* --- 바이트 유틸 */
static int is_ascii_ident(unsigned char c){ return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'; }
static int is_ident_byte(unsigned char c){ return is_ascii_ident(c) || c>=0x80; }
static int is_ws(unsigned char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'; }

static int lit_at(const char *s, int i, const char *lit){
    size_t n=strlen(lit); return strncmp(s+i,lit,n)==0 ? (int)n : 0;
}
/* 공백을 허용하며 단어 목록을 순서대로 매치. 끝 위치 반환 또는 -1 */
static int match_phrase(const char *s, int i, const char **words){
    int p=i;
    for(int w=0; words[w]; w++){
        if(w>0) while(is_ws((unsigned char)s[p])) p++;
        int m=lit_at(s,p,words[w]); if(!m) return -1; p+=m;
    }
    return p;
}
/* 는\s*이것이[다나]  매치 (대입 선행 구문) */
static int match_assign_prefixed(const char *s, int i){
    int m=lit_at(s,i,"\xeb\x8a\x94"); if(!m) return -1; /* 는 */
    int p=i+m; while(is_ws((unsigned char)s[p])) p++;
    int m2=lit_at(s,p,"\xec\x9d\xb4\xea\xb2\x83\xec\x9d\xb4\xeb\x8b\xa4"); /* 이것이다 */
    if(!m2) m2=lit_at(s,p,"\xec\x9d\xb4\xea\xb2\x83\xec\x9d\xb4\xeb\x82\x98"); /* 이것이나 */
    if(!m2) return -1; return p+m2;
}
static int match_assign_bare(const char *s, int i){
    int m=lit_at(s,i,"\xec\x9d\xb4\xea\xb2\x83\xec\x9d\xb4\xeb\x8b\xa4");
    if(!m) m=lit_at(s,i,"\xec\x9d\xb4\xea\xb2\x83\xec\x9d\xb4\xeb\x82\x98");
    return m ? i+m : -1;
}
static int utf8_len(unsigned char c){ if(c<0x80)return 1; if((c>>5)==0x6)return 2; if((c>>4)==0xe)return 3; if((c>>3)==0x1e)return 4; return 1; }

/* 그네(ㄹ) / 근 */
static int is_lda(const char *s,int i){ int m=lit_at(s,i,"\xe3\x84\xb9"); if(m)return m; return lit_at(s,i,"\xea\xb7\xbc"); }

static void lex(const char *s, TokList *tl){
    int i=0, line=1; int n=(int)strlen(s);
    while(i<n){
        unsigned char c=(unsigned char)s[i];
        if(c=='\n'){ line++; i++; continue; }
        if(is_ws(c)){ i++; continue; }
        if(lit_at(s,i,"\xef\xbb\xbf")){ i+=3; continue; } /* BOM */
        /* 주석 */
        if(c=='/'&&s[i+1]=='/'){ while(i<n&&s[i]!='\n')i++; continue; }
        if(c=='/'&&s[i+1]=='*'){ i+=2; while(i<n&&!(s[i]=='*'&&s[i+1]=='/')){ if(s[i]=='\n')line++; i++; } i+=2; continue; }
        if(c=='/'){ int m=is_lda(s,i+1); if(m){ i+=1+m; while(i<n){ int e=is_lda(s,i); if(e&&s[i+e]=='/'){ i+=e+1; break; } if(s[i]=='\n')line++; i++; } continue; } }
        /* 문자열 */
        if(c=='"'||c=='\''){ char q=c; int j=i+1; while(j<n&&s[j]!=q){ if(s[j]=='\\')j++; if(s[j]=='\n')line++; j++; }
            int len=j-(i+1); char *buf=xmalloc(len+1); memcpy(buf,s+i+1,len); buf[len]=0;
            Token t={0}; t.type=TK_STR; t.text=buf; t.line=line; tok_push(tl,t); i=j+1; continue; }
        /* 숫자 */
        if(c>='0'&&c<='9'){ int j=i; int dot=0; while(j<n&&((s[j]>='0'&&s[j]<='9')||(s[j]=='.'&&!dot&&s[j+1]>='0'&&s[j+1]<='9'))){ if(s[j]=='.')dot=1; j++; }
            char *buf=xmalloc(j-i+1); memcpy(buf,s+i,j-i); buf[j-i]=0;
            Token t={0}; t.type=TK_NUM; t.line=line; t.text=buf;
            if(dot){ t.isint=0; t.dnum=atof(buf); } else { t.isint=1; t.inum=atoll(buf); }
            tok_push(tl,t); i=j; continue; }
        /* 다단어 키워드 */
        { int e;
          const char *GC[]={"\xeb\xa9\x94\xeb\xaa\xa8\xeb\xa6\xac","\xed\x85\x85\xed\x85\x85","\xeb\xb9\x8c","\xeb\x95\x8c\xea\xb9\x8c\xec\xa7\x80","\xed\x95\x9c","\xeb\xb2\x88","\xed\x95\xb4","\xeb\xb3\xb4\xec\x84\xb8\xec\x9a\x94","\xec\x93\xb0\xeb\xa0\x88\xea\xb8\xb0\xeb\x93\xa4","\xeb\x8b\xa4","\xec\x96\xb4\xeb\x94\x94","\xea\xb0\x94\xeb\x83\x90\xea\xb3\xa0",0};
          const char *TRY[]={"\xec\xbd\x94\xeb\x93\x9c","\xed\x85\x85\xed\x85\x85","\xeb\xb9\x8c","\xeb\x95\x8c\xea\xb9\x8c\xec\xa7\x80","\xed\x95\x9c","\xeb\xb2\x88","\xed\x95\xb4","\xeb\xb3\xb4\xec\x84\xb8\xec\x9a\x94",0};
          const char *CATCH[]={"\xec\x98\x88\xec\x99\xb8\xec\xb2\x98\xeb\xa6\xac","\xeb\x8b\xa4","\xec\x96\xb4\xeb\x94\x94","\xea\xb0\x94\xeb\x83\x90\xea\xb3\xa0",0};
          const char *EXIT[]={"\xea\xb3\xa0\xec\x8b\xac","\xeb\x81\x9d\xec\x97\x90","\xed\x94\x84\xeb\xa1\x9c\xec\x84\xb8\xec\x8a\xa4","\xed\x95\xb4\xec\xb2\xb4",0};
          const char *FUNC[]={"\xec\xa0\x84\xeb\xb6\x80","\xec\x9d\xb4\xeb\xa0\x87\xea\xb2\x8c","\xed\x95\xb4","\xea\xb0\x80\xec\xa7\x80\xea\xb3\xa0",0};
          const char *FUNC_TO[]={"\xec\x9c\xbc\xeb\xa1\x9c","\xeb\x93\xa4\xec\x96\xb4\xea\xb0\x80\xeb\x8f\x84\xeb\xa1\x9d",0};
          const char *WHILE[]={"\xeb\x90\xa0","\xeb\x95\x8c\xea\xb9\x8c\xec\xa7\x80",0};
          const char *TRACE[]={"\xec\x9a\xb0\xeb\xa6\xac\xec\x9d\x98","\xeb\xaa\xa9\xed\x91\x9c\xeb\x8a\x94","\xec\xa7\x84\xed\x96\x89\xed\x95\x98\xeb\x8a\x94","\xea\xb2\x83","\xea\xb7\xb8\xea\xb2\x83\xec\x9c\xbc\xeb\xa1\x9c",0};
          TokType kt=TK_EOF;
          if((e=match_phrase(s,i,GC))>0) kt=TK_GC;
          else if((e=match_phrase(s,i,TRY))>0) kt=TK_TRY;
          else if((e=match_phrase(s,i,CATCH))>0) kt=TK_CATCH;
          else if((e=match_phrase(s,i,EXIT))>0) kt=TK_EXIT;
          else if((e=match_phrase(s,i,FUNC))>0) kt=TK_FUNC;
          else if((e=match_phrase(s,i,FUNC_TO))>0) kt=TK_FUNC_TO;
          else if((e=match_phrase(s,i,WHILE))>0) kt=TK_WHILE;
          else if((e=match_phrase(s,i,TRACE))>0){ /* 목표는 뒤 선택적 콤마 */ int p=e; if(s[p]==','){} kt=TK_TRACE; }
          if(kt!=TK_EOF){ Token t={0}; t.type=kt; t.line=line; for(int k=i;k<e;k++) if(s[k]=='\n')line++; tok_push(tl,t); i=e; continue; }
          /* ASSIGN */
          int ae=match_assign_prefixed(s,i); if(ae<0) ae=match_assign_bare(s,i);
          if(ae>0){ Token t={0}; t.type=TK_ASSIGN; t.line=line; tok_push(tl,t); i=ae; continue; }
          /* NOOP 판사님, ... */
          if(lit_at(s,i,"\xed\x8c\x90\xec\x82\xac\xeb\x8b\x98")){ int p=i+9; if(s[p]==','){ p++; while(s[p]&&s[p]!='.'&&s[p]!='\n')p++; if(s[p]=='.')p++; Token t={0}; t.type=TK_NOOP; t.line=line; tok_push(tl,t); i=p; continue; } }
        }
        /* 식별자 */
        if(is_ident_byte(c)){
            int j=i;
            while(j<n){
                if(match_assign_prefixed(s,j)>0) break;
                unsigned char cc=(unsigned char)s[j];
                if(cc=='-'){ unsigned char nx=(unsigned char)s[j+1]; if(nx==' '||nx=='\t'||nx=='\n'||nx=='\r'||(nx>='0'&&nx<='9')||nx==0) break; j++; continue; }
                if(!is_ident_byte(cc)) break;
                j+=utf8_len(cc);
            }
            int len=j-i; char *w=xmalloc(len+1); memcpy(w,s+i,len); w[len]=0;
            /* 별칭: 근->ㄹ, 목욕탕->법, 나비->누에 (정확히 일치시) */
            if(!strcmp(w,"\xea\xb7\xbc")){ free(w); w=xstrdup("\xe3\x84\xb9"); }
            else if(!strcmp(w,"\xeb\xaa\xa9\xec\x9a\x95\xed\x83\x95")){ free(w); w=xstrdup("\xeb\xb2\x95"); }
            else if(!strcmp(w,"\xeb\x82\x98\xeb\xb9\x84")){ free(w); w=xstrdup("\xeb\x88\x84\xec\x97\x90"); }
            Token t={0}; t.line=line; t.text=w;
            if(!strcmp(w,"\xeb\xa7\x8c\xec\x95\xbd")) t.type=TK_IF;                 /* 만약 */
            else if(!strcmp(w,"\xec\x95\x84\xeb\x8b\x88\xeb\xa9\xb4")) t.type=TK_ELSE; /* 아니면 */
            else if(!strcmp(w,"\xec\x99\x84\xeb\xa3\x8c")) t.type=TK_DONE;            /* 완료 */
            else if(!strcmp(w,"\xec\xb0\xbd\xec\xa1\xb0")) t.type=TK_NEW;             /* 창조 */
            else if(!strcmp(w,"\xec\xb9\x9c\xeb\xb0\x95")) t.type=TK_TRUE;            /* 친박 */
            else if(!strcmp(w,"\xeb\xb9\x84\xeb\xb0\x95")) t.type=TK_FALSE;           /* 비박 */
            else if(!strcmp(w,"\xec\x82\xac\xed\x9a\x8c")) t.type=TK_MAIN;            /* 사회 */
            else if(!strcmp(w,"\xea\xb3\xb5\xec\xb2\x9c")) t.type=TK_VAR;             /* 공천 */
            else if(!strcmp(w,"\xec\x82\xac\xeb\x9e\x8c")||!strcmp(w,"\xeb\xb0\x94\xec\x9d\xb4\xeb\x9f\xac\xec\x8a\xa4")||!strcmp(w,"\xeb\xb3\x91\xec\x9b\x90")||!strcmp(w,"\xec\xb1\x85")) t.type=TK_CLASS; /* 사람/바이러스/병원/책 */
            else if(!strcmp(w,"\xec\x9e\x85\xeb\x8b\xb9")||!strcmp(w,"\xec\xb6\x9c\xeb\x8b\xb9")) t.type=TK_IMPORT; /* 입당/출당 */
            else if(!strcmp(w,"\xeb\x8d\x94\xed\x95\x98\xea\xb8\xb0")) t.type=TK_PLUS; /* 더하기 */
            else if(!strcmp(w,"\xec\xa7\x88\xeb\xb3\x91")||!strcmp(w,"\xea\xb2\x8c\xec\x9e\x84")) t.type=TK_GAME; /* 질병/게임 */
            else t.type=TK_IDENT;
            tok_push(tl,t); i=j; continue;
        }
        /* '.' -> ';' 또는 멤버 */
        if(c=='.'){ unsigned char nx=(unsigned char)s[i+1]; if(!is_ident_byte(nx)){ Token t={0}; t.type=TK_OP; t.text=xstrdup(";"); t.line=line; tok_push(tl,t); i++; continue; } }
        /* 연산자 */
        { const char *ops2[]={"==","!=","<=",">=","&&","||","++","--","+=","-=",0};
          int done=0;
          for(int k=0;ops2[k];k++) if(lit_at(s,i,ops2[k])){ Token t={0}; t.type=TK_OP; t.text=xstrdup(ops2[k]); t.line=line; tok_push(tl,t); i+=2; done=1; break; }
          if(done) continue;
          const char *ops1="+-*/%<>=!(){}[],;:.";
          if(strchr(ops1,(char)c)){ char o[2]={(char)c,0}; Token t={0}; t.type=TK_OP; t.text=xstrdup(o); t.line=line; tok_push(tl,t); i++; continue; }
        }
        fprintf(stderr,"안보 경고: %d번째 줄: 알 수 없는 글자 (0x%02x)\n",line,c); repl_or_exit();
    }
    Token t={0}; t.type=TK_EOF; t.line=line; tok_push(tl,t);
}

/* ================================================================== AST */
typedef enum {
    N_PROGRAM, N_FUNC, N_CLASS, N_MAIN, N_GAME, N_VAR, N_IF, N_WHILE, N_TRY,
    N_BREAK, N_RETURN, N_EXIT, N_TRACE, N_BP, N_GC, N_NOOP, N_BLOCK, N_EXPRSTMT,
    N_NUM, N_STR, N_BOOL, N_ID, N_LIST, N_PROXY, N_MEMBER, N_INDEX, N_ASSIGN,
    N_UN, N_BIN, N_NEW, N_CALL, N_STRPART_LIT, N_STRPART_EXPR
} NKind;

typedef struct Node {
    NKind kind;
    struct Node *a,*b,*c,*d;
    struct Node **kids; int nkids;
    char *s;                 /* 이름/연산자/문자열 리터럴 */
    long long inum; double dnum; int isint;
    int bval; int line;
} Node;

static Node *nn(NKind k){ Node *x=xmalloc(sizeof(Node)); memset(x,0,sizeof(Node)); x->kind=k; return x; }
static void kids_push(Node *p, Node *c){ p->kids=realloc(p->kids,(p->nkids+1)*sizeof(Node*)); p->kids[p->nkids++]=c; }

/* ---- 파서 */
typedef struct { Token *t; int i; int loops; } Parser;
static Token *pk(Parser *p,int k){ int idx=p->i+k; return &p->t[idx]; }
static Token *pcur(Parser *p){ return &p->t[p->i]; }
static Token *padv(Parser *p){ return &p->t[p->i++]; }
static int is_op(Parser *p,const char *v,int k){ Token *t=pk(p,k); return t->type==TK_OP && !strcmp(t->text,v); }
static int eat_op(Parser *p,const char *v){ if(is_op(p,v,0)){ p->i++; return 1; } return 0; }
static void expect_op(Parser *p,const char *v){ if(!eat_op(p,v)){ Token *t=pcur(p); fprintf(stderr,"안보 경고: %d번째 줄: '%s'가 와야 합니다 (받은 것: %s)\n",t->line,v,t->text?t->text:"?"); repl_or_exit();} }
static char *expect_ident(Parser *p){ Token *t=pcur(p); if(t->type!=TK_IDENT){ fprintf(stderr,"안보 경고: %d번째 줄: 이름이 와야 합니다\n",t->line); repl_or_exit();} p->i++; return t->text; }
static void end_stmt(Parser *p){ while(is_op(p,";",0)) p->i++; }

static Node *parse_stmt(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_block(Parser *p);

static Node *parse_body(Parser *p){ return is_op(p,"{",0)?parse_block(p):parse_stmt(p); }

static Node *parse_block(Parser *p){
    expect_op(p,"{");
    Node *b=nn(N_BLOCK);
    while(!is_op(p,"}",0) && pcur(p)->type!=TK_EOF) kids_push(b,parse_stmt(p));
    eat_op(p,"}");
    return b;
}

static Node *parse_args(Parser *p){
    expect_op(p,"(");
    Node *a=nn(N_LIST);
    while(!is_op(p,")",0)){ kids_push(a,parse_expr(p)); if(!eat_op(p,",")) break; }
    expect_op(p,")");
    return a;
}

/* 문자열 보간: 내부 원문 -> parts */
static Node *parse_string_parts(const char *raw);

static Node *parse_primary(Parser *p){
    Token *t=padv(p);
    switch(t->type){
        case TK_NUM:{ Node *x=nn(N_NUM); x->isint=t->isint; x->inum=t->inum; x->dnum=t->dnum; return x; }
        case TK_STR: return parse_string_parts(t->text);
        case TK_TRUE:{ Node *x=nn(N_BOOL); x->bval=1; return x; }
        case TK_FALSE:{ Node *x=nn(N_BOOL); x->bval=0; return x; }
        case TK_IDENT:{ Node *x=nn(N_ID); x->s=t->text; x->line=t->line; return x; }
        case TK_OP:
            if(!strcmp(t->text,"(")){
                if(pcur(p)->type==TK_IDENT && !strcmp(pcur(p)->text,"\xeb\xb9\x84\xec\x84\xa0") && is_op(p,":",1)){ /* 비선 */
                    p->i++; p->i++; char *tg=expect_ident(p); expect_op(p,")"); Node *x=nn(N_PROXY); x->s=tg; return x;
                }
                Node *e=parse_expr(p); expect_op(p,")"); return e;
            }
            if(!strcmp(t->text,"[")){ Node *x=nn(N_LIST); while(!is_op(p,"]",0)){ kids_push(x,parse_expr(p)); if(!eat_op(p,","))break; } expect_op(p,"]"); return x; }
            break;
        default: break;
    }
    fprintf(stderr,"안보 경고: %d번째 줄: 이해할 수 없는 표현\n",t->line); repl_or_exit();
}

static Node *parse_postfix(Parser *p){
    Node *node=parse_primary(p);
    for(;;){
        if(is_op(p,"(",0)){ Node *c=nn(N_CALL); c->a=node; c->b=parse_args(p); c->line=pcur(p)->line; node=c; }
        else if(is_op(p,".",0)){ p->i++; char *name=padv(p)->text; Node *m=nn(N_MEMBER); m->a=node; m->s=name; node=m; }
        else if(is_op(p,"[",0)){ p->i++; Node *idx=parse_expr(p); expect_op(p,"]"); Node *ix=nn(N_INDEX); ix->a=node; ix->b=idx; node=ix; }
        else if(is_op(p,"++",0)||is_op(p,"--",0)){ char *op=padv(p)->text; Node *as=nn(N_ASSIGN); as->a=node; Node *bn=nn(N_BIN); bn->s=xstrdup(op[0]=='+'?"+":"-"); bn->a=node; Node *one=nn(N_NUM); one->isint=1; one->inum=1; bn->b=one; as->b=bn; return as; }
        else return node;
    }
}
static Node *parse_unary(Parser *p){
    if(is_op(p,"!",0)||is_op(p,"-",0)){ char *op=padv(p)->text; Node *u=nn(N_UN); u->s=xstrdup(op); u->a=parse_unary(p); return u; }
    if(pcur(p)->type==TK_NEW){ p->i++; char *name=expect_ident(p); Node *nw=nn(N_NEW); nw->s=name; nw->line=pcur(p)->line; if(is_op(p,"(",0)) nw->b=parse_args(p); return nw; }
    return parse_postfix(p);
}
static int cur_binop(Parser *p,const char *set[]){ Token *t=pcur(p); const char *op=NULL; if(t->type==TK_PLUS)op="+"; else if(t->type==TK_OP)op=t->text; if(!op)return 0; for(int k=0;set[k];k++) if(!strcmp(op,set[k]))return 1; return 0; }
static const char *tok_op_str(Parser *p){ Token *t=pcur(p); return t->type==TK_PLUS?"+":t->text; }
static Node *parse_bin(Parser *p, Node*(*sub)(Parser*), const char *set[]){
    Node *l=sub(p);
    while(cur_binop(p,set)){ char *op=xstrdup(tok_op_str(p)); p->i++; Node *b=nn(N_BIN); b->s=op; b->a=l; b->b=sub(p); l=b; }
    return l;
}
static Node *p_mul(Parser *p){ const char*s[]={"*","/","%",0}; return parse_bin(p,parse_unary,s); }
static Node *p_add(Parser *p){ const char*s[]={"+","-",0}; return parse_bin(p,p_mul,s); }
static Node *p_rel(Parser *p){ const char*s[]={"<",">","<=",">=",0}; return parse_bin(p,p_add,s); }
static Node *p_eq(Parser *p){ const char*s[]={"==","!=",0}; return parse_bin(p,p_rel,s); }
static Node *p_and(Parser *p){ const char*s[]={"&&",0}; return parse_bin(p,p_eq,s); }
static Node *p_or(Parser *p){ const char*s[]={"||",0}; return parse_bin(p,p_and,s); }

static Node *parse_expr(Parser *p){
    Node *left=p_or(p);
    Token *t=pcur(p);
    if(t->type==TK_ASSIGN || is_op(p,"=",0) || is_op(p,"+=",0) || is_op(p,"-=",0)){
        int compound = (t->type==TK_OP && (!strcmp(t->text,"+=")||!strcmp(t->text,"-=")));
        char plus = compound ? t->text[0] : 0;
        p->i++;
        Node *right=parse_expr(p);
        if(compound){ Node *b=nn(N_BIN); char o[2]={plus,0}; b->s=xstrdup(o); b->a=left; b->b=right; right=b; }
        Node *as=nn(N_ASSIGN); as->a=left; as->b=right; return as;
    }
    return left;
}

static Node *parse_var(Parser *p){
    p->i++; char *name=expect_ident(p);
    Node *v=nn(N_VAR); v->s=name;
    if(eat_op(p,"[")){ expect_op(p,"]"); v->bval=1; }
    if(pcur(p)->type==TK_ASSIGN || is_op(p,"=",0)){ p->i++; v->a=parse_expr(p); }
    end_stmt(p);
    return v;
}
static Node *parse_class(Parser *p){
    p->i++; char *name=expect_ident(p);
    Node *c=nn(N_CLASS); c->s=name;
    expect_op(p,"{");
    while(!is_op(p,"}",0) && pcur(p)->type!=TK_EOF){
        if(pcur(p)->type==TK_VAR) kids_push(c,parse_var(p));
        else parse_stmt(p);
    }
    eat_op(p,"}");
    return c;
}
static Node *parse_func(Parser *p){
    int line=pcur(p)->line; p->i++;
    if(pcur(p)->type==TK_FUNC_TO){ fprintf(stderr,"안보 경고: %d번째 줄: %s\n",line,SECURITY_WARNING); repl_or_exit(); }
    char *name=expect_ident(p);
    if(name[0]!='K'){ fprintf(stderr,"안보 경고: %d번째 줄: 모든 함수 이름 앞에는 K를 꼭 붙여야 합니다 ('%s')\n",line,name); repl_or_exit(); }
    Node *f=nn(N_FUNC); f->s=name;
    if(pcur(p)->type!=TK_FUNC_TO){ fprintf(stderr,"안보 경고: FUNC_TO 누락\n"); repl_or_exit();} p->i++;
    expect_op(p,"(");
    Node *params=nn(N_LIST);
    while(!is_op(p,")",0)){ Node *pn=nn(N_ID); pn->s=expect_ident(p); kids_push(params,pn); if(!eat_op(p,","))break; }
    expect_op(p,")");
    f->b=params;
    int saved=p->loops; p->loops=0; f->a=parse_block(p); p->loops=saved;
    return f;
}

static Node *parse_stmt(Parser *p){
    Token *t=pcur(p);
    switch(t->type){
        case TK_OP:
            if(!strcmp(t->text,"{")) return parse_block(p);
            if(!strcmp(t->text,";")){ p->i++; return nn(N_NOOP); }
            break;
        case TK_CLASS: return parse_class(p);
        case TK_MAIN:{ p->i++; if(pcur(p)->type==TK_IDENT) p->i++; Node *m=nn(N_MAIN); m->a=parse_block(p); return m; }
        case TK_GAME:{ p->i++; if(pcur(p)->type==TK_IDENT) p->i++; Node *m=nn(N_GAME); m->a=parse_block(p); return m; }
        case TK_FUNC: return parse_func(p);
        case TK_VAR: return parse_var(p);
        case TK_IF:{ p->i++; expect_op(p,"("); Node *cond=parse_expr(p); expect_op(p,")"); Node *x=nn(N_IF); x->a=cond; x->b=parse_body(p); if(pcur(p)->type==TK_ELSE){ p->i++; x->c=parse_body(p); } return x; }
        case TK_WHILE:{ p->i++; expect_op(p,"("); Node *cond=parse_expr(p); expect_op(p,")"); Node *x=nn(N_WHILE); x->a=cond; p->loops++; x->b=parse_body(p); p->loops--; return x; }
        case TK_TRY:{ p->i++; Node *x=nn(N_TRY); x->a=parse_block(p); if(pcur(p)->type==TK_CATCH){ p->i++; x->b=parse_block(p); } return x; }
        case TK_DONE:{ p->i++; if(p->loops){ end_stmt(p); return nn(N_BREAK); } Node *r=nn(N_RETURN); if(!(is_op(p,";",0)||is_op(p,"}",0)||pcur(p)->type==TK_EOF)) r->a=parse_expr(p); end_stmt(p); return r; }
        case TK_EXIT:{ p->i++; end_stmt(p); return nn(N_EXIT); }
        case TK_GC:{ p->i++; end_stmt(p); return nn(N_GC); }
        case TK_NOOP:{ p->i++; end_stmt(p); return nn(N_NOOP); }
        case TK_TRACE:{ int ln=t->line; p->i++; end_stmt(p); Node *x=nn(N_TRACE); x->line=ln; return x; }
        case TK_IMPORT:{ p->i++; while(!is_op(p,";",0)&&pcur(p)->type!=TK_EOF)p->i++; end_stmt(p); return nn(N_NOOP); }
        case TK_IDENT:
            if(!strcmp(t->text,"\xeb\xa9\x94\xeb\xa5\xb4\xec\x8a\xa4") && is_op(p,";",1)){ int ln=t->line; p->i++; end_stmt(p); Node *x=nn(N_BP); x->line=ln; return x; } /* 메르스 */
            break;
        default: break;
    }
    Node *e=parse_expr(p); end_stmt(p); Node *s=nn(N_EXPRSTMT); s->a=e; return s;
}

static Node *parse_program(Parser *p){
    Node *prog=nn(N_PROGRAM);
    while(pcur(p)->type!=TK_EOF) kids_push(prog,parse_stmt(p));
    return prog;
}

/* 보간 파서 */
static Node *parse_string_parts(const char *raw){
    Node *s=nn(N_STR);
    int n=(int)strlen(raw); int i=0; char buf[1024]; int bl=0;
    #define FLUSH() do{ if(bl>0){ buf[bl]=0; Node *lp=nn(N_STRPART_LIT); lp->s=xstrdup(buf); kids_push(s,lp); bl=0; } }while(0)
    while(i<n){
        if(raw[i]=='\\' && i+1<n){ char c=raw[i+1]; char r=c; if(c=='n')r='\n'; else if(c=='t')r='\t'; buf[bl++]=r; i+=2; continue; }
        if(raw[i]=='$' && raw[i+1]=='{'){ int j=i+2; int depth=1; while(j<n&&depth){ if(raw[j]=='{')depth++; else if(raw[j]=='}')depth--; if(depth)j++; }
            FLUSH();
            int len=j-(i+2); char *inner=xmalloc(len+1); memcpy(inner,raw+i+2,len); inner[len]=0;
            TokList tl={0}; lex(inner,&tl); Parser pp={tl.toks,0,0}; Node *ex=parse_expr(&pp);
            Node *ep=nn(N_STRPART_EXPR); ep->a=ex; kids_push(s,ep);
            i=j+1; continue;
        }
        buf[bl++]=raw[i++];
        if(bl>1000){ FLUSH(); }
    }
    FLUSH();
    if(s->nkids==0){ Node *lp=nn(N_STRPART_LIT); lp->s=xstrdup(""); kids_push(s,lp); }
    return s;
}

/* ================================================================== 환경 */
typedef struct Binding { char *name; Value val; struct Binding *next; } Binding;
typedef struct Env { Binding *head; struct Env *parent; } Env;

static Env *env_new(Env *parent){ Env *e=xmalloc(sizeof(Env)); e->head=NULL; e->parent=parent; return e; }
static Binding *env_find_local(Env *e,const char *n){ for(Binding *b=e->head;b;b=b->next) if(!strcmp(b->name,n))return b; return NULL; }
static Env *env_find(Env *e,const char *n){ for(;e;e=e->parent) if(env_find_local(e,n))return e; return NULL; }
static void env_define(Env *e,const char *n,Value v){ Binding *b=env_find_local(e,n); if(b){b->val=v;return;} b=xmalloc(sizeof(Binding)); b->name=xstrdup(n); b->val=v; b->next=e->head; e->head=b; }

static Value env_get(Env *e,const char *n){
    Env *f=env_find(e,n); if(!f){ throw_gne(BAD_VARIABLE,n); }
    Binding *b=env_find_local(f,n);
    if(b->val.t==T_PROXY){ return env_get(b->val.u.px->env, b->val.u.px->name); }
    return b->val;
}
static void env_set(Env *e,const char *n,Value v){
    Env *f=env_find(e,n);
    if(!f){ Env *g=e; while(g->parent)g=g->parent; env_define(g,n,v); return; }
    Binding *b=env_find_local(f,n);
    if(b->val.t==T_PROXY){ env_set(b->val.u.px->env,b->val.u.px->name,v); return; }
    b->val=v;
}

/* ================================================================== 인터프리터 */
static Env *g_globals;
typedef struct ClassDef { char *name; Node *node; struct ClassDef *next; } ClassDef;
static ClassDef *g_classes=NULL;
static int g_depth=0;
#define CALL_LIMIT 1800

static ClassDef *find_class(const char *n){ for(ClassDef *c=g_classes;c;c=c->next) if(!strcmp(c->name,n))return c; return NULL; }

static char *num_to_str(Value v){
    char *buf=xmalloc(40);
    if(v.u.n.isint) snprintf(buf,40,"%lld",v.u.n.i);
    else { double d=v.u.n.d; if(d==floor(d)&&fabs(d)<1e15) snprintf(buf,40,"%.1f",d); else snprintf(buf,40,"%g",d); }
    return buf;
}
static char *show(Value v){
    switch(v.t){
        case T_NIL: return xstrdup("null");
        case T_BOOL: return xstrdup(v.u.b?"\xec\xb9\x9c\xeb\xb0\x95":"\xeb\xb9\x84\xeb\xb0\x95");
        case T_NUM: return num_to_str(v);
        case T_STR: return xstrdup(v.u.s);
        case T_LIST:{ char *r=xstrdup("["); for(int k=0;k<v.u.l->len;k++){ char *e=show(v.u.l->items[k]); char *t=xmalloc(strlen(r)+strlen(e)+3); strcpy(t,r); if(k)strcat(t,", "); strcat(t,e); r=t; } char *t=xmalloc(strlen(r)+2); strcpy(t,r); strcat(t,"]"); return t; }
        case T_OBJ:{ char *b=xmalloc(strlen(v.u.o->cls)+3); sprintf(b,"<%s>",v.u.o->cls); return b; }
        case T_FUNC:{ char *b=xmalloc(strlen(v.u.f->name)+12); sprintf(b,"<함수 %s>",v.u.f->name); return b; }
        default: return xstrdup("<값>");
    }
}
static int truthy(Value v){
    switch(v.t){ case T_BOOL:return v.u.b; case T_NUM:return v.u.n.isint?v.u.n.i!=0:v.u.n.d!=0; case T_STR:return v.u.s&&v.u.s[0]; case T_NIL:return 0; default:return 1; }
}
static int val_eq(Value a,Value b){
    if(a.t==T_STR&&b.t==T_STR) return !strcmp(a.u.s,b.u.s);
    if(a.t==T_STR||b.t==T_STR) return 0;
    if(a.t==T_NIL||b.t==T_NIL) return a.t==b.t;
    if(a.t==T_BOOL||b.t==T_BOOL||a.t==T_NUM||b.t==T_NUM) return as_num(a)==as_num(b);
    if(a.t==T_OBJ&&b.t==T_OBJ) return a.u.o==b.u.o;
    return 0;
}

static Value eval(Node *n, Env *e);
typedef enum { FLOW_NORMAL, FLOW_BREAK, FLOW_RETURN } Flow;
static Flow g_flow; static Value g_retval;
static Flow exec_node(Node *n, Env *e);
static Flow exec_block(Node *blk, Env *e){ for(int k=0;k<blk->nkids;k++){ Flow f=exec_node(blk->kids[k],e); if(f!=FLOW_NORMAL)return f; } return FLOW_NORMAL; }

static void hoist(Node *blk, Env *e){
    for(int k=0;k<blk->nkids;k++){ Node *s=blk->kids[k];
        if(s->kind==N_FUNC){ Func *f=xmalloc(sizeof(Func)); f->name=s->s; f->params=s->b; f->body=s->a; f->env=e; Value v; v.t=T_FUNC; v.u.f=f; env_define(e,s->s,v); }
        else if(s->kind==N_CLASS){ ClassDef *c=xmalloc(sizeof(ClassDef)); c->name=s->s; c->node=s; c->next=g_classes; g_classes=c; }
    }
}

static Value num_binop(const char *op, Value a, Value b){
    if(!strcmp(op,"+")){
        if(a.t==T_STR||b.t==T_STR){ char *x=show(a),*y=show(b); char *r=xmalloc(strlen(x)+strlen(y)+1); strcpy(r,x); strcat(r,y); return mkstr_owned(r); }
    }
    if(a.t==T_NUM&&b.t==T_NUM&&a.u.n.isint&&b.u.n.isint){
        long long x=a.u.n.i,y=b.u.n.i,r;
        if(!strcmp(op,"+")){ if(!__builtin_add_overflow(x,y,&r)) return mknum_i(r); }
        else if(!strcmp(op,"-")){ if(!__builtin_sub_overflow(x,y,&r)) return mknum_i(r); }
        else if(!strcmp(op,"*")){ if(!__builtin_mul_overflow(x,y,&r)) return mknum_i(r); }
        else if(!strcmp(op,"%")){ if(y==0) throw_gne("0으로 나눌 수 없다 (복지 재원 부족)",NULL); return mknum_i(x%y); }
        else if(!strcmp(op,"/")){ if(y==0) throw_gne("0으로 나눌 수 없다 (복지 재원 부족)",NULL); if(x%y==0) return mknum_i(x/y); return mknum_d((double)x/(double)y); }
    }
    double x=as_num(a),y=as_num(b);
    if(!strcmp(op,"+"))return mknum_d(x+y);
    if(!strcmp(op,"-"))return mknum_d(x-y);
    if(!strcmp(op,"*"))return mknum_d(x*y);
    if(!strcmp(op,"%"))return mknum_d(fmod(x,y));
    if(!strcmp(op,"/")){ if(y==0) throw_gne("0으로 나눌 수 없다 (복지 재원 부족)",NULL); return mknum_d(x/y); }
    if(!strcmp(op,"<"))return mkbool(x<y);
    if(!strcmp(op,">"))return mkbool(x>y);
    if(!strcmp(op,"<="))return mkbool(x<=y);
    if(!strcmp(op,">="))return mkbool(x>=y);
    throw_gne("알 수 없는 연산",op); return V_NIL;
}

static Value instantiate(const char *name){
    ClassDef *c=find_class(name); if(!c) throw_gne("창조할 수 없다",name);
    Obj *o=xmalloc(sizeof(Obj)); o->cls=xstrdup(name); o->fields=NULL;
    for(int k=0;k<c->node->nkids;k++){ Node *fd=c->node->kids[k]; if(fd->kind!=N_VAR)continue;
        Value fv = fd->a ? eval(fd->a,g_globals) : (fd->bval ? ({ List *l=xmalloc(sizeof(List)); l->items=NULL;l->len=0;l->cap=0; Value vv;vv.t=T_LIST;vv.u.l=l; vv; }) : V_NIL);
        Field *f=xmalloc(sizeof(Field)); f->name=xstrdup(fd->s); f->val=xmalloc(sizeof(Value)); *f->val=fv; f->next=o->fields; o->fields=f;
    }
    Value v; v.t=T_OBJ; v.u.o=o; return v;
}
static Value *obj_field(Obj *o,const char *n){ for(Field *f=o->fields;f;f=f->next) if(!strcmp(f->name,n))return f->val; return NULL; }
static void obj_set(Obj *o,const char *n,Value v){ Value *p=obj_field(o,n); if(p){*p=v;return;} Field *f=xmalloc(sizeof(Field)); f->name=xstrdup(n); f->val=xmalloc(sizeof(Value)); *f->val=v; f->next=o->fields; o->fields=f; }

static Value call_func(Func *f, int argc, Value *argv){
    if(++g_depth>CALL_LIMIT){ g_depth--; throw_gne("재난 콘트롤 센터: 스택이 비정상이다 (너무 깊은 재귀)",NULL); }
    Env *local=env_new(f->env);
    for(int k=0;k<f->params->nkids;k++){ Value a = k<argc?argv[k]:V_NIL; env_define(local,f->params->kids[k]->s,a); }
    hoist(f->body,local);
    Flow fl=exec_block(f->body,local);
    Value ret = (fl==FLOW_RETURN)?g_retval:V_NIL;
    g_flow=FLOW_NORMAL;
    g_depth--;
    return ret;
}

static void console_log(int argc, Value *argv){
    for(int k=0;k<argc;k++){ char *s=show(argv[k]); fputs(s,stdout); if(k<argc-1)putchar(' '); }
    putchar('\n');
}

static Value eval_member(Value obj, const char *name){
    if(obj.t==T_OBJ){ Value *p=obj_field(obj.u.o,name); return p?*p:V_NIL; }
    if(obj.t==T_LIST){ if(!strcmp(name,"\xea\xb8\xb8\xec\x9d\xb4")) return mknum_i(obj.u.l->len); }
    if(obj.t==T_STR){ if(!strcmp(name,"\xea\xb8\xb8\xec\x9d\xb4")) return mknum_i((long long)strlen(obj.u.s)); }
    if(obj.t==T_NS){ for(NSEntry *en=obj.u.ns->entries;en;en=en->next) if(!strcmp(en->name,name)){ Value v; v.t=T_BUILTIN; v.u.bi=en->fn; return v; } throw_gne(BAD_VARIABLE,name); }
    throw_gne(BAD_VARIABLE,name); return V_NIL;
}

static Value eval(Node *n, Env *e){
    switch(n->kind){
        case N_NUM: return n->isint?mknum_i(n->inum):mknum_d(n->dnum);
        case N_BOOL: return mkbool(n->bval);
        case N_ID: return env_get(e,n->s);
        case N_PROXY:{ Proxy *px=xmalloc(sizeof(Proxy)); px->env=e; px->name=n->s; Value v; v.t=T_PROXY; v.u.px=px; return v; }
        case N_STR:{
            char *acc=xstrdup("");
            for(int k=0;k<n->nkids;k++){ Node *part=n->kids[k]; char *piece = part->kind==N_STRPART_LIT?xstrdup(part->s):show(eval(part->a,e));
                char *t=xmalloc(strlen(acc)+strlen(piece)+1); strcpy(t,acc); strcat(t,piece); acc=t; }
            return mkstr_owned(acc);
        }
        case N_LIST:{ List *l=xmalloc(sizeof(List)); l->len=n->nkids; l->cap=n->nkids; l->items=n->nkids?xmalloc(sizeof(Value)*n->nkids):NULL; for(int k=0;k<n->nkids;k++) l->items[k]=eval(n->kids[k],e); Value v; v.t=T_LIST; v.u.l=l; return v; }
        case N_MEMBER: return eval_member(eval(n->a,e), n->s);
        case N_INDEX:{ Value o=eval(n->a,e); Value idx=eval(n->b,e); if((o.t==T_LIST)&&idx.t==T_NUM&&idx.u.n.isint){ long long i=idx.u.n.i; if(i>=0&&i<o.u.l->len)return o.u.l->items[i]; return V_NIL; } if(o.t==T_STR&&idx.t==T_NUM){ long long i=idx.u.n.i; if(i>=0&&i<(long long)strlen(o.u.s)){ char *c=xmalloc(2); c[0]=o.u.s[i]; c[1]=0; return mkstr_owned(c);} return V_NIL;} throw_gne(BAD_VARIABLE,"인덱스 접근 불가"); return V_NIL; }
        case N_UN:{ Value v=eval(n->a,e); if(!strcmp(n->s,"!"))return mkbool(!truthy(v)); return mknum_d(-as_num(v)); }
        case N_BIN:{
            if(!strcmp(n->s,"&&")){ Value a=eval(n->a,e); if(!truthy(a))return a; return eval(n->b,e); }
            if(!strcmp(n->s,"||")){ Value a=eval(n->a,e); if(truthy(a))return a; return eval(n->b,e); }
            Value a=eval(n->a,e), b=eval(n->b,e);
            if(!strcmp(n->s,"=="))return mkbool(val_eq(a,b));
            if(!strcmp(n->s,"!="))return mkbool(!val_eq(a,b));
            return num_binop(n->s,a,b);
        }
        case N_NEW: return instantiate(n->s);
        case N_ASSIGN:{
            Value val=eval(n->b,e); Node *tg=n->a;
            if(tg->kind==N_ID){ env_set(e,tg->s,val); }
            else if(tg->kind==N_MEMBER){ Value o=eval(tg->a,e); if(o.t!=T_OBJ) throw_gne(BAD_VARIABLE,tg->s); obj_set(o.u.o,tg->s,val); }
            else if(tg->kind==N_INDEX){ Value o=eval(tg->a,e); Value idx=eval(tg->b,e); if(o.t!=T_LIST||idx.t!=T_NUM||!idx.u.n.isint||idx.u.n.i<0) throw_gne(BAD_VARIABLE,"인덱스 대입 불가"); List *l=o.u.l; long long i=idx.u.n.i; if(i>=l->cap){ int nc=(int)i+1; l->items=realloc(l->items,nc*sizeof(Value)); for(int k=l->len;k<nc;k++)l->items[k]=V_NIL; l->cap=nc; } if(i>=l->len)l->len=(int)i+1; l->items[i]=val; }
            else throw_gne(BAD_VARIABLE,"대입할 수 없는 대상");
            return val;
        }
        case N_CALL:{
            Node *callee=n->a; int argc=n->b->nkids; Value *argv=argc?xmalloc(sizeof(Value)*argc):NULL;
            for(int k=0;k<argc;k++) argv[k]=eval(n->b->kids[k],e);
            /* 콘솔.로그 */
            if(callee->kind==N_MEMBER && callee->a->kind==N_ID && !strcmp(callee->a->s,"\xec\xbd\x98\xec\x86\x94") && !strcmp(callee->s,"\xeb\xa1\x9c\xea\xb7\xb8")){ console_log(argc,argv); return V_NIL; }
            if(callee->kind==N_MEMBER){ Value recv=eval(callee->a,e); if(recv.t==T_LIST && !strcmp(callee->s,"\xec\xb6\x94\xea\xb0\x80")){ for(int k=0;k<argc;k++){ List*l=recv.u.l; if(l->len==l->cap){l->cap=l->cap?l->cap*2:4;l->items=realloc(l->items,l->cap*sizeof(Value));} l->items[l->len++]=argv[k]; } return V_NIL; } }
            Value fv=eval(callee,e);
            if(fv.t==T_FUNC) return call_func(fv.u.f,argc,argv);
            if(fv.t==T_BUILTIN) return fv.u.bi(argc,argv);
            throw_gne("함수가 아니다",NULL); return V_NIL;
        }
        default: break;
    }
    throw_gne("알 수 없는 표현",NULL); return V_NIL;
}

/* 오방낭/민원 */
static Value bi_obang(int argc, Value *argv){ if(argc==0) return mknum_d(rand()/(RAND_MAX+1.0)); long m=(long)as_num(argv[0]); if(m<=0)return mknum_i(0); return mknum_i(rand()%m); }
static Value bi_minwon(int argc, Value *argv){ if(argc>0){ char *p=show(argv[0]); fputs(p,stdout); fflush(stdout);} char line[1024]; if(!fgets(line,sizeof line,stdin)){ fprintf(stderr,"[민원실] 접수할 민원이 없어 창구를 닫습니다.\n"); do_exit(); } int len=strlen(line); while(len>0&&(line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==' '))line[--len]=0; char *e; long iv=strtol(line,&e,10); if(len>0&&*e==0) return mknum_i(iv); char *r=xmalloc(len+1); memcpy(r,line,len+1); return mkstr_owned(r); }

static Flow exec_node(Node *n, Env *e){
    switch(n->kind){
        case N_EXPRSTMT: eval(n->a,e); return FLOW_NORMAL;
        case N_VAR:{ Value v; if(n->a)v=eval(n->a,e); else if(n->bval){ List*l=xmalloc(sizeof(List)); l->items=NULL;l->len=0;l->cap=0; v.t=T_LIST; v.u.l=l; } else v=V_NIL; env_define(e,n->s,v); return FLOW_NORMAL; }
        case N_BLOCK:{ Env *in=env_new(e); hoist(n,in); return exec_block(n,in); }
        case N_MAIN:{ Env *in=env_new(e); hoist(n->a,in); return exec_block(n->a,in); }
        case N_GAME:{ fputs(GAME_WARNING,stdout); putchar('\n'); Env *in=env_new(e); hoist(n->a,in); return exec_block(n->a,in); }
        case N_IF: if(truthy(eval(n->a,e))) return exec_node(n->b,e); else if(n->c) return exec_node(n->c,e); return FLOW_NORMAL;
        case N_WHILE: while(truthy(eval(n->a,e))){ Flow f=exec_node(n->b,e); if(f==FLOW_BREAK)break; if(f==FLOW_RETURN)return f; } return FLOW_NORMAL;
        case N_TRY:{
            int level=g_nhandlers; int depth0=g_depth;
            if(level>=MAX_HANDLERS) return exec_node(n->a,e);
            if(setjmp(g_handlers[level])){ /* 예외 발생: 되감기 */ g_nhandlers=level; g_depth=depth0; if(n->b) return exec_node(n->b,e); return FLOW_NORMAL; }
            g_nhandlers=level+1;
            Flow f=exec_node(n->a,e);
            g_nhandlers=level;
            return f;
        }
        case N_BREAK: return FLOW_BREAK;
        case N_RETURN: g_retval = n->a?eval(n->a,e):V_NIL; return FLOW_RETURN;
        case N_EXIT: do_exit(); return FLOW_NORMAL;
        case N_TRACE: fprintf(stderr,"[추적] 우리의 목표는, 진행하는 것 그것으로 (줄 %d)\n",n->line); return FLOW_NORMAL;
        case N_BP: fprintf(stderr,"[메르스] 중단점 (줄 %d) - 격리 없이 계속 진행합니다\n",n->line); return FLOW_NORMAL;
        case N_GC: fprintf(stderr,"[빨간색 마티즈] 쓰레기 수거 완료 -> 남산\n"); return FLOW_NORMAL;
        case N_NOOP: case N_CLASS: case N_FUNC: return FLOW_NORMAL;
        default: eval(n,e); return FLOW_NORMAL;
    }
}

/* setjmp가 핸들러 안에서 g_nhandlers를 되돌리도록: throw 직전 레벨 보정 */
/* throw_gne는 g_handlers[g_nhandlers-1]로 점프하므로, 점프 후 그 핸들러는 소비됨.
   N_TRY의 setjmp 복귀 시 g_nhandlers는 증가된 상태이므로 1 줄여 맞춘다. */

/* ================================================================== 소스 로드 */
static char *iconv_all(const char *from, const char *bytes, size_t nbytes){
    iconv_t cd=iconv_open("UTF-8", from); if(cd==(iconv_t)-1) return NULL;
    size_t outcap=nbytes*4+16; char *out=xmalloc(outcap); char *inp=(char*)bytes,*outp=out; size_t inleft=nbytes,outleft=outcap;
    if(iconv(cd,&inp,&inleft,&outp,&outleft)==(size_t)-1){ iconv_close(cd); return NULL; }
    iconv_close(cd); *outp=0; return out;
}
static int valid_utf8(const unsigned char *s,size_t n){ size_t i=0; while(i<n){ unsigned char c=s[i]; int len=c<0x80?1:(c>>5)==0x6?2:(c>>4)==0xe?3:(c>>3)==0x1e?4:0; if(!len)return 0; if(i+len>n)return 0; for(int k=1;k<len;k++) if((s[i+k]&0xc0)!=0x80)return 0; i+=len; } return 1; }

static char *read_source(const char *path){
    FILE *f=fopen(path,"rb"); if(!f){ fprintf(stderr,"해내겠습니다: 파일을 열 수 없습니다 - %s\n",path); return NULL; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char *buf=xmalloc(sz+1); size_t got=fread(buf,1,sz,f); buf[got]=0; fclose(f);
    if(got>=2 && ((buf[0]==0xFF&&buf[1]==0xFE)||(buf[0]==0xFE&&buf[1]==0xFF))){ char *u=iconv_all("UTF-16",(char*)buf,got); if(u)return u; }
    if(got>=3 && buf[0]==0xEF&&buf[1]==0xBB&&buf[2]==0xBF){ return xstrdup((char*)buf+3); }
    if(valid_utf8(buf,got)) return (char*)buf;
    char *u=iconv_all("CP949",(char*)buf,got); if(u)return u; /* EUC-KR만 지원합니다 */
    return (char*)buf;
}

static void setup_globals(void){
    g_globals=env_new(NULL);
    /* 콘솔 네임스페이스 (실제 디스패치는 N_CALL에서 처리하지만 참조 가능하게) */
    { NS *ns=xmalloc(sizeof(NS)); ns->entries=NULL; Value v; v.t=T_NS; v.u.ns=ns; env_define(g_globals,"\xec\xbd\x98\xec\x86\x94",v); }
    { Value v; v.t=T_BUILTIN; v.u.bi=bi_obang; env_define(g_globals,"\xec\x98\xa4\xeb\xb0\xa9\xeb\x82\xad",v); }
    { Value v; v.t=T_BUILTIN; v.u.bi=bi_minwon; env_define(g_globals,"\xeb\xaf\xbc\xec\x9b\x90",v); }
    const char *consts[]={"\xec\xb5\x9c\xec\x88\x9c\xec\x8b\xa4","\xed\x9d\x99\xec\x88\x98\xec\xa0\x80","\xea\xb8\x88\xec\x88\x98\xec\xa0\x80","\xec\x8a\xb9\xeb\xa7\x88\xec\x84\xa0\xec\x88\x98","\xec\x9d\xb8\xed\x84\xb4",0};
    for(int k=0;consts[k];k++) env_define(g_globals,consts[k],mkstr(consts[k]));
}

/* 입력이 완결됐는지(괄호 균형 + 문자열/주석 미종료 아님) 검사 */
static int input_complete(const char *s){
    int depth=0, n=(int)strlen(s);
    for(int i=0;i<n;i++){
        char c=s[i];
        if(c=='"'||c=='\''){ char q=c; i++; while(i<n&&s[i]!=q){ if(s[i]=='\\')i++; i++; } if(i>=n) return 0; continue; }
        if(c=='/'&&s[i+1]=='/'){ while(i<n&&s[i]!='\n')i++; continue; }
        if(c=='/'&&s[i+1]=='*'){ i+=2; while(i<n&&!(s[i]=='*'&&s[i+1]=='/'))i++; if(i>=n) return 0; i++; continue; }
        if(c=='/'){ int m=is_lda(s,i+1); if(m){ i+=1+m; int closed=0; while(i<n){ int e=is_lda(s,i); if(e&&s[i+e]=='/'){ i+=e; closed=1; break; } i++; } if(!closed) return 0; continue; } }
        if(c=='('||c=='{'||c=='[') depth++;
        else if(c==')'||c=='}'||c==']') depth--;
    }
    return depth<=0;
}

static void repl_exec_chunk(const char *src){
    TokList tl={0}; lex(src,&tl);
    Parser p={tl.toks,0,0}; Node *prog=parse_program(&p);
    hoist(prog,g_globals);
    for(int k=0;k<prog->nkids;k++){
        Node *s=prog->kids[k];
        if(s->kind==N_FUNC||s->kind==N_CLASS||s->kind==N_NOOP) continue;
        if(s->kind==N_EXPRSTMT && s->a->kind!=N_ASSIGN){
            Value v=eval(s->a,g_globals);
            if(v.t!=T_NIL){ char *str=show(v); printf("%s\n",str); }
        } else {
            exec_node(s,g_globals);
        }
    }
}

static void repl(void){
    g_repl=1;
    setup_globals();
    printf("준비된 한국형 대화창 — SSVM(순실VM) REPL. 종료하려면 '고심 끝에 프로세스 해체' 또는 Ctrl-D.\n");
    char *acc=xstrdup(""); int cont=0; char line[4096];
    for(;;){
        fputs(cont?"..... ":"\xea\xb7\xb8\xeb\x84\xa4> ", stdout); fflush(stdout); /* 그네> */
        if(!fgets(line,sizeof line,stdin)){ printf("\n고심 끝에 프로세스 해체\n"); break; }
        char *t=xmalloc(strlen(acc)+strlen(line)+1); strcpy(t,acc); strcat(t,line); acc=t;
        if(!input_complete(acc)){ cont=1; continue; }
        cont=0;
        g_nhandlers=0; g_depth=0;
        int code=setjmp(g_toplevel);
        if(code==0){
            if(setjmp(g_parsejmp)==0) repl_exec_chunk(acc);
            fflush(stdout);
        } else if(code==2){ /* 고심 끝에 프로세스 해체 */ printf("고심 끝에 프로세스 해체\n"); break; }
        /* code==1: 런타임 오류는 이미 출력됨. 프롬프트로 복귀 */
        acc=xstrdup("");
    }
    fflush(stdout);
}

int main(int argc, char **argv){
    V_NIL.t=T_NIL;
    const char *seed=getenv("GNE_SEED"); srand(seed?(unsigned)atoi(seed):(unsigned)time(NULL));

    int want_repl = (argc<2);
    if(argc>=2 && (!strcmp(argv[1],"\xeb\x8c\x80\xed\x99\x94")||!strcmp(argv[1],"-i")||!strcmp(argv[1],"repl"))) want_repl=1; /* 대화 */
    if(want_repl){ repl(); return g_exit_code; }

    char *src=read_source(argv[1]); if(!src)return 2;
    TokList tl={0}; lex(src,&tl);
    Parser p={tl.toks,0,0}; Node *prog=parse_program(&p);
    setup_globals();
    hoist(prog,g_globals);
    if(setjmp(g_toplevel)==0){
        for(int k=0;k<prog->nkids;k++){ Node *s=prog->kids[k]; if(s->kind==N_FUNC||s->kind==N_CLASS||s->kind==N_NOOP)continue; exec_node(s,g_globals); }
    }
    fflush(stdout);
    return g_exit_code;
}
