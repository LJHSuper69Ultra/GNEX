/* === GNE 런타임 (JTBC로 C 컴파일된 사회가 쓰는 순실VM C 호환층) ===
 * JTBC(ssvm --emit-c)가 생성한 C 소스가 #include 합니다.
 * 수는 double 기반이라 아주 큰 수는 근사치일 수 있습니다. 메모리는 빨간색 마티즈가
 * 수거하지 않으므로 그냥 샙니다(leak). 사회는 짧게 삽니다. */
#ifndef GNE_RT_H
#define GNE_RT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

typedef enum { GNE_NIL, GNE_NUM, GNE_BOOL, GNE_STR } GneTag;
typedef struct { GneTag tag; double num; int boolean; const char *str; } Value;

static Value gne_nil(void){ Value v; v.tag=GNE_NIL; v.num=0; v.boolean=0; v.str=0; return v; }
static Value gne_num(double n){ Value v=gne_nil(); v.tag=GNE_NUM; v.num=n; return v; }
static Value gne_bool(int b){ Value v=gne_nil(); v.tag=GNE_BOOL; v.boolean=b?1:0; return v; }
static Value gne_str(const char *s){ Value v=gne_nil(); v.tag=GNE_STR; v.str=s; return v; }

static double gne_asnum(Value v){
    switch(v.tag){
        case GNE_NUM: return v.num;
        case GNE_BOOL: return v.boolean;
        case GNE_STR: return atof(v.str);
        default: return 0;
    }
}
static const char *gne_tostr(Value v){
    char *buf;
    switch(v.tag){
        case GNE_STR: return v.str;
        case GNE_BOOL: return v.boolean ? "친박" : "비박";
        case GNE_NIL: return "null";
        default: break;
    }
    buf = (char*)malloc(32);
    if(v.num == floor(v.num) && fabs(v.num) < 1e15) snprintf(buf, 32, "%lld", (long long)v.num);
    else snprintf(buf, 32, "%g", v.num);
    return buf;
}
static int gne_truthy(Value v){
    switch(v.tag){
        case GNE_BOOL: return v.boolean;
        case GNE_NUM: return v.num != 0;
        case GNE_STR: return v.str && v.str[0];
        default: return 0;
    }
}
static Value gne_cat(Value a, Value b){
    const char *x=gne_tostr(a), *y=gne_tostr(b);
    char *r=(char*)malloc(strlen(x)+strlen(y)+1);
    strcpy(r,x); strcat(r,y); return gne_str(r);
}
static Value gne_add(Value a, Value b){
    if(a.tag==GNE_STR || b.tag==GNE_STR) return gne_cat(a,b);
    return gne_num(gne_asnum(a)+gne_asnum(b));
}
static Value gne_sub(Value a, Value b){ return gne_num(gne_asnum(a)-gne_asnum(b)); }
static Value gne_mul(Value a, Value b){ return gne_num(gne_asnum(a)*gne_asnum(b)); }
static Value gne_div(Value a, Value b){
    double d=gne_asnum(b);
    if(d==0){ fprintf(stderr,"제가 뭐라고 했습니까? 0으로 나눌 수 없다\n"); exit(1); }
    return gne_num(gne_asnum(a)/d);
}
static Value gne_mod(Value a, Value b){ return gne_num(fmod(gne_asnum(a),gne_asnum(b))); }
static int gne_eq(Value a, Value b){
    if(a.tag==GNE_STR && b.tag==GNE_STR) return strcmp(a.str,b.str)==0;
    if(a.tag==GNE_STR || b.tag==GNE_STR) return 0;
    if(a.tag==GNE_NIL || b.tag==GNE_NIL) return a.tag==b.tag;
    return gne_asnum(a)==gne_asnum(b);
}
static void gne_log(int n, ...){
    int i; va_list ap; va_start(ap,n);
    for(i=0;i<n;i++){ Value v=va_arg(ap,Value); fputs(gne_tostr(v),stdout); if(i<n-1) putchar(' '); }
    putchar('\n'); va_end(ap);
}
static Value gne_obang_none(void){ return gne_num(rand()/(RAND_MAX+1.0)); }
static Value gne_obang(Value n){ long m=(long)gne_asnum(n); if(m<=0) return gne_num(0); return gne_num(rand()%m); }
static Value gne_minwon(Value prompt){
    char line[1024]; size_t len; char *e; long iv;
    if(prompt.tag!=GNE_NIL){ fputs(gne_tostr(prompt),stdout); fflush(stdout); }
    if(!fgets(line,sizeof(line),stdin)){ fprintf(stderr,"[민원실] 접수할 민원이 없어 창구를 닫습니다.\n"); exit(0); }
    len=strlen(line);
    while(len>0 && (line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==' ')) line[--len]=0;
    iv=strtol(line,&e,10);
    if(*line && *e==0) return gne_num(iv);
    { char *r=(char*)malloc(len+1); memcpy(r,line,len+1); return gne_str(r); }
}
static void gne_seed(void){ const char *s=getenv("GNE_SEED"); srand(s ? (unsigned)atoi(s) : (unsigned)time(0)); }
#endif
