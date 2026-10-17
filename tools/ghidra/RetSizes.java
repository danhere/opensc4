// RetSizes.java <vtable addr> <slots>: for each vtable slot, the function's
// address and the distinct RET operand sizes (bytes the callee pops; for
// __thiscall that is the argument size).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.util.TreeSet;

public class RetSizes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address vt = toAddr(Long.decode(args[0]));
        int n = Integer.parseInt(args[1]);
        for (int i = 0; i < n; i++) {
            Address target = toAddr(getInt(vt.add(4L * i)) & 0xffffffffL);
            Function f = getFunctionAt(target);
            TreeSet<Integer> sizes = new TreeSet<>();
            if (f != null) {
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    if (!ins.getMnemonicString().equals("RET"))
                        continue;
                    Scalar s = ins.getNumOperands() > 0 ? ins.getScalar(0) : null;
                    sizes.add(s == null ? 0 : (int) s.getValue());
                }
            }
            println(String.format("%3d %s %s %s", i, target, sizes, f == null ? "?" : f.getName()));
        }
    }
}
