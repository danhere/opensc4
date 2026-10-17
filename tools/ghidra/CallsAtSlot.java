import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
// CallsAtSlot <hexoffset...>: indirect calls through [reg + offset], grouped by function
public class CallsAtSlot extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      long off = Long.decode(s);
      int n = 0;
      for (Instruction i : currentProgram.getListing().getInstructions(true)) {
        if (!i.getMnemonicString().equals("CALL")) continue;
        String t = i.toString();
        if (!t.contains("[")) continue;
        Object[] objs = i.getOpObjects(0);
        boolean reg = false, hit = false;
        for (Object o : objs) {
          if (o instanceof ghidra.program.model.lang.Register) reg = true;
          if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == off) hit = true;
        }
        if (!reg || !hit) continue;
        Function f = getFunctionContaining(i.getAddress());
        println(s + " " + i.getAddress() + " " + t + " in " + (f == null ? "-" : f.getName() + "@" + f.getEntryPoint()));
        n++;
      }
      println(s + " total " + n);
    }
  }
}
