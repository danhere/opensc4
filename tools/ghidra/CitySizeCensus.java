// Lists code in SimCity 4.exe that may assume the three fixed city sizes
// (64, 128, 256 cells a side; 257 heightmap points; 1, 2, 4 km). Input for
// docs/city-size.md "Size census".
//
// Categories (one CSV row per hit):
//   size_read   functions that call cISC4City slot 93 or 94 (vtable +0x174,
//               +0x178: cells in x and z). Slot 93/94 of other interfaces
//               share the offsets, so this is an over-count.
//   near_read   immediates 64/128/256/257/1024/4096, masks 0x3F/0x7F/0xFF/
//               0x3FF/0xFFF, shifts by 6/7/8 inside a size_read function
//               (not stack adjustments or byte-sized constants).
//   area        immediates equal to k * n * n (n = 64, 65, 128, 129, 256,
//               257; k = 1, 2, 4, 8, 12, 16): grid areas and allocation sizes.
//               0x1000 and 0x10000 are left out (page sizes, everywhere);
//               other powers of two only inside size_read functions.
//   imm257      the immediate 257 (0x101) anywhere: heightmap points a side.
//   float       references to float/double 1024.0, 2048.0, 4096.0 constants
//               (and immediates with their float bit patterns); "uses_16"
//               says whether the same function also reads 16.0 (metres a cell).
//   table       data holding 64,128,256 or 1,2,4 (dwords) or 1024,2048,4096
//               (floats) in a row: size lookup tables, with their readers.
//               Dword runs that keep doubling (bit-mask tables) are skipped.
//   config_bmp  functions referencing the "config.bmp" string.
//
// Every row names the function, its class (from the Ghidra name, or the
// class of the named functions around it: "~cSC4Foo"), and, when a coverage
// file from diagnostics.coverage is given, whether the function ran.
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <scripts> -postScript CitySizeCensus.java <out.csv> [opensc4-coverage.csv]
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
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class CitySizeCensus extends GhidraScript {

    private static final long[] SIDES = {64, 65, 128, 129, 256, 257};
    private static final long[] ELEMENT = {1, 2, 4, 8, 12, 16};
    private static final Set<Long> NEAR_VALUES = Set.of(64L, 128L, 256L, 257L, 1024L, 4096L,
        0x3FL, 0x7FL, 0xFFL, 0x3FFL, 0xFFFL);
    private static final Set<Long> SHIFTS = Set.of(6L, 7L, 8L);

    private final List<String[]> rows = new ArrayList<>();
    private final Set<Address> ran = new HashSet<>();
    private boolean haveCoverage;
    private final TreeMap<Long, String> namedClassAt = new TreeMap<>();

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: CitySizeCensus.java <out.csv> [opensc4-coverage.csv]");
            return;
        }
        if (args.length > 1) {
            haveCoverage = true;
            try (BufferedReader r = new BufferedReader(new FileReader(args[1]))) {
                r.readLine();
                for (String line; (line = r.readLine()) != null;)
                    ran.add(toAddr(line.split(",")[0]));
            }
        }
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            String c = classOf(f);
            if (c != null)
                namedClassAt.put(f.getEntryPoint().getOffset(), c);
        }

        Set<Long> area = new HashSet<>();
        for (long n : SIDES)
            for (long k : ELEMENT)
                area.add(k * n * n);
        area.remove(0x1000L);
        area.remove(0x10000L);

        // Pass 1: functions that read the city size.
        Map<Function, String> sizeReaders = new HashMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            int slot = sizeSlotCall(ins);
            if (slot == 0)
                continue;
            Function f = getFunctionContaining(ins.getAddress());
            if (f == null)
                continue;
            String had = sizeReaders.get(f);
            String now = slot == 0x174 ? "x" : "z";
            sizeReaders.put(f, had == null || had.equals(now) ? now : "xz");
            add("size_read", ins.getAddress(), f, ins.toString(), "slot " + (slot == 0x174 ? 93 : 94), "");
        }

        // Pass 2: immediates.
        it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            Function f = getFunctionContaining(ins.getAddress());
            String mnem = ins.getMnemonicString().toUpperCase();
            boolean shift = mnem.startsWith("SHL") || mnem.startsWith("SHR") || mnem.startsWith("SAR");
            for (int op = 0; op < ins.getNumOperands(); op++) {
                int type = ins.getOperandType(op);
                if (!OperandType.isScalar(type) || OperandType.isDynamic(type))
                    continue;
                for (Object o : ins.getOpObjects(op)) {
                    if (!(o instanceof Scalar))
                        continue;
                    long v = ((Scalar) o).getUnsignedValue() & 0xFFFFFFFFL;
                    String hex = String.format("0x%X", v);
                    if (v == 257)
                        add("imm257", ins.getAddress(), f, ins.toString(), hex, "");
                    else if (area.contains(v) && (Long.bitCount(v) > 2 || (f != null && sizeReaders.containsKey(f))))
                        add("area", ins.getAddress(), f, ins.toString(), hex + " " + areaName(v), "");
                    if (f != null && sizeReaders.containsKey(f) && !stackOrByte(ins, (Scalar) o)
                        && (NEAR_VALUES.contains(v) || (shift && SHIFTS.contains(v))))
                        add("near_read", ins.getAddress(), f, ins.toString(), hex, "reads " + sizeReaders.get(f));
                    if (v == 0x44800000L || v == 0x45000000L || v == 0x45800000L)
                        add("float", ins.getAddress(), f, ins.toString(), floatName(v), "immediate");
                }
            }
        }

        // Pass 3: float constants and size tables in data.
        Set<Function> uses16 = new HashSet<>();
        for (Address a : findFloats(16.0))
            for (Reference r : getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null)
                    uses16.add(f);
            }
        for (double d : new double[] {1024.0, 2048.0, 4096.0})
            for (Address a : findFloats(d))
                for (Reference r : getReferencesTo(a)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    Instruction ins = getInstructionAt(r.getFromAddress());
                    add("float", r.getFromAddress(), f, ins == null ? "data ref" : ins.toString(),
                        (long) d + ".0 @" + a, f != null && uses16.contains(f) ? "uses_16" : "");
                }
        findTables();

        // Pass 4: config.bmp.
        Memory mem = currentProgram.getMemory();
        byte[] pat = "config.bmp\0".getBytes("US-ASCII");
        for (Address a = mem.getMinAddress(); (a = mem.findBytes(a, pat, null, true, monitor)) != null; a = a.add(1)) {
            List<Address> from = new ArrayList<>();
            for (Reference r : getReferencesTo(a))
                from.add(r.getFromAddress());
            // The string is also reached through a pointer variable.
            for (Reference r : getReferencesTo(a))
                for (Reference r2 : getReferencesTo(r.getFromAddress()))
                    from.add(r2.getFromAddress());
            for (Address x : from) {
                Function f = getFunctionContaining(x);
                if (f != null)
                    add("config_bmp", x, f, String.valueOf(getInstructionAt(x)), "\"config.bmp\" @" + a, "");
            }
        }

        try (PrintWriter out = new PrintWriter(new File(args[0]), "UTF-8")) {
            out.println("category,address,function,function_name,class,ran,instruction,value,note");
            for (String[] r : rows)
                out.println(String.join(",", r));
        }

        // Summary: hits and functions per category, and per class.
        Map<String, Set<String>> funcs = new TreeMap<>();
        Map<String, Integer> hits = new TreeMap<>();
        Map<String, Set<String>> ranFuncs = new TreeMap<>();
        Map<String, Set<String>> classFuncs = new TreeMap<>();
        for (String[] r : rows) {
            funcs.computeIfAbsent(r[0], k -> new HashSet<>()).add(r[2]);
            hits.merge(r[0], 1, Integer::sum);
            if (r[5].equals("yes"))
                ranFuncs.computeIfAbsent(r[0], k -> new HashSet<>()).add(r[2]);
            if (!r[0].equals("size_read"))
                classFuncs.computeIfAbsent(r[4].isEmpty() ? "(unknown)" : r[4], k -> new HashSet<>()).add(r[2]);
        }
        println(String.format("%-12s %7s %9s %7s", "category", "hits", "functions", "ran"));
        for (String c : funcs.keySet())
            println(String.format("%-12s %7d %9d %7s", c, hits.get(c), funcs.get(c).size(),
                haveCoverage ? String.valueOf(ranFuncs.getOrDefault(c, Set.of()).size()) : "-"));
        println("Classes with the most functions hit (excluding size_read):");
        classFuncs.entrySet().stream()
            .sorted((a, b) -> b.getValue().size() - a.getValue().size())
            .limit(40)
            .forEach(e -> println(String.format("  %4d  %s", e.getValue().size(), e.getKey())));
        println("Wrote " + rows.size() + " rows to " + new File(args[0]).getAbsolutePath());
    }

    // Returns 0x174 or 0x178 for CALL [reg+0x174/0x178], or for
    // MOV reg2,[reg+0x174/0x178] followed by CALL reg2 within 4 instructions.
    private int sizeSlotCall(Instruction ins) {
        String m = ins.getMnemonicString().toUpperCase();
        if (!m.equals("CALL") && !m.equals("MOV"))
            return 0;
        int op = m.equals("CALL") ? 0 : 1;
        if (ins.getNumOperands() <= op || !OperandType.isDynamic(ins.getOperandType(op)))
            return 0;
        int disp = 0;
        boolean reg = false;
        for (Object o : ins.getOpObjects(op)) {
            if (o instanceof Register)
                reg = true;
            else if (o instanceof Scalar) {
                long v = ((Scalar) o).getUnsignedValue();
                if (v == 0x174 || v == 0x178)
                    disp = (int) v;
            }
        }
        if (!reg || disp == 0)
            return 0;
        if (m.equals("CALL"))
            return disp;
        Object[] dst = ins.getOpObjects(0);
        if (dst.length != 1 || !(dst[0] instanceof Register))
            return 0;
        Instruction n = ins;
        for (int i = 0; i < 4 && (n = n.getNext()) != null; i++) {
            if (n.getMnemonicString().equalsIgnoreCase("CALL")) {
                Object[] t = n.getOpObjects(0);
                return t.length == 1 && dst[0].equals(t[0]) ? disp : 0;
            }
        }
        return 0;
    }

    // Stack frame adjustments and byte-sized 0xFF/0x40/0x80 constants are
    // not sizes.
    private static boolean stackOrByte(Instruction ins, Scalar s) {
        String t = ins.toString();
        if (t.startsWith("SUB ESP,") || t.startsWith("ADD ESP,"))
            return true;
        return s.bitLength() <= 8 && !t.startsWith("SH") && !t.startsWith("SAR");
    }

    private List<Address> findFloats(double d) throws Exception {
        List<Address> found = new ArrayList<>();
        Memory mem = currentProgram.getMemory();
        int fbits = Float.floatToIntBits((float) d);
        long dbits = Double.doubleToLongBits(d);
        byte[] f4 = {(byte) fbits, (byte) (fbits >> 8), (byte) (fbits >> 16), (byte) (fbits >> 24)};
        byte[] d8 = new byte[8];
        for (int i = 0; i < 8; i++)
            d8[i] = (byte) (dbits >> (8 * i));
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute())
                continue;
            for (byte[] p : new byte[][] {f4, d8}) {
                Address a = b.getStart();
                while ((a = mem.findBytes(a, b.getEnd(), p, null, true, monitor)) != null) {
                    if (a.getOffset() % 4 == 0)
                        found.add(a);
                    a = a.add(1);
                }
            }
        }
        return found;
    }

    private void findTables() throws Exception {
        Memory mem = currentProgram.getMemory();
        long[][] dwordTables = {{64, 128, 256}, {1, 2, 4}};
        List<byte[]> pats = new ArrayList<>();
        List<String> names = new ArrayList<>();
        for (long[] t : dwordTables) {
            byte[] p = new byte[12];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    p[4 * i + j] = (byte) (t[i] >> (8 * j));
            pats.add(p);
            names.add("dwords " + t[0] + "," + t[1] + "," + t[2]);
        }
        byte[] fp = new byte[12];
        float[] ft = {1024f, 2048f, 4096f};
        for (int i = 0; i < 3; i++) {
            int bits = Float.floatToIntBits(ft[i]);
            for (int j = 0; j < 4; j++)
                fp[4 * i + j] = (byte) (bits >> (8 * j));
        }
        pats.add(fp);
        names.add("floats 1024,2048,4096");
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute())
                continue;
            for (int k = 0; k < pats.size(); k++) {
                Address a = b.getStart();
                while ((a = mem.findBytes(a, b.getEnd(), pats.get(k), null, true, monitor)) != null) {
                    boolean bitTable = k < 2 && mem.getInt(a.add(12)) == 2 * mem.getInt(a.add(8));
                    if (a.getOffset() % 4 == 0 && !bitTable) {
                        // A reader may index from the table start or from one
                        // entry before it (index 1..3).
                        boolean any = false;
                        for (long back = -4; back <= 8; back += 4)
                            for (Reference r : getReferencesTo(a.add(back))) {
                                Function f = getFunctionContaining(r.getFromAddress());
                                Instruction ins = getInstructionAt(r.getFromAddress());
                                add("table", r.getFromAddress(), f, ins == null ? "data ref" : ins.toString(),
                                    names.get(k) + " @" + a, back == 0 ? "" : "ref at " + a.add(back));
                                any = true;
                            }
                        if (!any)
                            add("table", a, null, "no references", names.get(k) + " @" + a, "");
                    }
                    a = a.add(1);
                }
            }
        }
    }

    private void add(String cat, Address at, Function f, String insn, String value, String note) {
        String name = f == null ? "" : f.getName(true);
        String cls = f == null ? "" : classOf(f);
        if (f != null && cls == null)
            cls = neighbourClass(f.getEntryPoint().getOffset());
        String ranText = !haveCoverage || f == null ? "-" : ran.contains(f.getEntryPoint()) ? "yes" : "no";
        rows.add(new String[] {cat, at.toString(), f == null ? "" : f.getEntryPoint().toString(),
            csv(name), csv(cls == null ? "" : cls), ranText, csv(insn), csv(value), csv(note)});
    }

    private static String classOf(Function f) {
        String n = f.getName(true);
        int i = n.lastIndexOf("::");
        if (i <= 0)
            return null;
        return n.substring(0, i);
    }

    // MSVC keeps an object file's functions together: an unnamed function
    // between two named functions of the same class (less than 16 KB apart)
    // most likely belongs to it; "?" = only close after a named function.
    private String neighbourClass(long entry) {
        Map.Entry<Long, String> lo = namedClassAt.floorEntry(entry);
        Map.Entry<Long, String> hi = namedClassAt.ceilingEntry(entry);
        if (lo != null && hi != null && lo.getValue().equals(hi.getValue()) && hi.getKey() - lo.getKey() < 0x4000)
            return "~" + lo.getValue();
        if (lo != null && entry - lo.getKey() < 0x400)
            return "~" + lo.getValue() + "?";
        return null;
    }

    private static String areaName(long v) {
        for (long k : ELEMENT)
            for (long n : SIDES)
                if (k * n * n == v)
                    return k == 1 ? n + "x" + n : n + "x" + n + "x" + k;
        return "";
    }

    private static String floatName(long bits) {
        return Float.intBitsToFloat((int) bits) + "f";
    }

    private static String csv(String s) {
        return s.contains(",") || s.contains("\"") ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
    }
}
