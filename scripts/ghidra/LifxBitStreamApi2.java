// Issue #68 follow-up. Confirm BitStream::writeInt = FUN_140086330, find
// the per-event packer FUN_140540EB0 (called from the drainer's inner
// loop), and dump anything else the packer calls so we can pick up
// writeBytes/writeBuffer.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;

public class LifxBitStreamApi2 extends GhidraScript {

    private static final long[] FN_RVAS = {
        0x086330L, // candidate BitStream::writeInt
        0x086430L, // adjacent — possibly BitStream::write (bytes)
        0x540EB0L, // per-event packer called from FUN_140542630
        0x447970L, // core BitStream::read (used by readInt)
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Set<Function> callees = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "bitstream_api2.txt"))))) {
            for (long rva : FN_RVAS) {
                Function f = fm.getFunctionAt(base.add(rva));
                if (f == null) { pw.printf("(no fn at 0x%X)%n%n", rva); continue; }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                String code = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)";
                pw.println(code);
                pw.println();

                // Collect callees of FUN_140540EB0 specifically — that's the per-event packer.
                if (rva == 0x540EB0L && r.decompileCompleted()) {
                    HighFunction hf = r.getHighFunction();
                    if (hf != null) {
                        Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                        while (ops.hasNext()) {
                            PcodeOpAST op = ops.next();
                            if (op.getOpcode() != PcodeOp.CALL) continue;
                            Varnode tgt = op.getInput(0);
                            if (tgt == null || !tgt.isAddress()) continue;
                            Function cf = fm.getFunctionAt(tgt.getAddress());
                            if (cf != null && cf != f) callees.add(cf);
                        }
                    }
                }
            }
            pw.println("=== callees of FUN_140540EB0 (per-event packer) ===");
            for (Function f : callees) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/bitstream_api2.txt");
    }
}
