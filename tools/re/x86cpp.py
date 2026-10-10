#!/usr/bin/env python3
"""x86cpp.py <listing> <first> <last> [call=ADDR:argbytes:cleanup:ret ...]

The listing is `objdump -d -M intel --no-show-raw-insn` of the game's exe with
each line cut down to "<address>  <mnemonic> <operands>".

Translates a whole function (objdump -M intel listing) into C++ statements over
an emulated machine: integer registers as uint32_t, memory through pointers,
the x87 stack as doubles (the game runs it at 53-bit precision), the frame as a
local array. Branches become gotos. Calls go to the real functions: each needs
call=ADDR:argbytes:cleanup:ret with cleanup 'callee' or 'caller' and ret 'int',
'float' or 'void'; indirect calls use call=@SITE:... keyed by the call's own
address. Anything not handled stops with an error.
"""
import re, sys
first, last = int(sys.argv[2], 16), int(sys.argv[3], 16)
calls = {}
for a in sys.argv[4:]:
    k, v = a.split('=', 1)
    assert k == 'call'
    addr, nbytes, cleanup, ret = v.split(':')
    calls[addr.lower()] = (int(nbytes), cleanup, ret)
ins = []
for line in open(sys.argv[1]):
    m = re.match(r'([0-9a-f]+)\s+(\S+)\s*(.*)', line.rstrip())
    if m and first <= int(m.group(1), 16) <= last:
        ins.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
targets = set()
for a, mn, rest in ins:
    if mn.startswith('j'):
        targets.add(int(rest, 16))
R32 = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')
R8 = {'al': ('eax', 0), 'bl': ('ebx', 0), 'cl': ('ecx', 0), 'dl': ('edx', 0), 'ah': ('eax', 8)}
out = []
def E(s): out.append('    ' + s)
def addr_expr(inner):
    inner = inner.replace(' ', '')
    parts = re.split(r'(?=[+-])', inner)
    terms = []
    for p in parts:
        sign = '-' if p.startswith('-') else '+'
        p = p.lstrip('+-')
        m = re.fullmatch(r'(e\w\w)\*(\d)', p)
        if m: t = '%s * %s' % (m.group(1), m.group(2))
        elif p in R32: t = p
        else: t = '0x%Xu' % int(p, 16)
        terms.append((sign, t))
    s = ''
    for i, (sign, t) in enumerate(terms):
        s += (t if i == 0 and sign == '+' else ' %s %s' % (sign, t))
    return s
def mem(op):
    m = re.fullmatch(r'(DWORD|BYTE|WORD|QWORD) PTR (?:ds:0x([0-9a-f]+)|\[(.+)\])', op)
    if not m: return None
    size = m.group(1)
    a = '0x%08Xu' % int(m.group(2), 16) if m.group(2) else addr_expr(m.group(3))
    return size, a
def rd32(op):
    if op in R32: return op
    if op in R8:
        r, sh = R8[op]; return '((%s >> %d) & 0xFF)' % (r, sh)
    k = mem(op)
    if k:
        t = {'DWORD': 'uint32_t', 'BYTE': 'uint8_t', 'WORD': 'uint16_t'}[k[0]]
        return 'M<%s>(%s)' % (t, k[1])
    return '0x%Xu' % int(op, 16)
def wr(op, val):
    if op in R32: E('%s = %s;' % (op, val)); return
    if op in R8:
        r, sh = R8[op]; E('%s = (%s & ~(0xFFu << %d)) | ((static_cast<uint32_t>(%s) & 0xFF) << %d);' % (r, r, sh, val, sh)); return
    k = mem(op); assert k, op
    t = {'DWORD': 'uint32_t', 'BYTE': 'uint8_t', 'WORD': 'uint16_t'}[k[0]]
    E('M<%s>(%s) = static_cast<%s>(%s);' % (t, k[1], t, val))
def fsrc(op):
    m = re.fullmatch(r'st\((\d)\)', op)
    if m: return 'ST(%s)' % m.group(1)
    if op == 'st': return 'ST(0)'
    k = mem(op); assert k, op
    return 'static_cast<double>(M<%s>(%s))' % ('double' if k[0] == 'QWORD' else 'float', k[1])
flag = None  # ('cmp', a, b) | ('test', a, b) | ('test8', expr)
def cond(mn):
    kind = flag[0]
    if kind == 'cmp':
        a, b = flag[1], flag[2]
        s = lambda x: 'static_cast<int32_t>(%s)' % x
        return {'je': '%s == %s' % (a, b), 'jne': '%s != %s' % (a, b), 'jb': '%s < %s' % (a, b), 'jae': '%s >= %s' % (a, b),
                'jbe': '%s <= %s' % (a, b), 'ja': '%s > %s' % (a, b), 'jl': '%s < %s' % (s(a), s(b)), 'jge': '%s >= %s' % (s(a), s(b)),
                'jle': '%s <= %s' % (s(a), s(b)), 'jg': '%s > %s' % (s(a), s(b))}[mn]
    v = flag[1]
    if mn == 'je': return '(%s) == 0' % v
    if mn == 'jne': return '(%s) != 0' % v
    if mn == 'jp': return 'Even(%s)' % v
    if mn == 'jnp': return '!Even(%s)' % v
    w = flag[2]
    s = 'static_cast<%s>(%s)' % (w, v)
    return {'jge': '%s >= 0' % s, 'jl': '%s < 0' % s, 'jle': '%s <= 0' % s, 'jg': '%s > 0' % s}[mn]
for a, mn, rest in ins:
    if a in targets:
        out.append('L_%x:;' % a)
    ops = [o.strip() for o in re.split(r',(?![^\[]*\])', rest)] if rest else []
    if mn == 'mov': wr(ops[0], rd32(ops[1]))
    elif mn == 'movzx': wr(ops[0], rd32(ops[1]))
    elif mn == 'movsx':
        k = mem(ops[1]); t = {'BYTE': 'int8_t', 'WORD': 'int16_t'}[k[0]]
        wr(ops[0], 'static_cast<uint32_t>(static_cast<int32_t>(M<%s>(%s)))' % (t, k[1]))
    elif mn == 'lea':
        m = re.fullmatch(r'\[(.+)\]', ops[1]); wr(ops[0], addr_expr(m.group(1)))
    elif mn in ('add', 'sub', 'and', 'or', 'xor', 'imul') and len(ops) == 2:
        sym = {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^', 'imul': '*'}[mn]
        wr(ops[0], '%s %s %s' % (rd32(ops[0]), sym, rd32(ops[1])))
        flag = ('test', rd32(ops[0]), 'int32_t')
    elif mn in ('inc', 'dec'):
        wr(ops[0], '%s %s 1' % (rd32(ops[0]), '+' if mn == 'inc' else '-')); flag = ('test', rd32(ops[0]), 'int32_t')
    elif mn in ('shl', 'shr', 'sar'):
        v = rd32(ops[0]); n = rd32(ops[1]) if len(ops) > 1 else '1'
        wr(ops[0], {'shl': '%s << %s', 'shr': '%s >> %s', 'sar': 'static_cast<uint32_t>(static_cast<int32_t>(%s) >> %s)'}[mn] % (v, n))
    elif mn == 'cmp':
        w = 'uint8_t' if ops[0] in R8 or (mem(ops[0]) and mem(ops[0])[0] == 'BYTE') else None
        a_, b_ = rd32(ops[0]), rd32(ops[1])
        if w: a_, b_ = 'static_cast<uint8_t>(%s)' % a_, 'static_cast<uint8_t>(%s)' % b_
        E('fa = %s; fb = %s;' % (a_, b_)); flag = ('cmp', 'fa', 'fb')
    elif mn == 'test':
        w = 'int8_t' if ops[0] in R8 or (mem(ops[0]) and mem(ops[0])[0] == 'BYTE') else 'int32_t'
        E('fa = %s & %s;' % (rd32(ops[0]), rd32(ops[1]))); flag = ('test', 'fa', w)
    elif mn == 'push':
        E('esp -= 4; M<uint32_t>(esp) = %s;' % rd32(ops[0]))
    elif mn == 'pop':
        E('%s = M<uint32_t>(esp); esp += 4;' % ops[0])
    elif mn == 'jmp':
        E('goto L_%x;' % int(rest, 16))
    elif mn.startswith('j'):
        E('if (%s) goto L_%x;' % (cond(mn), int(rest, 16)))
    elif mn == 'ret':
        E('return eax;')
    elif mn == 'call':
        key = rest.lower() if rest.startswith('0x') else '@%x' % a
        assert key in calls, 'call at %x: %s (key %s)' % (a, rest, key)
        nbytes, cleanup, ret = calls[key]
        target = rest if rest.startswith('0x') else rd32(rest)
        args = ''.join(', M<uint32_t>(esp + %d)' % i for i in range(0, nbytes, 4))
        rt = {'int': 'uint32_t', 'float': 'double', 'void': 'void'}[ret]
        sig = 'reinterpret_cast<%s(__thiscall*)(void*%s)>(%s)(reinterpret_cast<void*>(ecx)%s)' % (rt, ', uint32_t' * (nbytes // 4), target, args)
        if ret == 'int': E('eax = %s;' % sig)
        elif ret == 'float': E('PUSH(%s);' % sig)
        else: E('%s;' % sig)
        if cleanup == 'callee' and nbytes: E('esp += %d;' % nbytes)
    elif mn == 'fld': E('PUSH(%s);' % fsrc(ops[0]))
    elif mn == 'fild':
        k = mem(ops[0]); E('PUSH(static_cast<double>(M<int32_t>(%s)));' % k[1])
    elif mn in ('fst', 'fstp'):
        if ops[0].startswith('st'):
            E('ST(%s) = ST(0);' % re.search(r'\d', ops[0]).group(0))
        else:
            k = mem(ops[0]); ty = 'double' if k[0] == 'QWORD' else 'float'
            E('M<%s>(%s) = static_cast<%s>(ST(0));' % (ty, k[1], ty))
        if mn == 'fstp': E('POP();')
    elif mn in ('fadd', 'fsub', 'fmul', 'fdiv', 'fsubr', 'fdivr'):
        sym = {'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/', 'fsubr': '-', 'fdivr': '/'}[mn]
        if len(ops) == 2:
            d, s_ = fsrc(ops[0]), fsrc(ops[1])
        else:
            d, s_ = 'ST(0)', fsrc(ops[0])
        if mn in ('fsubr', 'fdivr'): E('%s = %s %s %s;' % (d, s_, sym, d))
        else: E('%s = %s %s %s;' % (d, d, sym, s_))
    elif mn in ('faddp', 'fsubp', 'fmulp', 'fdivp', 'fsubrp', 'fdivrp'):
        sym = {'faddp': '+', 'fsubp': '-', 'fmulp': '*', 'fdivp': '/', 'fsubrp': '-', 'fdivrp': '/'}[mn]
        d = fsrc(ops[0]) if ops else 'ST(1)'
        if mn in ('fsubrp', 'fdivrp'): E('%s = ST(0) %s %s; POP();' % (d, sym, d))
        else: E('%s = %s %s ST(0); POP();' % (d, d, sym))
    elif mn == 'fchs': E('ST(0) = -ST(0);')
    elif mn == 'fabs': E('ST(0) = std::fabs(ST(0));')
    elif mn == 'fxch':
        i = re.search(r'\d', rest).group(0) if rest else '1'; E('std::swap(ST(0), ST(%s));' % i)
    elif mn in ('fcom', 'fcomp'):
        E('FCOM(%s);' % (fsrc(ops[0]) if ops else 'ST(1)'))
        if mn == 'fcomp': E('POP();')
    elif mn == 'fnstsw': E('eax = (eax & 0xFFFF0000) | (status << 8);')
    elif mn == 'nop': pass
    else:
        raise SystemExit('unhandled at %x: %s %s' % (a, mn, rest))
print('\n'.join(out))
