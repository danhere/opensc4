// SlotOf.java <addr...>: for a vtable entry address, the nearest vftable label at or before it and the slot offset.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Symbol;
public class SlotOf extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      Address a = toAddr(s);
      for (Address b = a; a.subtract(b) < 0x1000; b = b.subtract(4)) {
        Symbol sym = getSymbolAt(b);
        if (sym != null && sym.getName(true).contains("vftable")) {
          println(String.format("%s: %s +0x%X (slot %d)", s, sym.getName(true), a.subtract(b), a.subtract(b) / 4));
          break;
        }
      }
    }
  }
}
