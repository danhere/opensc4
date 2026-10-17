import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import java.util.*;
public class VftablesOf extends GhidraScript {
  public void run() throws Exception {
    String pat = getScriptArgs()[0];
    Iterator<DataType> it = currentProgram.getDataTypeManager().getAllDataTypes();
    while (it.hasNext()) {
      DataType t = it.next();
      if (!(t instanceof Structure) || !t.getName().startsWith("vftable")) continue;
      Structure s = (Structure) t; boolean hit = false;
      for (DataTypeComponent c : s.getDefinedComponents()) if (c.getFieldName()!=null && c.getFieldName().contains(pat)) hit = true;
      if (!hit) continue;
      println(t.getName() + " (" + t.getLength() + ")");
      for (DataTypeComponent c : s.getDefinedComponents()) println("    +" + Integer.toHexString(c.getOffset()) + " " + c.getFieldName());
    }
  }
}
