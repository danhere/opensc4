// FindScalar.java <value...>: instructions with the given immediate operand (hex), with their function.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class FindScalar extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] a = getScriptArgs();
    long[] want = new long[a.length];
    for (int i = 0; i < a.length; i++) want[i] = Long.parseUnsignedLong(a[i].replace("0x", ""), 16);
    InstructionIterator it = currentProgram.getListing().getInstructions(true);
    while (it.hasNext() && !monitor.isCancelled()) {
      Instruction ins = it.next();
      for (int op = 0; op < ins.getNumOperands(); op++) {
        for (Object o : ins.getOpObjects(op)) {
          if (!(o instanceof Scalar)) continue;
          long v = ((Scalar) o).getUnsignedValue() & 0xFFFFFFFFL;
          for (long w : want) {
            if (v == w) {
              Function f = getFunctionContaining(ins.getAddress());
              println(String.format("%08x %s  [%s]", ins.getAddress().getOffset(), ins,
                  f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
            }
          }
        }
      }
    }
  }
}
