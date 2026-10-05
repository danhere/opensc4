// For each GZCOM class given on the command line, finds the vtables its
// constructor installs and counts direct calls into those virtual methods
// from code outside the class. Calls through the vtable reach a GZCOM
// replacement automatically; direct calls (devirtualised, or non-virtual
// calls to a method that also sits in a vtable) would keep running the
// original code and need detours.
//
// Inputs: the CSV from ClassRegistrationCensus.java, a CSV of
// "clsid,name" rows to examine, and the output path.
//
// Method, per class:
//   1. factory = registered factory for the class ID;
//   2. ctor = the factory itself if it stores a vtable (inlined), otherwise
//      the last function it calls that stores a vtable at offset 0;
//   3. vtables = the last vtable stored at each displacement by the ctor;
//   4. slots = function pointers from each vtable start until the next
//      referenced address (the next vtable or table);
//   5. "own" functions = slots, ctor and factory; a direct CALL to a slot
//      from any other function is a bypass.
// Base-class methods inherited unchanged also appear in the slots, so
// bypasses into shared base methods are reported separately: a slot used by
// more than one of the examined classes is "shared".
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <repo>/tools/ghidra -postScript VtableBypassCensus.java
//     <class-registrations.csv> <classes.csv> <out.csv>
//
// The output describes the game's code and must not be committed.
//@category OpenSC4

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class VtableBypassCensus extends GhidraScript {

    private static final int MAX_SLOTS = 400;

    private static class ClassResult {
        String name;
        long clsid;
        Address factory, ctor;
        Map<Long, Address> vtables = new TreeMap<>(); // displacement -> vtable
        Set<Address> slots = new HashSet<>();
        Map<Address, String> slotPosition = new HashMap<>(); // slot -> "vtable disp:index"
        // slot -> external callers (function entry points)
        Map<Address, Set<Address>> bypasses = new TreeMap<>();
        String problem = "";
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            printerr("usage: VtableBypassCensus.java <class-registrations.csv> <classes.csv> <out.csv>");
            return;
        }

        Map<Long, Address> factoryByClsid = new HashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                String[] f = line.split(",");
                if (f[1].equals("0090e133") || f[1].equals("0090e160"))
                    factoryByClsid.put(Long.decode(f[3]), toAddr(f[4]));
            }
        }
        Map<Long, String> wanted = new LinkedHashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(args[1]))) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                String[] f = line.split(",");
                wanted.put(Long.decode(f[0]), f[1]);
            }
        }

        List<ClassResult> results = new ArrayList<>();
        Map<Address, Integer> slotUsers = new HashMap<>();
        for (Map.Entry<Long, String> w : wanted.entrySet()) {
            ClassResult c = analyse(w.getKey(), w.getValue(), factoryByClsid.get(w.getKey()));
            results.add(c);
            for (Address s : c.slots)
                slotUsers.merge(s, 1, Integer::sum);
        }

        // One row per (class, bypassed slot, external caller). MSVC keeps each
        // object file's functions together, so a caller inside the address
        // range of the class's own (non-shared) slot functions is most likely
        // one of its non-virtual methods rather than outside code.
        try (PrintWriter out = new PrintWriter(new File(args[2].replaceAll("\\.csv$", "") + "-details.csv"), "UTF-8")) {
            out.println("class,slot_function,vtable_disp_index,shared_with_other_classes,external_caller,in_class_code_range,slot_kind");
            for (ClassResult c : results) {
                Address lo = null, hi = null;
                for (Address s : c.slots) {
                    if (slotUsers.getOrDefault(s, 0) != 1)
                        continue;
                    if (lo == null || s.compareTo(lo) < 0) lo = s;
                    if (hi == null || s.compareTo(hi) > 0) hi = s;
                }
                for (Map.Entry<Address, Set<Address>> b : c.bypasses.entrySet())
                    for (Address caller : b.getValue()) {
                        boolean inRange = lo != null && caller.compareTo(lo) >= 0 && caller.compareTo(hi) <= 0;
                        out.printf("%s,%s,%s,%s,%s,%s,%s%n", c.name, b.getKey(), c.slotPosition.get(b.getKey()),
                            slotUsers.getOrDefault(b.getKey(), 0) > 1, caller, inRange, slotKind(b.getKey()));
                    }
            }
        }

        try (PrintWriter out = new PrintWriter(new File(args[2]), "UTF-8")) {
            out.println("class,clsid,factory,ctor,vtables,slots,bypassed_slots,bypass_callers_total,"
                + "distinct_callers,bypassed_own_slots,bypass_callers_own,problem");
            println(String.format("%-36s %6s %6s  %s", "class", "slots", "own", "outside callers of own (non-shared) virtual methods"));
            for (ClassResult c : results) {
                int calls = 0, ownSlots = 0, ownCalls = 0;
                Set<Address> callers = new HashSet<>();
                for (Map.Entry<Address, Set<Address>> b : c.bypasses.entrySet()) {
                    calls += b.getValue().size();
                    callers.addAll(b.getValue());
                    if (slotUsers.getOrDefault(b.getKey(), 0) == 1) {
                        ownSlots++;
                        ownCalls += b.getValue().size();
                    }
                }
                long own = c.slots.stream().filter(s -> slotUsers.getOrDefault(s, 0) == 1).count();
                out.printf("%s,0x%08X,%s,%s,%d,%d,%d,%d,%d,%d,%d,%s%n", c.name, c.clsid, c.factory, c.ctor,
                    c.vtables.size(), c.slots.size(), c.bypasses.size(), calls, callers.size(), ownSlots, ownCalls, c.problem);
                println(String.format("%-36s %6d %6d  %s", c.name, c.slots.size(), own,
                    c.problem.isEmpty() ? ownCalls + " callers into " + ownSlots + " methods" : c.problem));
            }
        }
        println("Wrote " + new File(args[2]).getAbsolutePath());
    }

    private ClassResult analyse(long clsid, String name, Address factoryAddr) throws Exception {
        ClassResult c = new ClassResult();
        c.name = name;
        c.clsid = clsid;
        c.factory = factoryAddr;
        if (factoryAddr == null) {
            c.problem = "no registered factory";
            return c;
        }
        Function factory = getFunctionAt(factoryAddr);
        if (factory == null) {
            if (getInstructionAt(factoryAddr) == null)
                disassemble(factoryAddr);
            factory = createFunction(factoryAddr, null);
        }
        if (factory == null) {
            c.problem = "factory is not a function";
            return c;
        }

        Function ctor = storesVtable(factory) ? factory : null;
        if (ctor == null) {
            for (Instruction insn : currentProgram.getListing().getInstructions(factory.getBody(), true)) {
                if (!insn.getFlowType().isCall() || insn.getFlows().length != 1)
                    continue;
                Function callee = getFunctionAt(insn.getFlows()[0]);
                if (callee != null && storesVtable(callee))
                    ctor = callee;
            }
        }
        if (ctor == null) {
            c.problem = "constructor not found";
            return c;
        }
        c.ctor = ctor.getEntryPoint();

        for (Instruction insn : currentProgram.getListing().getInstructions(ctor.getBody(), true)) {
            long[] store = vtableStore(insn);
            if (store != null)
                c.vtables.put(store[0], toAddr(store[1]));
        }
        Memory mem = currentProgram.getMemory();
        for (Map.Entry<Long, Address> v : c.vtables.entrySet()) {
            Address vt = v.getValue();
            for (int i = 0; i < MAX_SLOTS; i++) {
                Address slot = vt.add(4L * i);
                if (i > 0 && getReferencesTo(slot).length > 0)
                    break;
                Address fn;
                try {
                    fn = toAddr(mem.getInt(slot) & 0xFFFFFFFFL);
                } catch (Exception e) {
                    break;
                }
                MemoryBlock block = mem.getBlock(fn);
                if (block == null || !block.isExecute())
                    break;
                c.slots.add(fn);
                c.slotPosition.putIfAbsent(fn, String.format("0x%X:%d", v.getKey(), i));
            }
        }

        Set<Address> own = new HashSet<>(c.slots);
        own.add(c.ctor);
        own.add(c.factory);
        for (Address slot : c.slots) {
            for (Reference ref : getReferencesTo(slot)) {
                if (!ref.getReferenceType().isCall())
                    continue;
                Function caller = getFunctionContaining(ref.getFromAddress());
                if (caller != null && own.contains(caller.getEntryPoint()))
                    continue;
                c.bypasses.computeIfAbsent(slot, k -> new HashSet<>())
                    .add(caller != null ? caller.getEntryPoint() : ref.getFromAddress());
            }
        }
        return c;
    }

    // "class_id_getter": MOV EAX, imm32; RET (GetClassID / GetGZCLSID); calling
    //   it directly only reads a constant.
    // "tiny": at most 16 bytes, such as AddRef or a one-field getter. The linker
    //   folds identical functions, so these are shared with unrelated classes
    //   and their outside callers are mostly not calling this class.
    // "method": anything else; outside direct calls are real bypasses.
    private String slotKind(Address fn) {
        try {
            byte[] b = getBytes(fn, 6);
            if ((b[0] & 0xFF) == 0xB8 && (b[5] & 0xFF) == 0xC3)
                return "class_id_getter";
        } catch (Exception e) {
            return "method";
        }
        Function f = getFunctionAt(fn);
        return f != null && f.getBody().getNumAddresses() <= 16 ? "tiny" : "method";
    }

    private boolean storesVtable(Function f) {
        for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            long[] store = vtableStore(insn);
            if (store != null && store[0] == 0)
                return true;
        }
        return false;
    }

    // MOV dword ptr [reg + disp], vtable  ->  {disp, vtable}, else null.
    private long[] vtableStore(Instruction insn) {
        if (!insn.getMnemonicString().equals("MOV") || insn.getNumOperands() != 2)
            return null;
        if (!insn.getDefaultOperandRepresentation(0).startsWith("dword ptr ["))
            return null;
        Scalar value = insn.getScalar(1);
        if (value == null)
            return null;
        long disp = 0;
        int registers = 0;
        for (Object o : insn.getOpObjects(0)) {
            if (o instanceof Register r && r.getBitLength() == 32)
                registers++;
            else if (o instanceof Scalar s)
                disp = s.getSignedValue();
            else
                return null;
        }
        if (registers != 1 || disp < 0 || disp > 0x1000)
            return null;

        Memory mem = currentProgram.getMemory();
        try {
            Address vt = toAddr(value.getUnsignedValue());
            MemoryBlock block = mem.getBlock(vt);
            if (block == null || block.isExecute() || !block.isInitialized())
                return null;
            MemoryBlock target = mem.getBlock(toAddr(mem.getInt(vt) & 0xFFFFFFFFL));
            return target != null && target.isExecute() ? new long[] { disp, value.getUnsignedValue() } : null;
        } catch (Exception e) {
            return null;
        }
    }
}
