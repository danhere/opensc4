// FindMnemonic.java <mnemonic...>: every instruction with one of these
// mnemonics, with its function.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.util.HashSet;
import java.util.Set;

public class FindMnemonic extends GhidraScript {
    @Override
    public void run() throws Exception {
        Set<String> wanted = new HashSet<>();
        for (String a : getScriptArgs())
            wanted.add(a.toUpperCase());
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            if (!wanted.contains(ins.getMnemonicString()))
                continue;
            Function f = getFunctionContaining(ins.getAddress());
            println(String.format("%s %s [%s]", ins.getAddress(), ins, f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
        }
    }
}
