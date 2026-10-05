// Names what OpenSC4's analysis has identified so far:
//   - the GZCOM AddCls functions;
//   - for every class registered with AddCls whose class ID is in the exe's
//     name table: its factory (Create), constructor and vtables;
//   - the primary vtable's slot functions, when gzcom-dll declares the
//     matching interface (cSC4Foo -> cISC4Foo) and the vtable has exactly as
//     many slots as that interface, or at most two more where one of the
//     extras is an MSVC scalar deleting destructor (the class's own virtuals
//     follow the interface's);
//   - the game's operator new and delete (0x005E55E0, 0x005E5620).
// Only functions with default names are renamed, so Function ID's library
// names are kept. A function that would get two different names (identical
// code folded across classes by the linker) is left alone.
//
// Inputs (from ClassRegistrationCensus.java, tools/re/extract_name_table.py and
// tools/re/gzcom_vtable_slots.py):
//   <class-registrations.csv> <name-table.csv> <interface-slots.csv>
//
// Usage (headless, saves the program):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis
//     -scriptPath <repo>/tools/ghidra -postScript ApplyKnownNames.java <csv files>
//@category OpenSC4

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
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
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyKnownNames extends GhidraScript {

    private static final int MAX_SLOTS = 400;

    // Function entry -> proposed qualified names (class, method).
    private final Map<Address, Set<String>> proposals = new HashMap<>();
    private int labels, vtablesMatched, vtablesUnmatched;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            printerr("usage: ApplyKnownNames.java <class-registrations.csv> <name-table.csv> <interface-slots.csv>");
            return;
        }

        Map<Long, Address> factories = new TreeMap<>();
        for (String[] f : readCsv(args[0]))
            if (f[1].equals("0090e133") || f[1].equals("0090e160"))
                factories.put(Long.decode(f[3]), toAddr(f[4]));
        Map<Long, String> names = new HashMap<>();
        for (String[] f : readCsv(args[1]))
            if (f[1].startsWith("c"))
                names.putIfAbsent(Long.decode(f[0]), f[1]); // first name wins for shared IDs
        Map<String, List<String>> interfaces = new HashMap<>();
        for (String[] f : readCsv(args[2]))
            interfaces.computeIfAbsent(f[0], k -> new ArrayList<>()).add(f[2]);

        propose(toAddr(0x0090E133), "cRZCOMDllDirector", "AddCls");
        propose(toAddr(0x0090E160), "cRZCOMDllDirector", "AddCls_FactoryType2");
        // Precedes 752 constructor calls; scalar deleting destructors free with 0x005E5620.
        propose(toAddr(0x005E55E0), "SC4", "operator_new");
        propose(toAddr(0x005E5620), "SC4", "operator_delete");

        int classes = 0;
        for (Map.Entry<Long, Address> e : factories.entrySet()) {
            String cls = names.get(e.getKey());
            if (cls == null)
                continue;
            classes++;
            nameClass(cls, e.getValue(), interfaces.get("cI" + cls.substring(1)));
        }

        int renamed = 0, conflicts = 0, kept = 0;
        SymbolTable symbols = currentProgram.getSymbolTable();
        int created = 0;
        for (Map.Entry<Address, Set<String>> p : proposals.entrySet()) {
            Function f = getFunctionAt(p.getKey());
            if (f == null) {
                // Code reached only through vtables is often never made a
                // function by auto-analysis.
                if (getInstructionAt(p.getKey()) == null)
                    disassemble(p.getKey());
                f = createFunction(p.getKey(), null);
                if (f == null)
                    continue;
                created++;
            }
            if (p.getValue().size() > 1) {
                conflicts++;
                continue;
            }
            if (f.getSymbol().getSource() != SourceType.DEFAULT) {
                kept++;
                continue;
            }
            String[] qualified = p.getValue().iterator().next().split("::", 2);
            Namespace ns = classNamespace(symbols, qualified[0]);
            f.setParentNamespace(ns);
            try {
                f.setName(qualified[1], SourceType.IMPORTED);
            } catch (ghidra.util.exception.DuplicateNameException dup) {
                f.setName(qualified[1] + "_" + f.getEntryPoint(), SourceType.IMPORTED);
            }
            renamed++;
        }
        println("Functions created for vtable targets: " + created);
        println(String.format("Named classes: %d; functions renamed: %d; kept existing names: %d; "
            + "skipped (conflicting names): %d; vtable labels: %d; primary vtables matched to an interface: %d "
            + "(length mismatch or no interface: %d)",
            classes, renamed, kept, conflicts, labels, vtablesMatched, vtablesUnmatched));
    }

    private void nameClass(String cls, Address factoryAddr, List<String> iface) throws Exception {
        Function factory = getFunctionAt(factoryAddr);
        if (factory == null) {
            if (getInstructionAt(factoryAddr) == null)
                disassemble(factoryAddr);
            factory = createFunction(factoryAddr, null);
        }
        if (factory == null)
            return;
        propose(factory.getEntryPoint(), cls, "Create");

        Function ctor = lastVtableStoreAtZero(factory) != null ? factory : null;
        if (ctor == null) {
            for (Instruction insn : currentProgram.getListing().getInstructions(factory.getBody(), true)) {
                if (!insn.getFlowType().isCall() || insn.getFlows().length != 1)
                    continue;
                Function callee = getFunctionAt(insn.getFlows()[0]);
                if (callee != null && lastVtableStoreAtZero(callee) != null)
                    ctor = callee;
            }
        }
        if (ctor == null)
            return;
        if (ctor != factory)
            propose(ctor.getEntryPoint(), cls, cls);

        // Only stores through the register holding `this` count; constructors
        // also write vtables of sub-objects through other registers. `this` is
        // taken to be the register with the most vtable stores.
        Map<String, Integer> storesByRegister = new HashMap<>();
        for (Instruction insn : currentProgram.getListing().getInstructions(ctor.getBody(), true)) {
            long[] store = vtableStore(insn);
            if (store != null)
                storesByRegister.merge(baseRegister(insn), 1, Integer::sum);
        }
        String thisRegister = storesByRegister.entrySet().stream()
            .max(Map.Entry.comparingByValue()).map(Map.Entry::getKey).orElse("");
        Map<Long, Address> vtables = new TreeMap<>();
        for (Instruction insn : currentProgram.getListing().getInstructions(ctor.getBody(), true)) {
            long[] store = vtableStore(insn);
            if (store != null && baseRegister(insn).equals(thisRegister))
                vtables.put(store[0], toAddr(store[1]));
        }
        Namespace ns = classNamespace(currentProgram.getSymbolTable(), cls);
        for (Map.Entry<Long, Address> v : vtables.entrySet()) {
            String label = v.getKey() == 0 ? "vftable" : String.format("vftable_0x%X", v.getKey());
            if (getSymbolAt(v.getValue()) == null || getSymbolAt(v.getValue()).getSource() == SourceType.DEFAULT) {
                createLabel(v.getValue(), label, ns, true, SourceType.IMPORTED);
                labels++;
            }
        }

        Address primary = vtables.get(0L);
        List<Address> slots = primary == null ? List.of() : slots(primary);
        int extra = iface == null ? -1 : slots.size() - iface.size();
        int destructorSlot = -1;
        for (int i = Math.max(0, slots.size() - 2); extra > 0 && i < slots.size(); i++)
            if (i >= iface.size() && isScalarDeletingDestructor(slots.get(i)))
                destructorSlot = i;
        if (extra == 0 || (extra > 0 && extra <= 2 && destructorSlot >= 0)) {
            vtablesMatched++;
            for (int i = 0; i < iface.size(); i++)
                propose(slots.get(i), cls, iface.get(i));
            if (destructorSlot >= 0) {
                Address sdd = slots.get(destructorSlot);
                propose(sdd, cls, "scalar_deleting_destructor");
                Address dtor = calledFrom(sdd);
                if (dtor != null)
                    propose(dtor, cls, "~" + cls);
            }
        } else {
            vtablesUnmatched++;
            if (iface != null)
                println(String.format("  %s: primary vtable %s has %d slots, %s declares %d",
                    cls, primary, slots.size(), "cI" + cls.substring(1), iface.size()));
        }
    }

    // MSVC: push esi; mov esi, ecx; call ~T; test byte ptr [esp+8], 1; ...
    // (calls the destructor, then operator delete if the flag's bit 0 is set).
    private boolean isScalarDeletingDestructor(Address fn) {
        try {
            byte[] b = getBytes(fn, 13);
            return (b[0] & 0xFF) == 0x56 && (b[1] & 0xFF) == 0x8B && (b[2] & 0xFF) == 0xF1
                && (b[3] & 0xFF) == 0xE8 && (b[8] & 0xFF) == 0xF6 && (b[9] & 0xFF) == 0x44
                && (b[10] & 0xFF) == 0x24 && (b[11] & 0xFF) == 0x08 && (b[12] & 0xFF) == 0x01;
        } catch (Exception e) {
            return false;
        }
    }

    // Target of the CALL at fn+3 in a scalar deleting destructor.
    private Address calledFrom(Address sdd) {
        try {
            return sdd.add(8).add(getInt(sdd.add(4)));
        } catch (Exception e) {
            return null;
        }
    }

    private static String baseRegister(Instruction insn) {
        for (Object o : insn.getOpObjects(0))
            if (o instanceof Register r)
                return r.getName();
        return "";
    }

    private void propose(Address fn, String cls, String method) {
        proposals.computeIfAbsent(fn, k -> new HashSet<>()).add(cls + "::" + method);
    }

    private Namespace classNamespace(SymbolTable symbols, String name) throws Exception {
        Namespace global = currentProgram.getGlobalNamespace();
        Namespace ns = symbols.getNamespace(name, global);
        return ns != null ? ns : symbols.createClass(global, name, SourceType.IMPORTED);
    }

    private List<Address> slots(Address vtable) {
        List<Address> result = new ArrayList<>();
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < MAX_SLOTS; i++) {
            Address slot = vtable.add(4L * i);
            if (i > 0 && getReferencesTo(slot).length > 0)
                break;
            try {
                Address fn = toAddr(mem.getInt(slot) & 0xFFFFFFFFL);
                MemoryBlock block = mem.getBlock(fn);
                if (block == null || !block.isExecute())
                    break;
                result.add(fn);
            } catch (Exception e) {
                break;
            }
        }
        return result;
    }

    private Address lastVtableStoreAtZero(Function f) {
        Address last = null;
        for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            long[] store = vtableStore(insn);
            if (store != null && store[0] == 0)
                last = toAddr(store[1]);
        }
        return last;
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

    private static List<String[]> readCsv(String path) throws Exception {
        List<String[]> rows = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;)
                if (!line.isBlank())
                    rows.add(line.split(","));
        }
        return rows;
    }
}
