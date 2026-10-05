// Creates functions for code that auto-analysis disassembled but left outside
// every function body. Without RTTI, Ghidra does not treat vtable entries and
// other code pointers in data as function starts, so most virtual methods,
// callback tables and exception funclets end up as loose instructions. They are
// invisible to function-level tools such as the coverage meter.
//
// A function is created at an instruction outside any function when it is
// referenced by
//   - a 4-byte pointer in a non-executable block (vtables, callback and
//     exception tables), or
//   - an instruction operand (PUSH/MOV of the address) inside a function or
//     in the not-yet-attributed code itself (exception handler stubs, callbacks).
// and the address can start a function: the code before it does not run into
// it (RET, JMP, INT3 or NOP padding, a RET byte or data). Constants in data
// that only look like code pointers (IDs, strings) otherwise land inside real
// instructions; auto-analysis disassembles from some of them, for example
// from 0x00AAED58 into the middle of MOV EAX,[0x00B43D94] at 0x00707864. The
// coverage meter's INT3 there changes the address it loads and crashes the game.
// Jumps into loose code are not function starts; that code is picked up as part
// of the body of the function that reaches it. Passes repeat until nothing new
// is found, because new bodies expose new references.
//
// Usage (headless, saves the program):
//   analyzeHeadless <projdir> <project> -process "SimCity 4.exe" -noanalysis
//     -scriptPath <repo>/tools/ghidra -postScript DefineOrphanFunctions.java
//@category OpenSC4

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

public class DefineOrphanFunctions extends GhidraScript {

    @Override
    protected void run() throws Exception {
        int total = 0, failed = 0;
        for (int pass = 1; pass <= 10; pass++) {
            AddressSet starts = new AddressSet();
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction insn = it.next();
                Address a = insn.getAddress();
                if (getFunctionContaining(a) == null && isCodePointerTarget(a) && canStartFunction(a))
                    starts.add(a);
            }
            if (starts.isEmpty())
                break;
            int created = 0;
            for (Address a : starts.getAddresses(true)) {
                if (getFunctionContaining(a) != null)
                    continue; // absorbed by a function created earlier in this pass
                CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                if (cmd.applyTo(currentProgram, monitor))
                    created++;
                else
                    failed++;
            }
            println(String.format("pass %d: %d candidates, %d functions created", pass, starts.getNumAddresses(), created));
            total += created;
            if (created == 0)
                break;
        }
        println(String.format("created %d functions (%d failed)", total, failed));
    }

    private boolean isCodePointerTarget(Address a) {
        for (Reference r : getReferencesTo(a)) {
            RefType t = r.getReferenceType();
            if (t.isFlow())
                continue; // jumps and calls
            Address from = r.getFromAddress();
            MemoryBlock block = getMemoryBlock(from);
            if (block == null)
                continue;
            if (!block.isExecute())
                return true; // pointer in data
            if (getInstructionContaining(from) != null)
                return true; // address used as an operand
        }
        return false;
    }

    private boolean canStartFunction(Address a) throws Exception {
        Address before = a.subtract(1);
        Instruction prev = getInstructionContaining(before);
        if (prev != null) {
            if (!prev.getMaxAddress().equals(before))
                return false;
            String m = prev.getMnemonicString();
            return m.equals("INT3") || m.equals("NOP") || !prev.getFlowType().hasFallthrough();
        }
        int b = getByte(before) & 0xFF;
        if (b == 0xCC || b == 0x90 || b == 0xC3)
            return true; // padding, or a RET that was never disassembled
        Data data = getDataContaining(before);
        return data != null && data.isDefined();
    }
}
