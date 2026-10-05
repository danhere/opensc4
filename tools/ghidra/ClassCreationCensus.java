// Answers the P0 class-creation question: which classes the game creates
// through GZCOM factories (swappable by class ID, no patching) and which it
// constructs directly with operator new (these need vtable swaps or detours).
//
// The Windows exe has no RTTI for game classes, so a class is identified by
// its vtable:
//   - a "vtable store" is MOV dword ptr [reg], imm32 where imm32 points to
//     non-executable memory whose first dword points to executable code;
//   - a "constructor" is a function whose last such store names the class
//     (destructors match too; their callers end up in the "other" bucket);
//   - each call to a constructor is classified by its caller: a registered
//     GZCOM factory, another constructor (base-class or member setup), a
//     `new` site (allocator call, then MOV ECX,EAX, then the constructor),
//     or anything else (stack or member objects, destructor calls);
//   - a factory that stores a vtable itself has its constructor inlined.
//
// Input: the CSV written by ClassRegistrationCensus.java. Factory addresses
// are disassembled and turned into functions first (in memory only when run
// with -readOnly), because nothing calls them directly.
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <repo>/tools/ghidra -postScript ClassCreationCensus.java
//     <class-registrations.csv> <class-creation.csv>
//
// The output describes the game's code and must not be committed.
//@category OpenSC4

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class ClassCreationCensus extends GhidraScript {

    // Per class (primary vtable): how its instances come into being.
    private static class ClassInfo {
        final Set<Address> ctors = new HashSet<>();
        final Set<Long> clsids = new HashSet<>();
        int factoryCalls, factoryInline, ctorCalls, newCalls, otherCalls;
        final Set<Address> newCallers = new HashSet<>();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: ClassCreationCensus.java <class-registrations.csv> <out.csv>");
            return;
        }

        // 1. Registered factories (and the class ID each one serves).
        Map<Address, Long> factoryClsid = new HashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                String[] f = line.split(",");
                factoryClsid.put(toAddr(f[4]), Long.decode(f[3]));
            }
        }
        int created = 0;
        for (Address a : factoryClsid.keySet()) {
            if (getFunctionAt(a) != null)
                continue;
            if (getInstructionAt(a) == null)
                disassemble(a);
            if (createFunction(a, null) != null)
                created++;
        }
        println("Factories: " + factoryClsid.size() + " (" + created + " functions created)");
        Set<Function> factories = new HashSet<>();
        for (Address a : factoryClsid.keySet()) {
            Function f = getFunctionAt(a);
            if (f != null)
                factories.add(f);
        }

        // 2. Vtable stores; each function's last offset-0 store names its class.
        Map<Function, Address> primaryVtable = new HashMap<>();
        Set<Address> allVtables = new HashSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction insn = it.next();
            Address vtable = vtableStoredAtOffsetZero(insn);
            if (vtable == null)
                continue;
            allVtables.add(vtable);
            Function fn = getFunctionContaining(insn.getAddress());
            if (fn != null)
                primaryVtable.put(fn, vtable); // instructions arrive in address order
        }
        println("Distinct vtables stored at offset 0: " + allVtables.size()
            + ", constructor-like functions: " + primaryVtable.size());

        // 3. Factories with inlined constructors, then every call to every
        //    other constructor-like function.
        Map<Address, ClassInfo> classes = new TreeMap<>();
        Map<Address, Integer> allocators = new HashMap<>();
        for (Function f : factories) {
            Address vtable = primaryVtable.get(f);
            if (vtable == null)
                continue;
            ClassInfo info = classes.computeIfAbsent(vtable, k -> new ClassInfo());
            info.factoryInline++;
            info.clsids.add(factoryClsid.get(f.getEntryPoint()));
        }
        for (Map.Entry<Function, Address> e : primaryVtable.entrySet()) {
            Function ctor = e.getKey();
            if (factories.contains(ctor))
                continue;
            ClassInfo info = classes.computeIfAbsent(e.getValue(), k -> new ClassInfo());
            info.ctors.add(ctor.getEntryPoint());
            for (Reference ref : getReferencesTo(ctor.getEntryPoint())) {
                if (!ref.getReferenceType().isCall())
                    continue;
                Function caller = getFunctionContaining(ref.getFromAddress());
                if (caller != null && factories.contains(caller)) {
                    info.factoryCalls++;
                    info.clsids.add(factoryClsid.get(caller.getEntryPoint()));
                } else if (caller != null && primaryVtable.containsKey(caller)) {
                    info.ctorCalls++;
                } else {
                    Address allocator = allocatorBeforeCall(ref.getFromAddress());
                    if (allocator != null) {
                        info.newCalls++;
                        allocators.merge(allocator, 1, Integer::sum);
                        if (caller != null)
                            info.newCallers.add(caller.getEntryPoint());
                    } else {
                        info.otherCalls++;
                    }
                }
            }
        }

        // 4. Summary and per-class CSV.
        int gzOnly = 0, newOnly = 0, both = 0, otherOnly = 0, baseOnly = 0;
        for (ClassInfo c : classes.values()) {
            boolean gz = c.factoryCalls + c.factoryInline > 0, nw = c.newCalls > 0;
            if (gz && nw) both++;
            else if (gz) gzOnly++;
            else if (nw) newOnly++;
            else if (c.otherCalls > 0) otherOnly++;
            else baseOnly++;
        }
        println("Classes (by primary vtable): " + classes.size());
        println("  GZCOM factory only:                     " + gzOnly);
        println("  operator new only:                      " + newOnly);
        println("  both GZCOM and new:                     " + both);
        println("  stack/member/destructor calls only:     " + otherOnly);
        println("  only from other constructors, or never: " + baseOnly);
        println("Allocators before constructor calls:");
        allocators.entrySet().stream()
            .sorted((a, b) -> b.getValue() - a.getValue())
            .limit(5)
            .forEach(e -> println(String.format("  %s  %d sites", e.getKey(), e.getValue())));

        try (PrintWriter w = new PrintWriter(new File(args[1]), "UTF-8")) {
            w.println("vtable,ctors,clsids,factory_calls,factory_inline,ctor_calls,new_calls,new_callers,other_calls");
            for (Map.Entry<Address, ClassInfo> e : classes.entrySet()) {
                ClassInfo c = e.getValue();
                w.printf("%s,%d,%s,%d,%d,%d,%d,%d,%d%n", e.getKey(), c.ctors.size(),
                    String.join(" ", c.clsids.stream().map(id -> String.format("0x%08X", id)).toList()),
                    c.factoryCalls, c.factoryInline, c.ctorCalls, c.newCalls, c.newCallers.size(), c.otherCalls);
            }
        }
        println("Wrote " + new File(args[1]).getAbsolutePath());
    }

    // Recognises the MSVC `new T(...)` shape before a constructor call:
    //   CALL allocator ... MOV ECX,EAX ... CALL ctor
    // (with an optional TEST/JZ null check in between). Returns the allocator.
    private Address allocatorBeforeCall(Address callSite) {
        Instruction insn = getInstructionAt(callSite);
        if (insn == null)
            return null;
        boolean thisFromEax = false;
        insn = insn.getPrevious();
        for (int i = 0; i < 12 && insn != null; i++, insn = insn.getPrevious()) {
            if (!thisFromEax && insn.toString().equals("MOV ECX,EAX")) {
                thisFromEax = true;
                continue;
            }
            if (insn.getFlowType().isCall()) {
                Address[] flows = insn.getFlows();
                return thisFromEax && flows.length == 1 ? flows[0] : null;
            }
        }
        return null;
    }

    // Returns the vtable address if `insn` is MOV dword ptr [reg], vtable.
    private Address vtableStoredAtOffsetZero(Instruction insn) {
        if (!insn.getMnemonicString().equals("MOV") || insn.getNumOperands() != 2)
            return null;
        Scalar value = insn.getScalar(1);
        if (value == null)
            return null;
        Object[] dest = insn.getOpObjects(0);
        if (dest.length != 1 || !(dest[0] instanceof Register reg) || reg.getBitLength() != 32)
            return null;
        if (!insn.getDefaultOperandRepresentation(0).startsWith("dword ptr ["))
            return null;

        Memory mem = currentProgram.getMemory();
        Address vtable;
        try {
            vtable = toAddr(value.getUnsignedValue());
        } catch (Exception e) {
            return null;
        }
        MemoryBlock block = mem.getBlock(vtable);
        if (block == null || block.isExecute() || !block.isInitialized())
            return null;
        try {
            MemoryBlock target = mem.getBlock(toAddr(mem.getInt(vtable) & 0xFFFFFFFFL));
            return target != null && target.isExecute() ? vtable : null;
        } catch (Exception e) {
            return null;
        }
    }
}
