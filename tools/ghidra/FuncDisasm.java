// FuncDisasm.java <addr...>: every instruction of the function at each address (by its body, in address order).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
public class FuncDisasm extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      Function f = getFunctionAt(toAddr(s));
      println("== " + s + " " + (f == null ? "?" : f.getName()));
      if (f == null) continue;
      InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
      while (it.hasNext()) { Instruction i = it.next(); println("  " + i.getAddress() + "  " + i); }
    }
  }
}
