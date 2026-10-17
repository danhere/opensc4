// Decompile functions containing the given addresses; with "callers" first arg, also each caller.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;
public class Decompile extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    boolean callers = a.length > 0 && a[0].equals("callers");
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    Set<Function> done = new LinkedHashSet<>();
    for (int i = callers ? 1 : 0; i < a.length; i++) {
      Address ad = toAddr(a[i]);
      Function f = getFunctionContaining(ad);
      if (f == null) { println("NOFUNC " + a[i]); continue; }
      done.add(f);
      if (callers) for (Reference r : getReferencesTo(f.getEntryPoint())) {
        Function c = getFunctionContaining(r.getFromAddress());
        println("REF " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (c == null ? "none" : c.getName()));
        if (c != null) done.add(c);
      }
    }
    for (Function f : done) {
      DecompileResults r = d.decompileFunction(f, 60, monitor);
      println("=== " + f.getName() + " @ " + f.getEntryPoint() + "\n" + (r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage()));
    }
  }
}
