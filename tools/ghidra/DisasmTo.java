// DisasmTo.java <outfile> <addr...>: every instruction of the function at each address into outfile.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class DisasmTo extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    File out = new File(a[0]);
    try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
      for (int i = 1; i < a.length; i++) {
        Function f = getFunctionContaining(toAddr(a[i]));
        w.println("== " + a[i] + " " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
        if (f == null) continue;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) { Instruction ins = it.next(); w.println("  " + ins.getAddress() + "  " + ins); }
      }
    }
    println("wrote " + out);
  }
}
