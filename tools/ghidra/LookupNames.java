// Prints the function containing each given address, with its name, entry
// point and signature. A quick way to ask the Ghidra project "what is this?".
//
// Usage (headless, read-only):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis -readOnly
//     -scriptPath <repo>/tools/ghidra -postScript LookupNames.java 0x5e55e0 0x9133da ...
//@category OpenSC4

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class LookupNames extends GhidraScript {

    @Override
    protected void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address a = toAddr(Long.decode(arg));
            Function f = getFunctionContaining(a);
            if (f == null) {
                println(String.format("%s: no function", a));
                continue;
            }
            println(String.format("%s: %s (entry %s, %s) %s", a, f.getName(true), f.getEntryPoint(),
                f.getSymbol().getSource(), f.getSignature().getPrototypeString()));
        }
    }
}
