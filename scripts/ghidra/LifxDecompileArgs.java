// Decompile a list of functions passed as hex-RVA script args.
// Usage: -postScript LifxDecompileArgs.java 0x361c0 0x2d45c0 ...
// Output: /tmp/lifx_ghidra/args_decomp.txt
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxDecompileArgs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "args_decomp.txt"))))) {
            for (String s : argv) {
                long rva = Long.decode(s);
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                if (f == null) {
                    // try containing function
                    f = fm.getFunctionContaining(a);
                }
                if (f == null) {
                    pw.printf("=== RVA 0x%X: no function ===%n%n", rva);
                    continue;
                }
                pw.printf("=== RVA 0x%X  %s  (entry 0x%X) ===%n", rva,
                        f.getName(), f.getEntryPoint().subtract(base));
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/args_decomp.txt");
    }
}
