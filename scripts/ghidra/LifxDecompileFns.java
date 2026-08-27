// Decompile a hardcoded list of functions and dump the result.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxDecompileFns extends GhidraScript {
    private static final long[] RVAS = {
        0x4DCC80L, // Resurrected apply candidate
        0x10EC00L,
        0x2CA020L,
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
                new FileWriter(new File(outDir, "candidate_decomp.txt"))))) {
            for (long rva : RVAS) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                if (f == null) {
                    pw.printf("=== RVA 0x%X: no function at address ===%n%n", rva);
                    continue;
                }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 90, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/candidate_decomp.txt");
    }
}
