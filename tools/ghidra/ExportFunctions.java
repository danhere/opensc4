// Exports every function in the game's code section for the coverage meter
// (src/core/coverage.h) and coverage reports: entry address, body size, first
// byte, and the name Ghidra currently has.
//
// Only functions inside the exe's .text block are exported (no thunks, no
// external or library stubs outside .text). The first byte lets core.dll skip
// entries that someone has patched before it places its one-shot breakpoint.
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <repo>/tools/ghidra -postScript ExportFunctions.java <functions.csv>
//
// The output describes the game's code and must not be committed.
//@category OpenSC4

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class ExportFunctions extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "functions.csv");
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        int exported = 0, named = 0;
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            w.println("address,size,first_byte,name");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.isThunk() || f.isExternal() || !text.contains(f.getEntryPoint()))
                    continue;
                int first = getByte(f.getEntryPoint()) & 0xFF;
                String name = f.getName(true).replace(',', '_');
                w.printf("0x%08X,%d,0x%02X,%s%n", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(),
                    first, name);
                exported++;
                if (!name.startsWith("FUN_"))
                    named++;
            }
        }
        println("Exported " + exported + " functions (" + named + " named) to " + out.getAbsolutePath());
    }
}
