import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import java.util.*;
public class TypesLike extends GhidraScript {
  public void run() throws Exception {
    String[] pats = getScriptArgs();
    Iterator<DataType> it = currentProgram.getDataTypeManager().getAllDataTypes();
    while (it.hasNext()) {
      DataType t = it.next();
      for (String p : pats) if (t.getName().toLowerCase().contains(p.toLowerCase())) {
        println(t.getPathName() + " (" + t.getLength() + ")");
        if (t instanceof Structure && getScriptArgs().length > 0 && t.getName().length() < 40) {
          for (DataTypeComponent c : ((Structure)t).getDefinedComponents())
            if (c.getFieldName()!=null) println("    +" + Integer.toHexString(c.getOffset()) + " " + c.getDataType().getName() + " " + c.getFieldName());
        }
        break;
      }
    }
  }
}
