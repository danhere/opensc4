// DecompileTo.java <outfile> <addr...>: decompile the function containing each address into outfile.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
public class DecompileTo extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    File out = new File(a[0]);
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
      Set<Function> done = new LinkedHashSet<>();
      for (int i = 1; i < a.length; i++) {
        Function f = getFunctionContaining(toAddr(a[i]));
        if (f == null) { w.println("NOFUNC " + a[i]); continue; }
        if (!done.add(f)) continue;
        DecompileResults r = d.decompileFunction(f, 90, monitor);
        w.println("=== " + f.getName() + " @ " + f.getEntryPoint());
        w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("ERR " + r.getErrorMessage()));
      }
    }
    println("wrote " + out);
  }
}
