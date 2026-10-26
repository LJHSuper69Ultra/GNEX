#!/usr/bin/env python3
"""JTBC — Joseon Turbo Boost Compiler.

GNE(그네)를 C언어로 투명하게 컴파일하여 진상규명이 제대로 되도록 합니다.
생성된 C 소스를 직접 눈으로 확인할 수 있습니다(진상규명). gcc 로 네이티브 빌드합니다.

사용법:
    JTBC 파일.ㄹ            내각(현재 폴더)에 사회를 짓고 바로 실행
    JTBC 파일.ㄹ -o 사회    네이티브 실행 파일로 컴파일
    JTBC 파일.ㄹ -c         생성된 C 소스를 화면에 출력(투명한 컴파일)

JTBC가 지원하는 것은 그네의 명령형 핵심입니다: 함수(K-*), 공천, 만약/아니면,
될때까지, 산술/비교/논리 연산, 콘솔.로그, 오방낭, 민원, 완료(반환/break),
고심 끝에 프로세스 해체. 창조/사람 등 객체와 예외처리, 프록시, 배열은 아직
`JTBC`가 지원하지 않으니 그런 사회는 런타임 `해내겠습니다`로 실행하십시오.
"""
import os
import subprocess
import sys
import tempfile

import gne

GAME_WARNING = gne.GAME_WARNING


class JTBCError(Exception):
    pass


def mangle(name):
    """한글 등 식별자를 C 식별자로 변환한다. (투명하게 추적 가능)"""
    out = ["g_"]
    for ch in name:
        if ch.isascii() and (ch.isalnum() or ch == "_"):
            out.append(ch)
        else:
            out.append(f"_u{ord(ch):04x}_")
    return "".join(out)


def c_string(s):
    """파이썬 문자열을 C 문자열 리터럴로 이스케이프한다 (UTF-8 바이트 유지)."""
    buf = ['"']
    for ch in s:
        if ch == "\\":
            buf.append("\\\\")
        elif ch == '"':
            buf.append('\\"')
        elif ch == "\n":
            buf.append("\\n")
        elif ch == "\t":
            buf.append("\\t")
        elif ch == "\r":
            buf.append("\\r")
        else:
            buf.append(ch)
    buf.append('"')
    return "".join(buf)


RUNTIME = r"""/* === JTBC 런타임 (순실VM C 호환층) === */
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

/* 빨간색 마티즈가 수거하지 않으므로 아래 문자열들은 남산으로 가지 않고 샙니다(leak). */
static const char *gne_tostr(Value v){
    char *buf;
    switch(v.tag){
        case GNE_STR: return v.str;
        case GNE_BOOL: return v.boolean ? "\xec\xb9\x9c\xeb\xb0\x95" : "\xeb\xb9\x84\xeb\xb0\x95"; /* 친박/비박 */
        case GNE_NIL: return "null";
        default: break;
    }
    buf = (char*)malloc(32);
    if(v.num == floor(v.num) && fabs(v.num) < 1e15)
        snprintf(buf, 32, "%lld", (long long)v.num);
    else
        snprintf(buf, 32, "%g", v.num);
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
    strcpy(r,x); strcat(r,y);
    return gne_str(r);
}
static Value gne_add(Value a, Value b){
    if(a.tag==GNE_STR || b.tag==GNE_STR) return gne_cat(a,b);
    return gne_num(gne_asnum(a)+gne_asnum(b));
}
static Value gne_sub(Value a, Value b){ return gne_num(gne_asnum(a)-gne_asnum(b)); }
static Value gne_mul(Value a, Value b){ return gne_num(gne_asnum(a)*gne_asnum(b)); }
static Value gne_div(Value a, Value b){
    double d=gne_asnum(b);
    if(d==0){ fprintf(stderr,"\xec\xa0\x9c\xea\xb0\x80 \xeb\xad\x90\xeb\x9d\xbc\xea\xb3\xa0 \xed\x96\x88\xec\x8a\xb5\xeb\x8b\x88\xea\xb9\x8c? 0\xec\x9c\xbc\xeb\xa1\x9c \xeb\x82\x98\xeb\x88\x8c \xec\x88\x98 \xec\x97\x86\xeb\x8b\xa4\n"); exit(1); }
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
static Value gne_obang(Value n){
    long m=(long)gne_asnum(n);
    if(m<=0) return gne_num(0);
    return gne_num(rand()%m);
}

static Value gne_minwon(Value prompt){
    char line[1024]; size_t len; char *e;
    long iv;
    if(prompt.tag!=GNE_NIL){ fputs(gne_tostr(prompt),stdout); fflush(stdout); }
    if(!fgets(line,sizeof(line),stdin)){
        fprintf(stderr,"[\xeb\xaf\xbc\xec\x9b\x90\xec\x8b\xa4] \xec\xa0\x91\xec\x88\x98\xed\x95\xa0 \xeb\xaf\xbc\xec\x9b\x90\xec\x9d\xb4 \xec\x97\x86\xec\x96\xb4 \xec\xb0\xbd\xea\xb5\xac\xeb\xa5\xbc \xeb\x8b\xab\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.\n"); /* 민원실 */
        exit(0);
    }
    len=strlen(line);
    while(len>0 && (line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==' ')) line[--len]=0;
    iv=strtol(line,&e,10);
    if(*line && *e==0) return gne_num(iv);
    { char *r=(char*)malloc(len+1); memcpy(r,line,len+1); return gne_str(r); }
}

static void gne_seed(void){
    const char *s=getenv("GNE_SEED");
    srand(s ? (unsigned)atoi(s) : (unsigned)time(0));
}
"""


class Transpiler:
    def __init__(self):
        self.protos = []
        self.funcs = []
        self.mains = []
        self.tmp = 0

    def transpile(self, stmts):
        # 먼저 함수/클래스를 훑어 프로토타입을 만든다 (호이스팅).
        for s in stmts:
            if s[0] == "class":
                raise JTBCError(
                    f"JTBC는 아직 창조 객체(사람/병원 등 '{s[1]}')를 지원하지 않습니다. "
                    "해내겠습니다(인터프리터)로 실행하십시오.")
        for s in stmts:
            if s[0] == "func":
                params = ", ".join("Value " + mangle(p) for p in s[2]) or "void"
                self.protos.append(f"static Value {mangle(s[1])}({params});")
        for s in stmts:
            if s[0] == "func":
                self.emit_func(s)
            elif s[0] in ("main", "game"):
                self.emit_main(s)
            elif s[0] in ("noop",):
                pass
            else:
                raise JTBCError(f"최상위에는 사회/질병/함수만 올 수 있습니다 (받은 것: {s[0]})")
        return self.assemble()

    def emit_func(self, s):
        _, name, params, body = s
        sig = ", ".join("Value " + mangle(p) for p in params) or "void"
        lines = [f"static Value {mangle(name)}({sig}) {{"]
        lines += self.block_body(body[1], indent=1, in_func=True)
        lines.append("    return gne_nil();")
        lines.append("}")
        self.funcs.append("\n".join(lines))

    def emit_main(self, s):
        lines = []
        if s[0] == "game":
            lines.append(f"    gne_log(1, gne_str({c_string(GAME_WARNING)}));")
        lines += self.block_body(s[1][1], indent=1, in_func=False)
        self.mains.append("\n".join(lines))

    # --- 문장
    def block_body(self, stmts, indent, in_func):
        out = []
        for st in stmts:
            out += self.stmt(st, indent, in_func)
        return out

    def stmt(self, s, ind, in_func):
        pad = "    " * ind
        kind = s[0]
        if kind == "var":
            _, name, init, is_array = s
            if is_array:
                raise JTBCError(f"JTBC는 아직 배열('{name}[]')을 지원하지 않습니다.")
            rhs = self.expr(init) if init else "gne_nil()"
            return [f"{pad}Value {mangle(name)} = {rhs};"]
        if kind == "expr":
            return [f"{pad}{self.expr(s[1])};"]
        if kind == "block":
            return [pad + "{"] + self.block_body(s[1], ind + 1, in_func) + [pad + "}"]
        if kind == "if":
            out = [f"{pad}if (gne_truthy({self.expr(s[1])})) {{"]
            out += self.stmt_as_block(s[2], ind + 1, in_func)
            out.append(pad + "}")
            if s[3]:
                out.append(pad + "else {")
                out += self.stmt_as_block(s[3], ind + 1, in_func)
                out.append(pad + "}")
            return out
        if kind == "while":
            out = [f"{pad}while (gne_truthy({self.expr(s[1])})) {{"]
            out += self.stmt_as_block(s[2], ind + 1, in_func)
            out.append(pad + "}")
            return out
        if kind == "break":
            return [pad + "break;"]
        if kind == "return":
            if in_func:
                return [f"{pad}return {self.expr(s[1]) if s[1] else 'gne_nil()'};"]
            return [pad + "return;"]
        if kind == "exit":
            return [pad + "exit(0);"]
        if kind == "trace":
            return [f'{pad}fprintf(stderr, "[\xec\xb6\x94\xec\xa0\x81] \xec\xa4\x84 %d\\n", {s[1]});']
        if kind == "breakpoint":
            return [f'{pad}fprintf(stderr, "[\xeb\xa9\x94\xeb\xa5\xb4\xec\x8a\xa4] \xec\xa4\x84 %d\\n", {s[1]});']
        if kind in ("noop", "gc"):
            return []
        if kind == "try":
            raise JTBCError(
                "JTBC는 아직 예외처리(코드 텅텅 빌때까지 / 예외처리 다 어디 갔냐고)를 "
                "지원하지 않습니다. 해내겠습니다(인터프리터)로 실행하십시오.")
        if kind == "func":
            raise JTBCError("JTBC는 중첩 함수를 지원하지 않습니다. 함수는 최상위에 두십시오.")
        if kind == "class":
            raise JTBCError(f"JTBC는 아직 창조 객체('{s[1]}')를 지원하지 않습니다.")
        raise JTBCError(f"JTBC가 모르는 문장입니다: {kind}")

    def stmt_as_block(self, s, ind, in_func):
        if s[0] == "block":
            return self.block_body(s[1], ind, in_func)
        return self.stmt(s, ind, in_func)

    # --- 식
    def expr(self, n):
        kind = n[0]
        if kind == "num":
            v = n[1]
            return f"gne_num({v!r})" if isinstance(v, float) else f"gne_num({v}.0)"
        if kind == "bool":
            return f"gne_bool({1 if n[1] else 0})"
        if kind == "id":
            return self.name_ref(n[1])
        if kind == "str":
            return self.string(n[1])
        if kind == "un":
            if n[1] == "!":
                return f"gne_bool(!gne_truthy({self.expr(n[2])}))"
            return f"gne_num(-gne_asnum({self.expr(n[2])}))"
        if kind == "bin":
            return self.binop(n)
        if kind == "assign":
            return self.assign(n)
        if kind == "call":
            return self.call(n)
        if kind == "member":
            raise JTBCError("JTBC는 아직 멤버 접근(.)을 지원하지 않습니다 (콘솔.로그 제외).")
        if kind == "index":
            raise JTBCError("JTBC는 아직 배열 인덱스를 지원하지 않습니다.")
        if kind == "new":
            raise JTBCError(f"JTBC는 아직 '창조 {n[1]}'을 지원하지 않습니다.")
        if kind == "proxy":
            raise JTBCError("JTBC는 아직 프록시(비선)를 지원하지 않습니다.")
        if kind == "list":
            raise JTBCError("JTBC는 아직 리스트를 지원하지 않습니다.")
        raise JTBCError(f"JTBC가 모르는 표현입니다: {kind}")

    def name_ref(self, name):
        if name in ("친박",):
            return "gne_bool(1)"
        if name in ("비박",):
            return "gne_bool(0)"
        if name in ("최순실", "흙수저", "금수저", "승마선수", "인턴"):
            return f"gne_str({c_string(name)})"
        return mangle(name)

    def string(self, parts):
        if len(parts) == 1 and isinstance(parts[0], str):
            return f"gne_str({c_string(parts[0])})"
        acc = None
        for p in parts:
            piece = f"gne_str({c_string(p)})" if isinstance(p, str) else self.expr(p)
            acc = piece if acc is None else f"gne_cat({acc}, {piece})"
        return acc or "gne_str(\"\")"

    def binop(self, n):
        op, l, r = n[1], self.expr(n[2]), self.expr(n[3])
        fn = {"+": "gne_add", "-": "gne_sub", "*": "gne_mul", "/": "gne_div", "%": "gne_mod"}
        if op in fn:
            return f"{fn[op]}({l}, {r})"
        if op == "==":
            return f"gne_bool(gne_eq({l}, {r}))"
        if op == "!=":
            return f"gne_bool(!gne_eq({l}, {r}))"
        cmp = {"<": "<", ">": ">", "<=": "<=", ">=": ">="}
        if op in cmp:
            return f"gne_bool(gne_asnum({l}) {cmp[op]} gne_asnum({r}))"
        if op == "&&":
            return f"gne_bool(gne_truthy({l}) && gne_truthy({r}))"
        if op == "||":
            return f"gne_bool(gne_truthy({l}) || gne_truthy({r}))"
        raise JTBCError(f"JTBC가 모르는 연산자입니다: {op}")

    def assign(self, n):
        target, value = n[1], self.expr(n[2])
        if target[0] != "id":
            raise JTBCError("JTBC는 변수에만 대입할 수 있습니다 (멤버/인덱스 미지원).")
        return f"({self.name_ref(target[1])} = {value})"

    def call(self, n):
        callee, args = n[1], n[2]
        cargs = [self.expr(a) for a in args]
        if callee[0] == "member":
            obj, field = callee[1], callee[2]
            if obj[0] == "id" and obj[1] == "콘솔" and field == "로그":
                return f"gne_log({len(cargs)}" + "".join(", " + a for a in cargs) + ")"
            raise JTBCError(f"JTBC는 아직 '{field}' 멤버 호출을 지원하지 않습니다.")
        if callee[0] == "id":
            name = callee[1]
            if name == "오방낭":
                return "gne_obang_none()" if not cargs else f"gne_obang({cargs[0]})"
            if name == "민원":
                return "gne_minwon(gne_nil())" if not cargs else f"gne_minwon({cargs[0]})"
            return f"{mangle(name)}(" + ", ".join(cargs) + ")"
        raise JTBCError("JTBC는 이름으로만 함수를 호출할 수 있습니다.")

    def assemble(self):
        parts = [RUNTIME]
        if self.protos:
            parts.append("\n".join(self.protos))
        parts.extend(self.funcs)
        main = ["int main(void) {", "    gne_seed();"]
        main.append("\n".join(self.mains))
        main.append("    return 0;")
        main.append("}")
        parts.append("\n".join(main))
        return "\n\n".join(parts) + "\n"


def transpile_source(src):
    program = gne.Parser(gne.tokenize(src)).parse_program()
    return Transpiler().transpile(program)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("-")]
    flags = [a for a in argv[1:] if a.startswith("-")]
    if not args:
        print(__doc__)
        return 2
    path = args[0]
    try:
        src = gne.read_source(path)
        csrc = transpile_source(src)
    except JTBCError as e:
        print(f"JTBC 컴파일 거부: {e}", file=sys.stderr)
        return 2
    except gne.GNESyntaxError as e:
        print(f"안보 경고: {e}", file=sys.stderr)
        return 2

    if "-c" in flags:
        sys.stdout.write(csrc)
        return 0

    out = None
    if "-o" in flags:
        i = argv.index("-o")
        out = argv[i + 1] if i + 1 < len(argv) else None
    run = out is None
    if out is None:
        out = os.path.join(tempfile.mkdtemp(prefix="jtbc_"), "사회")

    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False, encoding="utf-8") as f:
        f.write(csrc)
        cpath = f.name
    cc = os.environ.get("CC", "gcc")
    res = subprocess.run([cc, "-O2", "-std=c11", cpath, "-o", out, "-lm"],
                         capture_output=True, text=True)
    if res.returncode != 0:
        print("JTBC: gcc 컴파일 실패\n" + res.stderr, file=sys.stderr)
        return 1
    if run:
        return subprocess.run([out]).returncode
    print(f"JTBC: 사회를 지었습니다 -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
