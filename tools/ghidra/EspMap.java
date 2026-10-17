// EspMap.java <function addr>: the function's disassembly with every
// [ESP + n] operand rewritten as a frame offset "F+x", counted from ESP at
// entry (F+0 is the return address, F+4 the first stack argument; negative
// offsets are locals and saved registers). The depth is tracked here:
// PUSH/POP, ADD/SUB ESP, a direct call pops what its target's RET says, an
// indirect call (a virtual, __thiscall) pops what was pushed since the last
// call or stack adjustment (saves of EBX/EBP/ESI/EDI before the first call
// excepted), and a jump target takes the depth of the jump.
// The second column is that depth ("?" where a target was never reached).
// Optional arguments "0x174:0 0xc:4 ..." (":", since the batch file eats "="): what an indirect call through that
// vtable offset pops, where the heuristic would be wrong (a no-argument call
// between the pushes of a later cdecl call; register saves pushed late).
// A RET reached at a depth other than 0 is flagged: a heuristic went wrong.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.FlowType;

import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class EspMap extends GhidraScript {
    private int retSize(Address target) {
        Function f = getFunctionAt(target);
        if (f == null)
            return 0;
        for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            if (!ins.getMnemonicString().equals("RET"))
                continue;
            Scalar s = ins.getNumOperands() > 0 ? ins.getScalar(0) : null;
            return s == null ? 0 : (int) s.getValue();
        }
        return 0;
    }

    @Override
    public void run() throws Exception {
        Function f = getFunctionAt(toAddr(Long.decode(getScriptArgs()[0])));
        if (f == null) {
            println("no function");
            return;
        }
        Pattern esp = Pattern.compile("\\[ESP(?: \\+ (0x[0-9a-fA-F]+|\\d+))?\\]");
        Pattern indirect = Pattern.compile("CALL dword ptr \\[E[A-Z]X(?: \\+ (0x[0-9a-fA-F]+|\\d+))?\\]");
        Map<Integer, Integer> hints = new HashMap<>();
        String[] args = getScriptArgs();
        for (int i = 1; i < args.length; i++) {
            String[] kv = args[i].split(":");
            hints.put(Integer.decode(kv[0]), Integer.decode(kv[1]));
        }
        Map<Address, Integer> atTarget = new HashMap<>();
        Integer depth = 0;
        int pushedSinceCall = 0;
        boolean seenCall = false;
        println("== " + f.getEntryPoint() + " " + f.getName());
        for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            if (depth == null) {
                depth = atTarget.get(ins.getAddress());
                pushedSinceCall = 0;
            }
            String text = ins.toString();
            StringBuffer out = new StringBuffer();
            Matcher m = esp.matcher(text);
            while (m.find()) {
                if (depth == null) {
                    m.appendReplacement(out, Matcher.quoteReplacement(m.group(0)));
                    continue;
                }
                int disp = m.group(1) == null ? 0 : Integer.decode(m.group(1));
                int frame = depth + disp;
                m.appendReplacement(out, Matcher.quoteReplacement(
                    "[F" + (frame >= 0 ? "+" : "-") + "0x" + Integer.toHexString(Math.abs(frame)) + "]"));
            }
            m.appendTail(out);
            println(String.format("  %s  %5s  %s", ins.getAddress(), depth == null ? "?" : depth.toString(), out));
            if (depth == null)
                continue;
            String mn = ins.getMnemonicString();
            if (mn.equals("PUSH")) {
                depth -= 4;
                // Saves of callee-saved registers before the first call are
                // not arguments.
                boolean save = !seenCall && (text.equals("PUSH EBX") || text.equals("PUSH EBP") ||
                                             text.equals("PUSH ESI") || text.equals("PUSH EDI"));
                if (!save)
                    pushedSinceCall += 4;
            } else if (mn.equals("POP")) {
                depth += 4;
            } else if ((mn.equals("SUB") || mn.equals("ADD")) && text.startsWith(mn + " ESP,")) {
                Scalar s = ins.getScalar(1);
                if (s != null)
                    depth += (mn.equals("SUB") ? -1 : 1) * (int) s.getValue();
                pushedSinceCall = 0;
            } else if (mn.equals("CALL")) {
                Address[] flows = ins.getFlows();
                int purge = flows.length > 0 ? retSize(flows[0]) : pushedSinceCall;
                Matcher call = indirect.matcher(text);
                if (flows.length == 0 && call.matches()) {
                    int slot = call.group(1) == null ? 0 : Integer.decode(call.group(1));
                    if (hints.containsKey(slot))
                        purge = hints.get(slot);
                }
                depth += purge;
                pushedSinceCall = 0;
                seenCall = true;
            }
            FlowType flow = ins.getFlowType();
            if (flow.isJump()) {
                for (Address target : ins.getFlows())
                    atTarget.putIfAbsent(target, depth);
            }
            if (mn.equals("RET")) {
                if (depth != 0)
                    println("  !! RET at depth " + depth + ": a call's argument count was guessed wrong above");
            }
            if (mn.equals("RET") || (flow.isJump() && !flow.isConditional()))
                depth = null;
        }
    }
}
