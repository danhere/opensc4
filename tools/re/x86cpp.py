#!/usr/bin/env python3
"""x86cpp.py <listing> <first> <last> [call=ADDR:argbytes:cleanup:ret ...]

The listing is `objdump -d -M intel --no-show-raw-insn` of the game's exe with
each line cut down to "<address>  <mnemonic> <operands>".

Translates a whole function (objdump -M intel listing) into C++ statements over
an emulated machine: integer registers as uint32_t, memory through pointers,
the x87 stack as doubles (the game runs it at 53-bit precision), the frame as a
local array. Branches become gotos. Calls go to the real functions: each needs
call=ADDR:argbytes:cleanup:ret with cleanup 'callee' (a thiscall, this in ECX)
or 'caller' (a cdecl call, the code's own `add esp` cleans up) and ret 'int',
'float' or 'void'; indirect calls use call=@SITE:... keyed by the call's own
address. With exe=<path> (and objdump on the PATH) direct calls without an
annotation are worked out: the argument bytes from the callee's first `ret N`,
or from an `add esp,N` right after the call (then the caller cleans up), and a
float result when the next instruction uses the x87 stack's top. The guesses
are listed on stderr. A `jmp [reg*4+table]` becomes a switch over the table's
entries, read from the exe. With dyn=1 every call without an annotation becomes
DYNCALL(target): the including code runs the callee on a copy of the emulated
stack's top with the emulated registers loaded, and reads back the registers,
how many bytes the callee took off the stack and whether it left a result on
the x87 stack, so nothing about the callee has to be known. 0x009EEF04 (the C runtime's _ftol2) is built in. Anything
not handled stops with an error.
"""
import re, subprocess, sys
first, last = int(sys.argv[2], 16), int(sys.argv[3], 16)
calls = {}
exe = None
dyn = False
for a in sys.argv[4:]:
    k, v = a.split('=', 1)
    if k == 'exe':
        exe = v
        continue
    if k == 'dyn':
        dyn = v == '1'
        continue
    assert k == 'call'
    addr, nbytes, cleanup, ret = v.split(':')
    calls[addr.lower()] = (int(nbytes), cleanup, ret)
ins = []
for line in open(sys.argv[1]):
    m = re.match(r'([0-9a-f]+)\s+(\S+)\s*(.*)', line.rstrip())
    if m and first <= int(m.group(1), 16) <= last:
        ins.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
def callee_ret(target):
    text = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', '--start-address=0x%x' % target,
                           '--stop-address=0x%x' % (target + 0x4000), exe], capture_output=True, text=True).stdout
    m = re.search(r'\tret\s*(0x[0-9a-f]+)?', text)
    assert m, 'no ret found for %x' % target
    # A function that ends in a jump to another has no `ret` of its own; the
    # first one found would be the next function's.
    tail = re.search(r'\tjmp\s+(DWORD PTR|e[a-ds][xip])', text[:m.start()])
    assert not tail, 'call to %x: it tail-jumps before any ret; give call=0x%x:argbytes:cleanup:ret' % (target, target)
    return int(m.group(1), 16) if m.group(1) else 0
USES_TOP = ('fstp', 'fst', 'fmul', 'fadd', 'fsub', 'fdiv', 'fsubr', 'fdivr', 'fcomp', 'fcom', 'fxch', 'fchs', 'fabs',
            'faddp', 'fsubp', 'fmulp', 'fdivp')
if exe:
    for i, (a, mn, rest) in enumerate(ins):
        if dyn or mn != 'call' or not rest.startswith('0x') or rest.lower() in calls or rest.lower() == '0x9eef04':
            continue
        nxt = ins[i + 1] if i + 1 < len(ins) else (0, '', '')
        m = re.fullmatch(r'esp,0x([0-9a-f]+)', nxt[2]) if nxt[1] == 'add' else None
        if m:
            nbytes, cleanup = int(m.group(1), 16), 'caller'
            nxt = ins[i + 2] if i + 2 < len(ins) else (0, '', '')
        else:
            nbytes, cleanup = callee_ret(int(rest, 16)), 'callee'
        ret = 'float' if nxt[1] in USES_TOP else 'int'
        calls[rest.lower()] = (nbytes, cleanup, ret)
        print('guessed call=%s:%d:%s:%s' % (rest.lower(), nbytes, cleanup, ret), file=sys.stderr)
def table_entries(table, lo, hi):
    """The dwords of a jump table in the exe that point into [lo, hi]."""
    import struct
    d = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    for i in range(nsec):
        vs, va, rs, ro = struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8)
        if va <= table - base < va + max(vs, rs):
            at = ro + table - base - va
            break
    out = []
    while True:
        v = struct.unpack_from('<I', d, at + 4 * len(out))[0]
        if not lo <= v <= hi or len(out) > 255:
            return out
        out.append(v)
JUMP_TABLE = r'DWORD PTR \[(e\w\w)\*4\+0x([0-9a-f]+)\]'
targets = set()
tables = {}
for a, mn, rest in ins:
    if mn == 'jmp' and re.fullmatch(JUMP_TABLE, rest):
        assert exe, 'a jump table needs exe=<path>'
        m = re.fullmatch(JUMP_TABLE, rest)
        tables[a] = (m.group(1), table_entries(int(m.group(2), 16), ins[0][0], ins[-1][0]))
        targets.update(tables[a][1])
    elif mn.startswith('j') and re.fullmatch(r'0x[0-9a-f]+', rest) and first <= int(rest, 16) <= last:
        targets.add(int(rest, 16))
R32 = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')
R8 = {'al': ('eax', 0), 'bl': ('ebx', 0), 'cl': ('ecx', 0), 'dl': ('edx', 0), 'ah': ('eax', 8), 'bh': ('ebx', 8),
      'ch': ('ecx', 8), 'dh': ('edx', 8)}
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
R16 = {'ax': 'eax', 'bx': 'ebx', 'cx': 'ecx', 'dx': 'edx', 'si': 'esi', 'di': 'edi', 'bp': 'ebp'}
def rd32(op):
    if op in R32: return op
    if op in R16: return '(%s & 0xFFFF)' % R16[op]
    if op in R8:
        r, sh = R8[op]; return '((%s >> %d) & 0xFF)' % (r, sh)
    k = mem(op)
    if k:
        t = {'DWORD': 'uint32_t', 'BYTE': 'uint8_t', 'WORD': 'uint16_t'}[k[0]]
        return 'M<%s>(%s)' % (t, k[1])
    m = re.fullmatch(r'ds:0x([0-9a-f]+)', op)
    if m:
        return 'M<%s>(0x%08Xu)' % (bare_type, int(m.group(1), 16))
    return '0x%Xu' % int(op, 16)
def wr(op, val):
    m = re.fullmatch(r'ds:0x([0-9a-f]+)', op)
    if m:
        E('M<%s>(0x%08Xu) = static_cast<%s>(%s);' % (bare_type, int(m.group(1), 16), bare_type, val)); return
    if op in R32: E('%s = %s;' % (op, val)); return
    if op in R16:
        E('%s = (%s & 0xFFFF0000) | (static_cast<uint32_t>(%s) & 0xFFFF);' % (R16[op], R16[op], val)); return
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
bare_type = 'uint32_t'
flag = None  # ('cmp', a, b) | ('test', a, b) | ('test8', expr)
def cond(mn):
    kind = flag[0]
    if kind == 'cmp':
        a, b = flag[1], flag[2]
        if mn in ('jns', 'js'):
            return 'static_cast<int32_t>(%s - %s) %s 0' % (a, b, '>=' if mn == 'jns' else '<')
        s = lambda x: 'static_cast<int32_t>(%s)' % x
        return {'je': '%s == %s' % (a, b), 'jne': '%s != %s' % (a, b), 'jb': '%s < %s' % (a, b), 'jae': '%s >= %s' % (a, b),
                'jbe': '%s <= %s' % (a, b), 'ja': '%s > %s' % (a, b), 'jl': '%s < %s' % (s(a), s(b)), 'jge': '%s >= %s' % (s(a), s(b)),
                'jle': '%s <= %s' % (s(a), s(b)), 'jg': '%s > %s' % (s(a), s(b))}[mn]
    v = flag[1]
    if mn in ('je', 'jbe'): return '(%s) == 0' % v
    if mn in ('jne', 'ja'): return '(%s) != 0' % v
    if mn == 'jp': return 'Even(%s)' % v
    if mn in ('jns', 'js'): return 'static_cast<%s>(%s) %s 0' % (flag[2], v, '>=' if mn == 'jns' else '<')
    if mn == 'jnp': return '!Even(%s)' % v
    w = flag[2]
    s = 'static_cast<%s>(%s)' % (w, v)
    return {'jge': '%s >= 0' % s, 'jl': '%s < 0' % s, 'jle': '%s <= 0' % s, 'jg': '%s > 0' % s}[mn]
FLAG_SETTERS = ('cmp', 'test', 'add', 'sub', 'and', 'or', 'xor', 'inc', 'dec', 'neg', 'shl', 'shr', 'sar', 'imul', 'mul',
                'adc', 'sbb', 'idiv', 'div', 'shrd', 'call')
def reads_flags_first(address):
    # Whether the code from `address` on uses the flags before setting them.
    for a2, mn2, _ in ins:
        if a2 < address:
            continue
        if mn2 in ('adc', 'sbb') or mn2.startswith('set') or (mn2.startswith('j') and mn2 != 'jmp'):
            return True
        if mn2 in FLAG_SETTERS or mn2 in ('jmp', 'ret'):
            return False
    return False
def note_jump(target):
    # Forward: remember the flags for the label. Backward: the label was
    # emitted with some flags already; a different kind is reported.
    if target in seen_labels:
        was = seen_labels[target]
        if flag is not None and was is not None and flag[0] != was[0] and reads_flags_first(target):
            print('note: jump back to %x with flags of another kind' % target, file=sys.stderr)
    else:
        label_flag.setdefault(target, flag)
last_neg = neg_nonzero = a_now = None
label_flag, seen_labels, falls_through = {}, {}, True
for a, mn, rest in ins:
    a_prev, a_now = a_now, a
    if a in targets:
        out.append('L_%x:;' % a)
        # The flags at a label are those of the jump that came here when the
        # code above cannot fall into it; kinds that disagree are reported.
        if a in label_flag:
            if not falls_through:
                flag = label_flag[a]
            elif flag is not None and label_flag[a] is not None and flag[0] != label_flag[a][0] and reads_flags_first(a):
                print('note: flags of different kinds meet at %x' % a, file=sys.stderr)
        seen_labels[a] = flag
    falls_through = mn not in ('jmp', 'ret')
    ops = [o.strip() for o in re.split(r',(?![^\[]*\])', rest)] if rest else []
    bare_type = 'uint8_t' if any(o in R8 for o in ops) else 'uint16_t' if any(o in R16 for o in ops) else 'uint32_t'
    if mn == 'mov' and ops[0] == ops[1]: pass  # padding
    elif mn == 'mov': wr(ops[0], rd32(ops[1]))
    elif mn == 'movzx': wr(ops[0], rd32(ops[1]))
    elif mn == 'movsx':
        k = mem(ops[1])
        if k:
            t = {'BYTE': 'int8_t', 'WORD': 'int16_t'}[k[0]]
            wr(ops[0], 'static_cast<uint32_t>(static_cast<int32_t>(M<%s>(%s)))' % (t, k[1]))
        elif ops[1] in R8:
            wr(ops[0], 'static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(%s)))' % rd32(ops[1]))
        else:
            assert ops[1] in R16, ops[1]
            wr(ops[0], 'static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(%s)))' % rd32(ops[1]))
    elif mn == 'lea':
        m = re.fullmatch(r'\[(.+)\]', ops[1]); wr(ops[0], addr_expr(m.group(1)))
    elif mn in ('add', 'sub', 'and', 'or', 'xor', 'imul') and len(ops) == 2:
        sym = {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^', 'imul': '*'}[mn]
        if mn == 'sub' and ops[0] != 'esp':
            # The flags of a subtraction are a comparison's.
            E('fa = %s; fb = %s;' % (rd32(ops[0]), rd32(ops[1])))
            wr(ops[0], 'fa - fb'); flag = ('cmp', 'fa', 'fb')
        else:
            src_before = rd32(ops[1])
            if mn == 'add' and ops[0] != 'esp' and ops[1] == ops[0]:
                E('fb = %s;' % rd32(ops[1])); src_before = 'fb'
            wr(ops[0], '%s %s %s' % (rd32(ops[0]), sym, rd32(ops[1])))
            if ops[0] != 'esp':
                if mn == 'add':
                    # fb keeps the operand: the add's carry is "result below operand".
                    if src_before == 'fb':
                        E('fa = %s;' % rd32(ops[0]))
                    else:
                        E('fb = %s; fa = %s;' % (src_before, rd32(ops[0])))
                    flag = ('test', 'fa', 'int32_t', 'add')
                else:
                    E('fa = %s;' % rd32(ops[0])); flag = ('test', 'fa', 'int32_t')
    elif mn in ('inc', 'dec'):
        wr(ops[0], '%s %s 1' % (rd32(ops[0]), '+' if mn == 'inc' else '-')); E('fa = %s;' % rd32(ops[0])); flag = ('test', 'fa', 'int32_t')
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
        src = rd32(ops[0])
        if 'esp' in src:
            # The operand is read before the stack pointer moves.
            E('{ const uint32_t pushedValue = %s; esp -= 4; M<uint32_t>(esp) = pushedValue; }' % src)
        else:
            E('esp -= 4; M<uint32_t>(esp) = %s;' % src)
    elif mn == 'pop':
        E('%s = M<uint32_t>(esp); esp += 4;' % ops[0])
    elif mn == 'jmp' and a in tables:
        reg, entries = tables[a]
        E('switch (%s) {' % reg)
        for i, target in enumerate(entries):
            E('case %d: goto L_%x;' % (i, target))
        E('default: __debugbreak();')
        E('}')
    elif mn == 'jmp' and (not re.fullmatch(r'0x[0-9a-f]+', rest) or not first <= int(rest, 16) <= last):
        # A tail call: the callee returns to our caller, so it runs without
        # our return address on the stack, and its result is ours.
        E('esp += 4;')
        E('DYNCALL(%s);' % (rest if rest.startswith('0x') else rd32(rest)))
        E('return eax;')
    elif mn == 'jmp':
        note_jump(int(rest, 16))
        E('goto L_%x;' % int(rest, 16))
    elif mn.startswith('j'):
        note_jump(int(rest, 16))
        E('if (%s) goto L_%x;' % (cond(mn), int(rest, 16)))
    elif mn == 'ret':
        E('return eax;')
    elif mn == 'call' and rest.lower() == '0x9eef04':
        E('{ const int64_t truncated = static_cast<int64_t>(ST(0)); POP(); eax = static_cast<uint32_t>(truncated); '
          'edx = static_cast<uint32_t>(static_cast<uint64_t>(truncated) >> 32); }')
    elif mn == 'call' and dyn and (rest.lower() if rest.startswith('0x') else '@%x' % a) not in calls:
        E('DYNCALL(%s);' % (rest if rest.startswith('0x') else rd32(rest)))
    elif mn == 'call':
        key = rest.lower() if rest.startswith('0x') else '@%x' % a
        assert key in calls, 'call at %x: %s (key %s)' % (a, rest, key)
        nbytes, cleanup, ret = calls[key]
        target = rest if rest.startswith('0x') else rd32(rest)
        args = ''.join(', M<uint32_t>(esp + %d)' % i for i in range(0, nbytes, 4))
        rt = {'int': 'uint32_t', 'float': 'double', 'void': 'void'}[ret]
        if cleanup == 'caller':
            sig = 'reinterpret_cast<%s(__cdecl*)(%s)>(%s)(%s)' % (rt, ', '.join(['uint32_t'] * (nbytes // 4)), target, args[2:])
        else:
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
    elif mn in ('fcom', 'fcomp', 'fucom', 'fucomp'):
        E('FCOM(%s);' % (fsrc(ops[0]) if ops else 'ST(1)'))
        if mn in ('fcomp', 'fucomp'): E('POP();')
    elif mn in ('fcompp', 'fucompp'):
        E('FCOM(ST(1)); POP(); POP();')
    elif mn == 'cdq':
        E('edx = static_cast<int32_t>(eax) < 0 ? 0xFFFFFFFFu : 0u;')
    elif mn == 'idiv':
        E('{ const int64_t dividend = static_cast<int64_t>((static_cast<uint64_t>(edx) << 32) | eax); '
          'const int32_t divisor = static_cast<int32_t>(%s); eax = static_cast<uint32_t>(dividend / divisor); '
          'edx = static_cast<uint32_t>(dividend %% divisor); }' % rd32(ops[0]))
    elif mn == 'div':
        E('{ const uint64_t dividend = (static_cast<uint64_t>(edx) << 32) | eax; const uint32_t divisor = %s; '
          'eax = static_cast<uint32_t>(dividend / divisor); edx = static_cast<uint32_t>(dividend %% divisor); }' % rd32(ops[0]))
    elif mn == 'rep' and rest.startswith('stos'):
        size, ty, val = (4, 'uint32_t', 'eax') if 'DWORD' in rest else (1, 'uint8_t', 'static_cast<uint8_t>(eax)')
        E('for (; ecx; --ecx) { M<%s>(edi) = %s; edi += %d; }' % (ty, val, size))
    elif mn == 'rep' and rest.startswith('movs'):
        size, ty = (4, 'uint32_t') if 'DWORD' in rest else (1, 'uint8_t')
        E('for (; ecx; --ecx) { M<%s>(edi) = M<%s>(esi); edi += %d; esi += %d; }' % (ty, ty, size, size))
    elif mn == 'adc':
        assert flag and len(flag) == 4 and flag[3] == 'add', 'adc at %x without an add setting the flags before it' % a
        wr(ops[0], '%s + %s + (fa < fb ? 1u : 0u)' % (rd32(ops[0]), rd32(ops[1])))
        E('fa = %s;' % rd32(ops[0])); flag = ('test', 'fa', 'int32_t')
    elif mn == 'shrd':
        n = int(ops[2], 16)
        wr(ops[0], '(%s >> %d) | (%s << %d)' % (rd32(ops[0]), n, rd32(ops[1]), 32 - n))
    elif mn == 'mul':
        E('{ const uint64_t product = static_cast<uint64_t>(eax) * %s; '
          'eax = static_cast<uint32_t>(product); edx = static_cast<uint32_t>(product >> 32); }' % rd32(ops[0]))
    elif mn == 'imul' and len(ops) == 1:
        E('{ const int64_t product = static_cast<int64_t>(static_cast<int32_t>(eax)) * static_cast<int32_t>(%s); '
          'eax = static_cast<uint32_t>(product); edx = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32); }' % rd32(ops[0]))
    elif mn in ('fidiv', 'fimul', 'fiadd', 'fisub'):
        k = mem(ops[0]); ty = 'int16_t' if k[0] == 'WORD' else 'int32_t'
        E('ST(0) = ST(0) %s static_cast<double>(M<%s>(%s));' % ({'fidiv': '/', 'fimul': '*', 'fiadd': '+', 'fisub': '-'}[mn], ty, k[1]))
    elif mn.startswith('set'):
        wr(ops[0], '(%s) ? 1u : 0u' % cond('j' + mn[3:]))
    elif mn == 'not':
        wr(ops[0], '~%s' % rd32(ops[0]))
    elif mn == 'fsqrt': E('ST(0) = std::sqrt(ST(0));')
    elif mn == 'fsin': E('ST(0) = X87Sin(ST(0));')
    elif mn == 'fpatan':
        E('ST(1) = X87Atan2(ST(1), ST(0));')
        E('POP();')
    elif mn == 'fcos': E('ST(0) = X87Cos(ST(0));')
    elif mn == 'sbb' and ops[0] == ops[1]:
        # Only right after a neg, whose carry is "the operand was not zero".
        assert last_neg is not None and last_neg == a_prev, 'sbb at %x without a neg before it' % a
        wr(ops[0], '(%s) != 0 ? 0xFFFFFFFFu : 0u' % neg_nonzero)
    elif mn == 'neg':
        wr(ops[0], '0u - %s' % rd32(ops[0])); flag = ('test', rd32(ops[0]), 'int32_t')
        last_neg = a; neg_nonzero = rd32(ops[0])
    elif mn == 'imul' and len(ops) == 3:
        wr(ops[0], '%s * %s' % (rd32(ops[1]), rd32(ops[2])))
    elif mn == 'fnstsw': E('eax = (eax & 0xFFFF0000) | (status << 8);')
    elif mn == 'nop': pass
    else:
        raise SystemExit('unhandled at %x: %s %s' % (a, mn, rest))
print('\n'.join(out))
