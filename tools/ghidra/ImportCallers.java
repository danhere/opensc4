// ImportCallers.java <name...>: functions that call each named import or
// library function (through thunks and IAT pointers too), with their size.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ImportCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        SymbolTable st = currentProgram.getSymbolTable();
        for (String name : getScriptArgs()) {
            Set<String> seen = new TreeSet<>();
            Deque<ghidra.program.model.address.Address> work = new ArrayDeque<>();
            for (Symbol s : st.getSymbols(name)) work.add(s.getAddress());
            for (Symbol s : st.getExternalSymbols(name)) work.add(s.getAddress());
            Set<ghidra.program.model.address.Address> done = new HashSet<>();
            while (!work.isEmpty()) {
                ghidra.program.model.address.Address a = work.pop();
                if (!done.add(a)) continue;
                for (Reference r : rm.getReferencesTo(a)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f == null) {
                        // IAT slot or pointer: follow references to it.
                        work.add(r.getFromAddress());
                        continue;
                    }
                    if (f.isThunk() || f.getBody().getNumAddresses() <= 8) {
                        work.add(f.getEntryPoint());
                        continue;
                    }
                    seen.add(String.format("%s %5d %s", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName(true)));
                }
            }
            println("== " + name + " (" + seen.size() + ")");
            for (String s : seen) println("  " + s);
        }
    }
}
