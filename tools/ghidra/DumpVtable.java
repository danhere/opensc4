// DumpVtable.java <vtable addr> <slots>: slot, target, size and name of each entry.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address base = toAddr(Long.decode(args[0]));
        int slots = Integer.parseInt(args[1]);
        for (int i = 0; i < slots; i++) {
            Address target = toAddr(getInt(base.add(4L * i)) & 0xFFFFFFFFL);
            Function f = getFunctionAt(target);
            println(String.format("%3d +%03x %s %5d %s", i, 4 * i, target,
                    f == null ? 0 : f.getBody().getNumAddresses(), f == null ? "?" : f.getName(true)));
        }
    }
}
