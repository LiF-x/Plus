// List callers (references) to functions given as hex-RVA args.
// Usage: -postScript LifxXrefs.java 0x2d78c0 0x2d4ba0 ...
// Output: /tmp/lifx_ghidra/xrefs.txt
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class LifxXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "xrefs.txt"))))) {
            for (String s : argv) {
                long rva = Long.decode(s);
                Address a = base.add(rva);
                Function tgt = fm.getFunctionAt(a);
                pw.printf("===== callers of 0x%X %s =====%n", rva,
                        tgt == null ? "(no func)" : tgt.getName());
                ReferenceIterator it = rm.getReferencesTo(a);
                int n = 0;
                while (it.hasNext()) {
                    Reference r = it.next();
                    Address from = r.getFromAddress();
                    Function cf = fm.getFunctionContaining(from);
                    pw.printf("    from 0x%06X  %s  (%s)%n",
                            from.subtract(base),
                            cf == null ? "(no func)" : cf.getName(),
                            r.getReferenceType());
                    n++;
                }
                if (n == 0) pw.println("    (no references)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/xrefs.txt");
    }
}
