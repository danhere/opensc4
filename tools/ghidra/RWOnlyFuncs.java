import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class RWOnlyFuncs extends GhidraScript {
  public void run() throws Exception {
    int n=0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      boolean rw=false, other=false;
      for (Reference r : getReferencesTo(f.getEntryPoint())) {
        RefType t=r.getReferenceType();
        if (t.isRead()||t.isWrite()) rw=true; else other=true;
      }
      if (rw && !other) { n++; if (n<=40) println("RW " + f.getEntryPoint()+" "+f.getName()+" size "+f.getBody().getNumAddresses()+" "+ getInstructionAt(f.getEntryPoint())); }
    }
    println("rw-only functions: "+n);
  }
}
