// FuncRefs.java <addr...>: one line per address: every reference to it (from address, type, containing function).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
public class FuncRefs extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      Address a = toAddr(s);
      StringBuilder b = new StringBuilder(s + ":");
      for (Reference r : getReferencesTo(a)) {
        Function f = getFunctionContaining(r.getFromAddress());
        b.append(" " + r.getFromAddress() + "/" + r.getReferenceType() + (f == null ? "" : "(" + f.getEntryPoint() + ")"));
      }
      println(b.toString());
    }
  }
}
