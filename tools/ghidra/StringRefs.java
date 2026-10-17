import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
// StringRefs <text...>: addresses of each ASCII string and the functions that reference it
public class StringRefs extends GhidraScript {
  public void run() throws Exception {
    Memory mem = currentProgram.getMemory();
    for (String s : getScriptArgs()) {
      byte[] pat = (s + "\0").getBytes("US-ASCII");
      Address a = mem.getMinAddress();
      while ((a = mem.findBytes(a, pat, null, true, monitor)) != null) {
        println(s + " @ " + a);
        for (Reference r : getReferencesTo(a)) {
          Function f = getFunctionContaining(r.getFromAddress());
          println("  <- " + r.getFromAddress() + " in " + (f == null ? "-" : f.getName() + "@" + f.getEntryPoint()));
        }
        a = a.add(1);
      }
    }
  }
}
