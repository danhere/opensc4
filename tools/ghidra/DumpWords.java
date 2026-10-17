// DumpWords.java <addr> <count> [<addr> <count>...]: 32-bit words as hex (and as float).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpWords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        for (int a = 0; a + 1 < args.length; a += 2) {
            Address base = toAddr(Long.decode(args[a]));
            int count = Integer.parseInt(args[a + 1]);
            println("== " + base + " x" + count);
            StringBuilder line = new StringBuilder();
            for (int i = 0; i < count; i++) {
                int v = getInt(base.add(4L * i));
                line.append(String.format("[%d]=0x%X ", i, v));
                if (i % 8 == 7) { println("  " + line); line.setLength(0); }
            }
            if (line.length() > 0) println("  " + line);
        }
    }
}
