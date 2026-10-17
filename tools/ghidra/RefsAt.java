import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
public class RefsAt extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      Address a = toAddr(s);
      for (Reference r : getReferencesTo(a)) {
        Instruction i = getInstructionContaining(r.getFromAddress());
        Function f = getFunctionContaining(r.getFromAddress());
        println(s+" <- "+r.getFromAddress()+" "+r.getReferenceType()+" "+(i==null?"data":i.toString())+" in "+(f==null?"-":f.getName()+"@"+f.getEntryPoint()));
      }
      Address p = a.subtract(16);
      for (Instruction i = getInstructionAt(p) != null ? getInstructionAt(p) : getInstructionAfter(p); i != null && i.getAddress().compareTo(a.add(24)) < 0; i = i.getNext())
        println("  " + i.getAddress() + "  " + i);
    }
  }
}
