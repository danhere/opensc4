import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
// args: <coverage.csv> <class name substring>
public class VtableSlotsRan extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    Set<Long> hits = new HashSet<>();
    BufferedReader r = new BufferedReader(new FileReader(a[0])); r.readLine(); String l;
    while ((l = r.readLine()) != null) hits.add(Long.decode(l.split(",")[0]));
    SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
    while (it.hasNext()) {
      Symbol s = it.next();
      if (!s.getName().startsWith("vftable") || !s.getParentNamespace().getName().contains(a[1])) continue;
      int slots = 0, ran = 0; String big = "";
      Address p = s.getAddress();
      for (int i = 0; i < 200; i++, p = p.add(4)) {
        if (i > 0 && getSymbolAt(p) != null && getSymbolAt(p).getName().startsWith("vftable")) break;
        long v = getInt(p) & 0xFFFFFFFFL;
        Function f = getFunctionAt(toAddr(v));
        if (f == null) break;
        slots++; if (hits.contains(v) && f.getBody().getNumAddresses() > 16) { ran++; big += " " + f.getEntryPoint() + "(" + f.getBody().getNumAddresses() + ")"; }
      }
      println(s.getParentNamespace().getName() + "::" + s.getName() + " @" + s.getAddress() + " slots " + slots + " substantial ran " + ran + big);
    }
  }
}
