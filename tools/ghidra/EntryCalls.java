// EntryCalls.java <start> <end>: functions inside [start, end) that are called (or referenced) from outside it,
// with each outside reference.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;

public class EntryCalls extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] a = getScriptArgs();
    Address start = toAddr(Long.parseUnsignedLong(a[0].replace("0x", ""), 16));
    Address end = toAddr(Long.parseUnsignedLong(a[1].replace("0x", ""), 16));
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(start, true);
    while (it.hasNext()) {
      Function f = it.next();
      if (f.getEntryPoint().compareTo(end) >= 0) break;
      StringBuilder sb = new StringBuilder();
      int n = 0;
      for (Reference r : getReferencesTo(f.getEntryPoint())) {
        Address from = r.getFromAddress();
        if (from.compareTo(start) >= 0 && from.compareTo(end) < 0) continue;
        Function c = getFunctionContaining(from);
        if (n++ < 6) sb.append(String.format(" %08x(%s)", from.getOffset(), c == null ? "?" : c.getName()));
      }
      if (n > 0) println(String.format("%08x %s <- %d:%s", f.getEntryPoint().getOffset(), f.getName(), n, sb));
    }
  }
}
