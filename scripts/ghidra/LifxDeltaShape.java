// Decompile FUN_14010cca0 (deltaList→event copy) and FUN_1404dc810
// (effect-manager insert). These together reveal the per-row delta format
// expected by the broadcast call.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxDeltaShape extends GhidraScript {
    private static final long[] RVAS = {
        0x10CCA0L, // copy ctor for deltaList container
        0x4DC810L, // manager-side insert (called by handler option 3)
        0x4DC2C0L, // ctor helper used in ObjEffectsEvent ctor (puVar3 + 9)
        0x548B50L, // ctor helper called by both ObjEffectsEvent ctors
        0x548B00L, // copy variant
        0x4DC810L,
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

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "delta_shape.txt"))))) {
            for (long rva : RVAS) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                if (f == null) {
                    pw.printf("=== RVA 0x%X: no function ===%n%n", rva);
                    continue;
                }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/delta_shape.txt");
    }
}
