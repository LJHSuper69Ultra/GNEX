#!/usr/bin/env python3
"""GNE(그네) 인터프리터 - SSVM(순실VM) 대충 구현체.

사용법:  해내겠습니다 국정운영.ㄹ
"""
import glob
import os
import re
import sys

sys.setrecursionlimit(10000)

SECURITY_WARNING = (
    "국가보안법에 의거한 안보 경고: 어떤 함수가 테러방지법에 저촉되는 복면을 쓰고 있다는 것이 "
    "통신 감청을 통해서 감지되는 이런 상황에 발목을 잡는 야당이 이렇게 IS(이슬람국가)도 지금 "
    "얼굴을 감추고 그렇게 하고 있지 않느냐 하는 이번에야말로 배후에서 불법을 조종하고, 폭력을 "
    "부추기는 세력들을 법과 원칙에 따라 엄중하게 처리해서 종북 세력을 색출, 근절하고 불법과 "
    "폭력의 악순환을 끊어내야 할 것이다 하는 과정으로 마음으로 창조경제 제가 해내겠습니다."
)
GMAIL_WARNING = "안보 경고: Gmail을 사용하는 사람은 종북세력입니다. 믿을 수 있는 샵메일을 대신 사용하십시오."
BAD_VARIABLE = "나쁜 변수이더라"


class GNEError(Exception):
    """예외처리 다 어디 갔냐고 에서 잡을 수 있는 오류."""


class GNESyntaxError(Exception):
    pass


class ExitSignal(Exception):
    pass


class BreakSignal(Exception):
    pass


class ReturnSignal(Exception):
    def __init__(self, value):
        self.value = value


# ---------------------------------------------------------------- 토크나이저

C = r"[0-9A-Za-z_\u3131-\u318e\uac00-\ud7a3]"
ASSIGN_RE = r"는\s*이것이[다나]"
IDENT_RE = rf"(?:(?!{ASSIGN_RE}){C})+(?:-(?![0-9\s])(?:(?!{ASSIGN_RE}){C})+)*"

KEYWORD_PATTERNS = [
    ("GC", r"메모리\s*텅텅\s*빌\s*때까지\s*한\s*번\s*해\s*보세요\s*쓰레기들\s*다\s*어디\s*갔냐고"),
    ("TRY", r"코드\s*텅텅\s*빌\s*때까지\s*한\s*번\s*해\s*보세요"),
    ("CATCH", r"예외처리\s*다\s*어디\s*갔냐고"),
    ("EXIT", r"고심\s*끝에\s*프로세스\s*해체"),
    ("TRACE", r"우리의\s*목표는,?\s*진행하는\s*것\s*그것으로"),
    ("FUNC", r"전부\s*이렇게\s*해\s*가지고"),
    ("FUNC_TO", r"으로\s*들어가도록"),
    ("WHILE", r"될\s*때까지"),
    ("ASSIGN", rf"{ASSIGN_RE}|이것이[다나]"),
    ("NOOP", r"판사님,[^.\n]*\.?"),
]
KEYWORD_PATTERNS = [(t, re.compile(p)) for t, p in KEYWORD_PATTERNS]

RESERVED = {
    "만약": "IF", "아니면": "ELSE", "완료": "DONE", "창조": "NEW",
    "친박": "TRUE", "비박": "FALSE", "사회": "MAIN", "공천": "VAR",
    "사람": "CLASS", "바이러스": "CLASS", "병원": "CLASS", "책": "CLASS",
    "입당": "IMPORT", "출당": "IMPORT", "더하기": "PLUS",
}
# 동치 관계: ㄹ=근, 법=목욕탕, 누에=나비
ALIASES = {"근": "ㄹ", "목욕탕": "법", "나비": "누에"}

COMMENT_RE = re.compile(r"//[^\n]*|/\*[\s\S]*?\*/|/[ㄹ근][\s\S]*?[ㄹ근]/")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
NUMBER_RE = re.compile(r"\d+(?:\.\d+)?")
IDENT = re.compile(IDENT_RE)
OPS = ["==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=",
       "+", "-", "*", "/", "%", "<", ">", "=", "!", "(", ")", "{", "}", "[", "]",
       ",", ";", ".", ":"]
ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}


def tokenize(src):
    toks, i, line = [], 0, 1
    n = len(src)
    while i < n:
        ch = src[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch.isspace() or ch == "\ufeff":
            i += 1
            continue
        m = COMMENT_RE.match(src, i)
        if m:
            line += m.group().count("\n")
            i = m.end()
            continue
        m = STRING_RE.match(src, i)
        if m:
            toks.append(("str", m.group()[1:-1], line))
            line += m.group().count("\n")
            i = m.end()
            continue
        m = NUMBER_RE.match(src, i)
        if m:
            text = m.group()
            toks.append(("num", float(text) if "." in text else int(text), line))
            i = m.end()
            continue
        for typ, pat in KEYWORD_PATTERNS:
            m = pat.match(src, i)
            if m:
                toks.append((typ, m.group(), line))
                line += m.group().count("\n")
                i = m.end()
                break
        else:
            m = IDENT.match(src, i)
            if m:
                word = ALIASES.get(m.group(), m.group())
                toks.append((RESERVED.get(word, "ident"), word, line))
                i = m.end()
                continue
            if ch == ".":
                nxt = src[i + 1] if i + 1 < n else " "
                if not re.match(C, nxt):  # 문장 끝의 마침표는 ;
                    toks.append(("op", ";", line))
                    i += 1
                    continue
            for op in OPS:
                if src.startswith(op, i):
                    toks.append(("op", op, line))
                    i += len(op)
                    break
            else:
                raise GNESyntaxError(f"{line}번째 줄: 알 수 없는 글자 '{ch}'")
    toks.append(("EOF", None, line))
    return toks


# ---------------------------------------------------------------- 파서

class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0
        self.loops = 0

    def peek(self, k=0):
        return self.t[min(self.i + k, len(self.t) - 1)]

    def next(self):
        tok = self.peek()
        self.i += 1
        return tok

    def is_op(self, v, k=0):
        tok = self.peek(k)
        return tok[0] == "op" and tok[1] == v

    def eat_op(self, v):
        if self.is_op(v):
            self.i += 1
            return True
        return False

    def expect_op(self, v):
        if not self.eat_op(v):
            tok = self.peek()
            raise GNESyntaxError(f"{tok[2]}번째 줄: '{v}' 가 와야 하는데 '{tok[1]}' 가 왔습니다")

    def expect(self, typ):
        tok = self.peek()
        if tok[0] != typ:
            raise GNESyntaxError(f"{tok[2]}번째 줄: {typ} 가 와야 하는데 '{tok[1]}' 가 왔습니다")
        return self.next()

    def end_stmt(self):
        while self.eat_op(";"):
            pass

    # --- 문장
    def parse_program(self):
        stmts = []
        while self.peek()[0] != "EOF":
            stmts.append(self.parse_stmt())
        return stmts

    def parse_block(self):
        self.expect_op("{")
        stmts = []
        # 중괄호를 덜 닫아도 EOF 에서 너그럽게 마무리한다 (국정화 정신)
        while not self.is_op("}") and self.peek()[0] != "EOF":
            stmts.append(self.parse_stmt())
        self.eat_op("}")
        return ("block", stmts)

    def parse_body(self):
        return self.parse_block() if self.is_op("{") else self.parse_stmt()

    def parse_stmt(self):
        typ, val, line = self.peek()
        if typ == "op":
            if val == "{":
                return self.parse_block()
            if val == ";":
                self.next()
                return ("noop",)
        elif typ == "CLASS":
            return self.parse_class()
        elif typ == "MAIN":
            self.next()
            if self.peek()[0] == "ident":
                self.next()
            return ("main", self.parse_block())
        elif typ == "FUNC":
            return self.parse_func()
        elif typ == "VAR":
            return self.parse_var()
        elif typ == "IF":
            self.next()
            self.expect_op("(")
            cond = self.parse_expr()
            self.expect_op(")")
            then = self.parse_body()
            other = None
            if self.peek()[0] == "ELSE":
                self.next()
                other = self.parse_body()
            return ("if", cond, then, other)
        elif typ == "WHILE":
            self.next()
            self.expect_op("(")
            cond = self.parse_expr()
            self.expect_op(")")
            self.loops += 1
            body = self.parse_body()
            self.loops -= 1
            return ("while", cond, body)
        elif typ == "TRY":
            self.next()
            body = self.parse_block()
            handler = None
            if self.peek()[0] == "CATCH":
                self.next()
                handler = self.parse_block()
            return ("try", body, handler)
        elif typ == "DONE":
            self.next()
            expr = None
            if self.loops == 0 and not (self.is_op(";") or self.is_op("}") or self.peek()[0] == "EOF"):
                expr = self.parse_expr()
            self.end_stmt()
            return ("break",) if self.loops else ("return", expr)
        elif typ == "EXIT":
            self.next()
            self.end_stmt()
            return ("exit",)
        elif typ in ("NOOP", "GC"):
            self.next()
            self.end_stmt()
            return ("gc",) if typ == "GC" else ("noop",)
        elif typ == "TRACE":
            self.next()
            self.end_stmt()
            return ("trace", line)
        elif typ == "IMPORT":
            self.next()
            while not self.is_op(";") and self.peek()[0] != "EOF":
                self.next()
            self.end_stmt()
            return ("noop",)
        elif typ == "ident" and val == "메르스" and self.is_op(";", 1):
            self.next()
            self.end_stmt()
            return ("breakpoint", line)
        expr = self.parse_expr()
        self.end_stmt()
        return ("expr", expr)

    def parse_class(self):
        self.next()
        name = self.expect("ident")[1]
        fields = []
        self.expect_op("{")
        while not self.is_op("}") and self.peek()[0] != "EOF":
            if self.peek()[0] == "VAR":
                fields.append(self.parse_var())
            else:
                self.parse_stmt()
        self.eat_op("}")
        return ("class", name, fields)

    def parse_var(self):
        self.next()
        name = self.expect("ident")[1]
        is_array = False
        if self.eat_op("["):
            self.expect_op("]")
            is_array = True
        init = None
        if self.peek()[0] == "ASSIGN" or self.is_op("="):
            self.next()
            init = self.parse_expr()
        self.end_stmt()
        return ("var", name, init, is_array)

    def parse_func(self):
        _, _, line = self.next()
        if self.peek()[0] == "FUNC_TO":
            raise GNESyntaxError(f"{line}번째 줄: " + SECURITY_WARNING)
        name = self.expect("ident")[1]
        if not name.startswith("K"):
            raise GNESyntaxError(f"{line}번째 줄: 모든 함수 이름 앞에는 K를 꼭 붙여야 합니다 ('{name}')")
        self.expect("FUNC_TO")
        self.expect_op("(")
        params = []
        while not self.is_op(")"):
            params.append(self.expect("ident")[1])
            if not self.eat_op(","):
                break
        self.expect_op(")")
        saved, self.loops = self.loops, 0
        body = self.parse_block()
        self.loops = saved
        return ("func", name, params, body)

    # --- 식
    def parse_expr(self):
        left = self.parse_or()
        tok = self.peek()
        if tok[0] == "ASSIGN" or (tok[0] == "op" and tok[1] in ("=", "+=", "-=")):
            if left[0] not in ("id", "member", "index"):
                raise GNESyntaxError(f"{tok[2]}번째 줄: 대입할 수 없는 대상입니다")
            self.next()
            right = self.parse_expr()
            if tok[1] in ("+=", "-="):
                right = ("bin", tok[1][0], left, right)
            return ("assign", left, right)
        return left

    def binary(self, sub, ops):
        left = sub()
        while True:
            tok = self.peek()
            op = "+" if tok[0] == "PLUS" else tok[1] if tok[0] == "op" else None
            if op in ops:
                self.next()
                left = ("bin", op, left, sub())
            else:
                return left

    def parse_or(self):
        return self.binary(self.parse_and, ("||",))

    def parse_and(self):
        return self.binary(self.parse_eq, ("&&",))

    def parse_eq(self):
        return self.binary(self.parse_rel, ("==", "!="))

    def parse_rel(self):
        return self.binary(self.parse_add, ("<", ">", "<=", ">="))

    def parse_add(self):
        return self.binary(self.parse_mul, ("+", "-"))

    def parse_mul(self):
        return self.binary(self.parse_unary, ("*", "/", "%"))

    def parse_unary(self):
        tok = self.peek()
        if tok[0] == "op" and tok[1] in ("!", "-"):
            self.next()
            return ("un", tok[1], self.parse_unary())
        if tok[0] == "NEW":
            self.next()
            name = self.expect("ident")[1]
            args = self.parse_args() if self.is_op("(") else []
            return ("new", name, args, tok[2])
        return self.parse_postfix()

    def parse_args(self):
        self.expect_op("(")
        args = []
        while not self.is_op(")"):
            args.append(self.parse_expr())
            if not self.eat_op(","):
                break
        self.expect_op(")")
        return args

    def parse_postfix(self):
        node = self.parse_primary()
        while True:
            if self.is_op("("):
                node = ("call", node, self.parse_args(), self.peek()[2])
            elif self.is_op("."):
                self.next()
                node = ("member", node, self.next()[1], self.peek()[2])
            elif self.is_op("["):
                self.next()
                idx = self.parse_expr()
                self.expect_op("]")
                node = ("index", node, idx)
            elif self.is_op("++") or self.is_op("--"):
                op = self.next()[1]
                return ("assign", node, ("bin", op[0], node, ("num", 1)))
            else:
                return node

    def parse_primary(self):
        typ, val, line = self.next()
        if typ == "num":
            return ("num", val)
        if typ == "str":
            return parse_string(val)
        if typ == "TRUE":
            return ("bool", True)
        if typ == "FALSE":
            return ("bool", False)
        if typ == "ident":
            return ("id", val, line)
        if typ == "op" and val == "(":
            if self.peek()[0] == "ident" and self.peek()[1] == "비선" and self.is_op(":", 1):
                self.next()
                self.next()
                target = self.expect("ident")[1]
                self.expect_op(")")
                return ("proxy", target)
            expr = self.parse_expr()
            self.expect_op(")")
            return expr
        if typ == "op" and val == "[":
            items = []
            while not self.is_op("]"):
                items.append(self.parse_expr())
                if not self.eat_op(","):
                    break
            self.expect_op("]")
            return ("list", items)
        raise GNESyntaxError(f"{line}번째 줄: 이해할 수 없는 표현입니다 '{val}'")


def parse_string(raw):
    parts, buf, i = [], "", 0
    while i < len(raw):
        ch = raw[i]
        if ch == "\\" and i + 1 < len(raw):
            buf += ESCAPES.get(raw[i + 1], raw[i + 1])
            i += 2
        elif raw.startswith("${", i) and "}" in raw[i:]:
            end = raw.index("}", i)
            if buf:
                parts.append(buf)
                buf = ""
            parts.append(Parser(tokenize(raw[i + 2:end])).parse_expr())
            i = end + 1
        else:
            buf += ch
            i += 1
    if buf or not parts:
        parts.append(buf)
    return ("str", parts)


# ---------------------------------------------------------------- 런타임

class Env:
    def __init__(self, parent=None):
        self.vars = {}
        self.parent = parent

    def find(self, name):
        env = self
        while env:
            if name in env.vars:
                return env
            env = env.parent
        return None

    def get(self, name):
        env = self.find(name)
        if env is None:
            raise GNEError(f"{BAD_VARIABLE}: '{name}'")
        val = env.vars[name]
        return val.get() if isinstance(val, Proxy) else val

    def set(self, name, value):
        env = self.find(name)
        if env is None:  # 공천 없이 쓰면 전역 창조
            env = self
            while env.parent:
                env = env.parent
        cur = env.vars.get(name)
        if isinstance(cur, Proxy):
            cur.env.set(cur.name, value)
        else:
            env.vars[name] = value


class Proxy:
    """`(비선: 순실)` - 다른 변수인 척하는 변수."""

    def __init__(self, env, name):
        self.env, self.name = env, name

    def get(self):
        return self.env.get(self.name)


class Obj:
    def __init__(self, cls):
        self.cls = cls
        self.fields = {}


class Func:
    def __init__(self, name, params, body, env):
        self.name, self.params, self.body, self.env = name, params, body, env


class Namespace(dict):
    pass


def show(v):
    if v is None:
        return "null"
    if v is True:
        return "친박"
    if v is False:
        return "비박"
    if isinstance(v, list):
        return "[" + ", ".join(show(x) for x in v) + "]"
    if isinstance(v, Obj):
        return f"<{v.cls}>"
    if isinstance(v, Func):
        return f"<함수 {v.name}>"
    return str(v)


def num(v):
    if v is None:
        return 0
    if isinstance(v, bool):
        return int(v)
    return v


class Interpreter:
    def __init__(self, out=None):
        self.out = out or sys.stdout
        self.globals = Env()
        self.classes = {}
        g = self.globals.vars
        g["콘솔"] = Namespace(로그=self.log)
        for name in ("최순실", "흙수저", "금수저", "승마선수", "인턴"):
            g[name] = name

    def log(self, *args):
        print(" ".join(show(a) for a in args), file=self.out)

    def run(self, stmts):
        self.hoist(stmts, self.globals)
        try:
            self.exec_all(stmts, self.globals)
        except ReturnSignal:
            pass

    def hoist(self, stmts, env):
        for s in stmts:
            if s[0] == "func":
                env.vars[s[1]] = Func(s[1], s[2], s[3], env)
            elif s[0] == "class":
                self.classes[s[1]] = s[2]

    def exec_all(self, stmts, env):
        for s in stmts:
            self.exec(s, env)

    def exec(self, s, env):
        kind = s[0]
        if kind == "expr":
            self.ev(s[1], env)
        elif kind == "var":
            _, name, init, is_array = s
            env.vars[name] = self.ev(init, env) if init else ([] if is_array else None)
        elif kind == "block" or kind == "main":
            body = s[1] if kind == "main" else s
            inner = Env(env)
            self.hoist(body[1], inner)
            self.exec_all(body[1], inner)
        elif kind == "if":
            if self.ev(s[1], env):
                self.exec(s[2], env)
            elif s[3]:
                self.exec(s[3], env)
        elif kind == "while":
            try:
                while self.ev(s[1], env):
                    self.exec(s[2], env)
            except BreakSignal:
                pass
        elif kind == "try":
            try:
                self.exec(s[1], env)
            except (GNEError, RecursionError, ZeroDivisionError) as e:
                if s[2] is None:
                    return
                self.exec(s[2], env)
        elif kind == "break":
            raise BreakSignal()
        elif kind == "return":
            raise ReturnSignal(self.ev(s[1], env) if s[1] else None)
        elif kind == "exit":
            raise ExitSignal()
        elif kind == "func":
            env.vars[s[1]] = Func(s[1], s[2], s[3], env)
        elif kind == "trace":
            print(f"[추적] 우리의 목표는, 진행하는 것 그것으로 (줄 {s[1]})", file=sys.stderr)
        elif kind == "breakpoint":
            print(f"[메르스] 중단점 (줄 {s[1]}) - 격리 없이 계속 진행합니다", file=sys.stderr)
        elif kind == "gc":
            print("[빨간색 마티즈] 쓰레기 수거 완료 -> 남산", file=sys.stderr)
        # class / noop 은 할 일이 없다

    def ev(self, n, env):
        kind = n[0]
        if kind == "num":
            return n[1]
        if kind == "bool":
            return n[1]
        if kind == "str":
            return "".join(p if isinstance(p, str) else show(self.ev(p, env)) for p in n[1])
        if kind == "id":
            return env.get(n[1])
        if kind == "list":
            return [self.ev(x, env) for x in n[1]]
        if kind == "proxy":
            return Proxy(env, n[1])
        if kind == "member":
            return self.member(self.ev(n[1], env), n[2])
        if kind == "index":
            return self.index(self.ev(n[1], env), self.ev(n[2], env))
        if kind == "assign":
            return self.assign(n[1], self.ev(n[2], env), env)
        if kind == "un":
            v = self.ev(n[2], env)
            return (not v) if n[1] == "!" else -num(v)
        if kind == "bin":
            return self.binop(n[1], n[2], n[3], env)
        if kind == "new":
            return self.instantiate(n[1], env)
        if kind == "call":
            return self.call(n, env)
        raise GNEError(f"알 수 없는 노드 {kind}")

    def member(self, obj, name):
        if isinstance(obj, Obj):
            return obj.fields.get(name)
        if isinstance(obj, Namespace):
            if name in obj:
                return obj[name]
            raise GNEError(f"{BAD_VARIABLE}: '{name}'")
        if isinstance(obj, (list, str)):
            if name == "길이":
                return len(obj)
            if name == "추가" and isinstance(obj, list):
                return lambda *xs: obj.extend(xs)
        raise GNEError(f"{BAD_VARIABLE}: '{name}' 을(를) 읽을 수 없다")

    def index(self, obj, idx):
        if isinstance(obj, (list, str)) and isinstance(idx, int):
            return obj[idx] if -len(obj) <= idx < len(obj) else None
        raise GNEError(f"{BAD_VARIABLE}: 인덱스 접근 불가")

    def assign(self, target, value, env):
        kind = target[0]
        if kind == "id":
            env.set(target[1], value)
        elif kind == "member":
            obj = self.ev(target[1], env)
            if not isinstance(obj, Obj):
                raise GNEError(f"{BAD_VARIABLE}: '{target[2]}' 에 대입할 수 없다")
            obj.fields[target[2]] = value
        else:
            obj = self.ev(target[1], env)
            idx = self.ev(target[2], env)
            if not isinstance(obj, list) or not isinstance(idx, int) or idx < 0:
                raise GNEError(f"{BAD_VARIABLE}: 인덱스 대입 불가")
            obj.extend([None] * (idx + 1 - len(obj)))
            obj[idx] = value
        return value

    def binop(self, op, l, r, env):
        if op == "&&":
            a = self.ev(l, env)
            return self.ev(r, env) if a else a
        if op == "||":
            a = self.ev(l, env)
            return a if a else self.ev(r, env)
        a, b = self.ev(l, env), self.ev(r, env)
        if op == "==":
            return a == b
        if op == "!=":
            return a != b
        if op == "+" and (isinstance(a, str) or isinstance(b, str)):
            return show(a) + show(b)
        try:
            a, b = (num(a), num(b)) if not (isinstance(a, str) and isinstance(b, str)) else (a, b)
            if op == "+":
                return a + b
            if op == "-":
                return a - b
            if op == "*":
                return a * b
            if op == "/":
                if b == 0:
                    raise GNEError("0으로 나눌 수 없다 (복지 재원 부족)")
                q = a / b
                return int(q) if isinstance(a, int) and isinstance(b, int) and a % b == 0 else q
            if op == "%":
                return a % b
            return {"<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[op]
        except TypeError:
            raise GNEError(f"{BAD_VARIABLE}: '{op}' 연산 불가")

    def instantiate(self, name, env):
        if name not in self.classes:
            raise GNEError(f"{BAD_VARIABLE}: '{name}' 은(는) 창조할 수 없다")
        obj = Obj(name)
        for _, fname, init, is_array in self.classes[name]:
            obj.fields[fname] = self.ev(init, self.globals) if init else ([] if is_array else None)
        return obj

    def call(self, n, env):
        fn = self.ev(n[1], env)
        args = [self.ev(a, env) for a in n[2]]
        if isinstance(fn, Func):
            local = Env(fn.env)
            for i, p in enumerate(fn.params):
                local.vars[p] = args[i] if i < len(args) else None
            self.hoist(fn.body[1], local)
            try:
                self.exec_all(fn.body[1], local)
            except ReturnSignal as r:
                return r.value
            return None
        if callable(fn):
            return fn(*args)
        raise GNEError(f"{BAD_VARIABLE}: 함수가 아니다 (줄 {n[3]})")


# ---------------------------------------------------------------- 실행

def read_source(path):
    raw = open(path, "rb").read()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        return raw.decode("utf-16")
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("cp949", errors="replace")  # EUC-KR 만 지원합니다 (원칙)


def run_file(path):
    try:
        src = read_source(path)
    except OSError as e:
        print(f"해내겠습니다: 파일을 열 수 없습니다 - {e}", file=sys.stderr)
        return 2
    if re.search(r"gmail", src, re.I):
        print(GMAIL_WARNING, file=sys.stderr)
    try:
        program = Parser(tokenize(src)).parse_program()
        Interpreter().run(program)
    except GNESyntaxError as e:
        print(f"안보 경고: {e}", file=sys.stderr)
        return 2
    except GNEError as e:
        print(f"제가 뭐라고 했습니까? {e}", file=sys.stderr)
        return 1
    except ExitSignal:
        pass
    except BreakSignal:
        pass
    return 0


def trust_process(root="."):
    """대북정책_* 파일을 테스트로 돌린다."""
    tests = sorted(glob.glob(os.path.join(root, "대북정책_*")))
    if not tests:
        print("대북정책이 없습니다. 신뢰를 쌓을 수 없습니다.")
        return 1
    failed = 0
    for t in tests:
        code = run_file(t)
        print(("신뢰 " if code == 0 else "종북 ") + t)
        failed += code != 0
    print(f"{len(tests) - failed}/{len(tests)} 신뢰 구축 완료")
    return 1 if failed else 0


def main(argv):
    if len(argv) < 2:
        print("사용법: 해내겠습니다 <파일.ㄹ> | 해내겠습니다 신뢰_프로세스", file=sys.stderr)
        return 2
    if argv[1] == "신뢰_프로세스":
        return trust_process()
    return run_file(argv[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
