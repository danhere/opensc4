// Finds GZCOM-style class registrations: calls whose last two pushed
// arguments are (class ID, pointer to a function), the shape of
// cRZCOMDllDirector::AddCls(clsid, factory) in gzcom-dll. The class ID is
// either an immediate or the result of a cFoo::GetClassID() getter
// (MOV EAX, imm32; RET). Tallies the callees and writes every candidate call
// site to a CSV for further analysis.
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <repo>/tools/ghidra -postScript ClassRegistrationCensus.java <out.csv>
//
// The output describes the game's code and must not be committed.
//@category OpenSC4

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;

public class ClassRegistrationCensus extends GhidraScript {

    private static final int MAX_LOOKBACK = 8;

    private record Site(Address call, Address callee, long clsid, Address factory) {}

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "class-registrations.csv");

        List<Site> sites = new ArrayList<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction call = it.next();
            if (!call.getFlowType().isCall())
                continue;
            Address[] flows = call.getFlows();
            if (flows.length != 1)
                continue;

            // Walk back to the two most recent PUSH imm32 instructions:
            // the last one pushed is the first argument (class ID).
            List<Long> pushes = new ArrayList<>();
            Instruction prev = call.getPrevious();
            for (int i = 0; i < MAX_LOOKBACK && prev != null && pushes.size() < 2; i++) {
                if (prev.getFlowType().isCall() || prev.getFlowType().isJump() || prev.getFlowType().isTerminal())
                    break;
                if (prev.getMnemonicString().equals("PUSH")) {
                    Scalar s = prev.getScalar(0);
                    Long value = s != null ? Long.valueOf(s.getUnsignedValue()) : classIdFromGetter(prev);
                    if (value == null)
                        break; // other register or memory push: not the pattern
                    pushes.add(value);
                    if (s == null)
                        prev = prev.getPrevious(); // skip the getter call itself
                }
                prev = prev.getPrevious();
            }
            if (pushes.size() < 2)
                continue;

            long clsid = pushes.get(0);
            Address factory = toAddr(pushes.get(1));
            if (isInProgram(clsid) || !isExecutable(factory))
                continue;
            sites.add(new Site(call.getAddress(), flows[0], clsid, factory));
        }

        Map<Address, Integer> byCallee = new HashMap<>();
        for (Site s : sites)
            byCallee.merge(s.callee(), 1, Integer::sum);

        println("Candidate registration calls: " + sites.size());
        byCallee.entrySet().stream()
            .sorted((a, b) -> b.getValue() - a.getValue())
            .limit(15)
            .forEach(e -> println(String.format("  callee %s  calls %d", e.getKey(), e.getValue())));

        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            w.println("call_site,callee,callee_calls,clsid,factory,factory_is_function,caller");
            for (Site s : sites) {
                Function caller = getFunctionContaining(s.call());
                w.printf("%s,%s,%d,0x%08X,%s,%s,%s%n",
                    s.call(), s.callee(), byCallee.get(s.callee()), s.clsid(), s.factory(),
                    getFunctionAt(s.factory()) != null,
                    caller == null ? "" : caller.getEntryPoint());
            }
        }
        println("Wrote " + out.getAbsolutePath());
    }

    // Many classes are registered as AddCls(cFoo::GetClassID(), factory), which
    // compiles to CALL getter; PUSH EAX where the getter is MOV EAX, imm32; RET.
    // Returns that immediate for a PUSH EAX right after such a call, else null.
    private Long classIdFromGetter(Instruction push) {
        if (!push.toString().equals("PUSH EAX"))
            return null;
        Instruction call = push.getPrevious();
        if (call == null || !call.getFlowType().isCall() || call.getFlows().length != 1)
            return null;
        try {
            Address getter = call.getFlows()[0];
            byte[] bytes = getBytes(getter, 6);
            if ((bytes[0] & 0xFF) != 0xB8 || (bytes[5] & 0xFF) != 0xC3)
                return null;
            return Integer.toUnsignedLong(getInt(getter.add(1)));
        } catch (Exception e) {
            return null;
        }
    }

    private boolean isInProgram(long value) {
        if (value < 0x10000)
            return true; // small constants are sizes and flags, not class IDs
        try {
            return currentProgram.getMemory().contains(toAddr(value));
        } catch (Exception e) {
            return false;
        }
    }

    private boolean isExecutable(Address a) {
        MemoryBlock block = currentProgram.getMemory().getBlock(a);
        return block != null && block.isExecute();
    }
}
