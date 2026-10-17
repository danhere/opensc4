import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;
import java.io.*;
public class OrphanCodeSurvey extends GhidraScript {
  public void run() throws Exception {
    PrintWriter w = new PrintWriter(getScriptArgs()[0]);
    w.println("start,end,bytes,insns,prev_func,refs_in,ref_kinds,first_mnemonic,ends_with");
    Listing L = currentProgram.getListing();
    InstructionIterator it = L.getInstructions(true);
    Address start=null, last=null, lastEnd=null; int n=0; Instruction lastI=null;
    long tot=0; int runs=0;
    while (it.hasNext()) {
      Instruction i = it.next();
      boolean orphan = getFunctionContaining(i.getAddress()) == null;
      boolean contiguous = lastEnd != null && i.getAddress().equals(lastEnd);
      if (start != null && (!orphan || !contiguous)) { emit(w,start,lastI,n); tot += lastI.getMaxAddress().subtract(start)+1; runs++; start=null; }
      if (orphan) { if (start==null){start=i.getAddress(); n=0;} n++; lastI=i; }
      lastEnd = i.getMaxAddress().next();
    }
    if (start!=null){emit(w,start,lastI,n); runs++;}
    w.close(); println("runs="+runs+" bytes="+tot);
  }
  void emit(PrintWriter w, Address s, Instruction li, int n) {
    Function pf = getFunctionBefore(s);
    Map<String,Integer> kinds = new TreeMap<>(); int refs=0;
    AddressSet set = new AddressSet(s, li.getMaxAddress());
    for (Address a : currentProgram.getReferenceManager().getReferenceDestinationIterator(set, true))
      for (Reference r : getReferencesTo(a)) {
        Function f = getFunctionContaining(r.getFromAddress());
        if (f==null && set.contains(r.getFromAddress())) continue;
        refs++;
        String k = r.getReferenceType().toString() + (f==null ? "@nofunc" : (pf!=null && f.equals(pf) ? "@prev" : "@other"));
        kinds.merge(k,1,Integer::sum);
      }
    Instruction fi = getInstructionAt(s);
    w.println(s+","+li.getMaxAddress()+","+(li.getMaxAddress().subtract(s)+1)+","+n+","+(pf==null?"":pf.getName()+"@"+pf.getEntryPoint())+","+refs+","+kinds.toString().replace(","," ")+","+fi.getMnemonicString()+","+li.getMnemonicString());
  }
}
