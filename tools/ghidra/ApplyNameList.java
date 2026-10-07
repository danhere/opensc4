// Applies OpenSC4's function name list (tools/re/name_list.py output: the
// address database plus data/names-641.txt) to the program.
//   - "Class::Method" becomes method Method in namespace Class (created if
//     needed); a plain name goes in the global namespace.
//   - Only functions that still have a default name (FUN_...) are renamed, so
//     names from Function ID, the game's symbols and earlier steps are kept;
//     a function is created where none exists yet.
//   - The note goes into the function's plate comment, prefixed "OpenSC4:".
// Prints one line per function it changed and a summary.
//
// Usage (headless, saves the program):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis
//     -scriptPath <repo>/tools/ghidra -postScript ApplyNameList.java <names.csv>
//@category OpenSC4

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;

public class ApplyNameList extends GhidraScript {
  // Minimal CSV: three fields, the last may contain commas and quotes.
  private static List<String> parse(String line) {
    List<String> fields = new ArrayList<>();
    StringBuilder cur = new StringBuilder();
    boolean quoted = false;
    for (int i = 0; i < line.length(); i++) {
      char c = line.charAt(i);
      if (quoted) {
        if (c == '"' && i + 1 < line.length() && line.charAt(i + 1) == '"') {
          cur.append('"');
          i++;
        } else if (c == '"') {
          quoted = false;
        } else {
          cur.append(c);
        }
      } else if (c == '"') {
        quoted = true;
      } else if (c == ',') {
        fields.add(cur.toString());
        cur.setLength(0);
      } else {
        cur.append(c);
      }
    }
    fields.add(cur.toString());
    return fields;
  }

  @Override
  public void run() throws Exception {
    String path = getScriptArgs()[0];
    int renamed = 0, created = 0, kept = 0, commented = 0;
    try (BufferedReader in = new BufferedReader(new FileReader(path))) {
      in.readLine(); // header
      String line;
      while ((line = in.readLine()) != null) {
        if (line.isBlank()) continue;
        List<String> f = parse(line);
        Address address = toAddr(Long.parseLong(f.get(0).substring(2), 16));
        String full = f.get(1);
        String note = f.size() > 2 ? f.get(2) : "";
        Function fn = getFunctionAt(address);
        if (fn == null) {
          fn = createFunction(address, null);
          if (fn == null) {
            println("cannot create a function at " + address + " for " + full);
            continue;
          }
          created++;
        }
        String comment = "OpenSC4: " + note;
        if (!comment.equals(fn.getComment())) {
          fn.setComment(comment);
          commented++;
        }
        if (!fn.getName().startsWith("FUN_")) {
          kept++;
          continue;
        }
        int split = full.lastIndexOf("::");
        String name = split < 0 ? full : full.substring(split + 2);
        Namespace ns = currentProgram.getGlobalNamespace();
        if (split >= 0) {
          ns = NamespaceUtils.createNamespaceHierarchy(full.substring(0, split), null, currentProgram,
              SourceType.USER_DEFINED);
        }
        fn.setParentNamespace(ns);
        fn.setName(name, SourceType.USER_DEFINED);
        println("named " + address + " " + full);
        renamed++;
      }
    }
    println(String.format("ApplyNameList: %d renamed, %d created, %d already named (kept), %d comments set",
        renamed, created, kept, commented));
  }
}
