import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
public class StartCheck extends GhidraScript {
  public void run() throws Exception {
    BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0])); r.readLine();
    Map<String,Integer> why = new TreeMap<>(); String line; int rej=0, n=0;
    while ((line = r.readLine()) != null) {
      Address a = toAddr(line.split(",")[0]); n++;
      String k = classify(a); why.merge(k,1,Integer::sum);
      if (k.startsWith("reject")) { rej++; if (rej <= 25) println("REJ " + a + " " + k + " first=" + getInstructionAt(a)); }
    }
    println(n + " checked; " + why);
  }
  String classify(Address a) throws Exception {
    Address p = a.subtract(1);
    Instruction prev = getInstructionContaining(p);
    if (prev != null) {
      if (!prev.getMaxAddress().equals(p)) return "reject: inside an instruction";
      String m = prev.getMnemonicString();
      if (m.equals("INT3") || m.equals("NOP")) return "ok: padding insn";
      return prev.getFlowType().hasFallthrough() ? "reject: previous falls through (" + m + ")" : "ok: after " + m;
    }
    int b = getByte(p) & 0xFF;
    if (b == 0xCC || b == 0x90) return "ok: padding byte";
    if (getDataContaining(p) != null && getDataContaining(p).isDefined()) return "ok: after defined data";
    return String.format("reject: undefined byte %02X before", b);
  }
}
