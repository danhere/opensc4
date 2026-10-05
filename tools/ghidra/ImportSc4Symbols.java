// Applies 0xC0000054/sc4-ghidra-symbols to the current program:
//   1. attaches every Function ID database in <repo>/function-id and runs the
//      Function ID analyzer once, naming the statically linked library code
//      (MSVC 2003 CRT, STLport 4.6, Lua 5.0, zlib, libpng);
//   2. copies every data type from the given archives (default SimCity4.gdt,
//      Gimex.gdt, Lua5.0.gdt) into the program's data type manager.
// This is the scripted form of the repository's manual instructions.
//
// Usage (headless, saves the program):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis
//     -scriptPath <repo>/tools/ghidra -postScript ImportSc4Symbols.java
//     <path to sc4-ghidra-symbols> [archive.gdt ...]
//@category OpenSC4

import java.io.File;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import ghidra.app.plugin.core.analysis.AutoAnalysisManager;
import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.analyzer.FidAnalyzer;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FileDataTypeManager;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ImportSc4Symbols extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: ImportSc4Symbols.java <sc4-ghidra-symbols dir> [archive.gdt ...]");
            return;
        }
        File repo = new File(args[0]);

        // 1. Function ID.
        int before = countNamedFunctions();
        File[] fidbs = new File(repo, "function-id").listFiles((dir, name) -> name.endsWith(".fidb"));
        if (fidbs == null || fidbs.length == 0) {
            printerr("no .fidb files in " + new File(repo, "function-id"));
            return;
        }
        FidFileManager fidFiles = FidFileManager.getInstance();
        for (File f : fidbs) {
            FidFile attached = fidFiles.addUserFidFile(f);
            println("Function ID database: " + f.getName() + (attached != null ? "" : " (already attached)"));
        }
        AutoAnalysisManager mgr = AutoAnalysisManager.getAnalysisManager(currentProgram);
        mgr.scheduleOneTimeAnalysis(new FidAnalyzer(), currentProgram.getMemory());
        mgr.startAnalysis(monitor);
        println("Named functions: " + before + " before Function ID, " + countNamedFunctions() + " after");

        // 2. Data types.
        List<File> archives = new ArrayList<>();
        for (int i = 1; i < args.length; i++)
            archives.add(new File(args[i]));
        if (archives.isEmpty())
            for (String name : new String[] { "SimCity4.gdt", "Gimex.gdt", "Lua5.0.gdt" })
                archives.add(new File(repo, name));

        DataTypeManager programTypes = currentProgram.getDataTypeManager();
        for (File archive : archives) {
            FileDataTypeManager source = FileDataTypeManager.openFileArchive(archive, false);
            int count = 0;
            try {
                for (Iterator<DataType> it = source.getAllDataTypes(); it.hasNext() && !monitor.isCancelled();) {
                    programTypes.resolve(it.next(), DataTypeConflictHandler.REPLACE_HANDLER);
                    count++;
                }
            } finally {
                source.close();
            }
            println("Imported " + count + " data types from " + archive.getName());
        }
    }

    private int countNamedFunctions() {
        int named = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getSymbol().getSource() != SourceType.DEFAULT)
                named++;
        }
        return named;
    }
}
