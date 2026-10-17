// Disasm.java <addr> <count> [<addr> <count>...]: instructions from each address.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

public class Disasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        for (int a = 0; a + 1 < args.length; a += 2) {
            Address at = toAddr(Long.decode(args[a]));
            int count = Integer.parseInt(args[a + 1]);
            println("== " + at);
            Instruction insn = getInstructionAt(at);
            for (int i = 0; i < count && insn != null; i++) {
                println("  " + insn.getAddress() + "  " + insn);
                insn = insn.getNext();
            }
        }
    }
}
