import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
// DispIn <start> <end> <disp>: instructions in [start,end) with a memory operand using displacement disp
public class DispIn extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    long start = Long.decode(a[0]), end = Long.decode(a[1]), disp = Long.decode(a[2]);
    for (Instruction i : currentProgram.getListing().getInstructions(toAddr(start), true)) {
      if (i.getAddress().getOffset() >= end) break;
      for (int op = 0; op < i.getNumOperands(); ++op) {
        String t = i.getDefaultOperandRepresentation(op);
        if (!t.contains("[")) continue;
        for (Object o : i.getOpObjects(op))
          if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == disp) {
            Function f = getFunctionContaining(i.getAddress());
            println(i.getAddress() + "  " + i + "  in " + (f == null ? "-" : f.getName()));
          }
      }
    }
  }
}
