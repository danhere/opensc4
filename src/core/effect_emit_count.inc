// Generated from the game's code 0x005B3CF0 to 0x005B3D95 (the emitter that
// keeps a count of particles) by tools/re/x86cpp.py: an instruction-by-
// instruction translation onto an emulated machine (registers as uint32_t,
// memory through M<T>(address), the x87 stack as doubles, branches as gotos,
// calls through CallThisTraced, which the comparison uses). The listing's
// fucompp was given to the translator as fcompp, which it treats the same.
// Included by water_effects.cpp.
    PUSH(static_cast<double>(M<float>(esp + 0x4u)));
    eax = M<uint32_t>(ecx + 0x128u);
    fa = eax & eax;
    ST(0) = ST(0) * static_cast<double>(M<double>(ecx + 0x130u));
    ST(0) = ST(0) + static_cast<double>(M<double>(ecx + 0x120u));
    M<double>(ecx + 0x120u) = static_cast<double>(ST(0));
    PUSH(static_cast<double>(M<int32_t>(ecx + 0x128u)));
    if (static_cast<int32_t>(fa) >= 0) goto L_5b3d1c;
    ST(0) = ST(0) + static_cast<double>(M<double>(0x00A80AB8u));
L_5b3d1c:;
    std::swap(ST(0), ST(1));
    FCOM(ST(1)); POP(); POP();
    eax = (eax & 0xFFFF0000) | (status << 8);
    fa = ((eax >> 8) & 0xFF) & 0x41u;
    if (!Even(fa)) goto L_5b3d95;
    edx = M<uint32_t>(ecx + 0x24u);
    eax = M<uint32_t>(edx + 0x88u);
    PUSH(static_cast<double>(M<float>(eax)));
    esp -= 4; M<uint32_t>(esp) = esi;
    PUSH(static_cast<double>(M<float>(0x00A84D2Cu)));
    ST(0) = ST(0) + ST(1);
    ST(0) = ST(0) + static_cast<double>(M<float>(0x00AA0CF8u));
    M<float>(esp + 0x8u) = static_cast<float>(ST(0));
    POP();
    edx = M<uint32_t>(esp + 0x8u);
    edx = edx + 0xB4C00000u;
    fa = edx;
    esi = edx - 0x1u;
    M<uint32_t>(esp + 0x8u) = static_cast<uint32_t>(esi);
    PUSH(static_cast<double>(M<int32_t>(esp + 0x8u)));
    std::swap(ST(0), ST(1));
    FCOM(ST(1)); POP(); POP();
    eax = (eax & 0xFFFF0000) | (status << 8);
    fa = ((eax >> 8) & 0xFF) & 0x44u;
    if (Even(fa)) goto L_5b3d67;
    edx = esi;
L_5b3d67:;
    fa = edx; fb = M<uint32_t>(ecx + 0x18u);
    edx = fa - fb;
    if (fa == fb) goto L_5b3d94;
    esp -= 4; M<uint32_t>(esp) = edi;
    esi = ecx + 0x4u;
    edi = edx;
L_5b3d72:;
    edx = M<uint32_t>(esi);
    ecx = esi;
    eax = CallThisTraced(M<uint32_t>(edx + 0x3Cu), reinterpret_cast<void*>(ecx));
    fa = eax & eax;
    if ((fa) == 0) goto L_5b3d90;
    ecx = (ecx & ~(0xFFu << 0)) | ((static_cast<uint32_t>(M<uint8_t>(eax)) & 0xFF) << 0);
    edx = M<uint32_t>(esi);
    fa = ((ecx >> 0) & 0xFF) & ((ecx >> 0) & 0xFF);
    esp -= 4; M<uint32_t>(esp) = eax;
    ecx = esi;
    if ((fa) == 0) goto L_5b3d8d;
    eax = CallThisTraced(M<uint32_t>(edx + 0x2Cu), reinterpret_cast<void*>(ecx), M<uint32_t>(esp + 0));
    esp += 4;
    goto L_5b3d90;
L_5b3d8d:;
    eax = CallThisTraced(M<uint32_t>(edx + 0x40u), reinterpret_cast<void*>(ecx), M<uint32_t>(esp + 0));
    esp += 4;
L_5b3d90:;
    edi = edi - 1;
    fa = edi;
    if ((fa) != 0) goto L_5b3d72;
    edi = M<uint32_t>(esp); esp += 4;
L_5b3d94:;
    esi = M<uint32_t>(esp); esp += 4;
L_5b3d95:;
    return eax;
