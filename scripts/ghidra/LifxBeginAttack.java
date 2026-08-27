// #154 STRIKE: 0x18B2C0 is the only fn that writes the animal WeaponData* at this+0x24f0
// (the field endAttack 0x18A4D0 needs). Decompile it (+ its callers) and 0x188EF0
// (writes attack-type +0x24f8 / flag +0x24fc) to learn the native begin-attack setup
// the bandit is missing.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxBeginAttack extends GhidraScript {
    private static final long[] DECOMP = { 0x18B2C0L, 0x188EF0L };
    private static final long[] CALLERS_OF = { 0x18B2C0L };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);
        File out = new File(outDir, "begin_attack.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : DECOMP) decompile(pw, fm, dec, base, t, true);
            pw.println("\n########## CALLERS of 0x18B2C0 ##########");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            for (long t : CALLERS_OF) {
                ReferenceIterator ri = rm.getReferencesTo(base.add(t));
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function ff = fm.getFunctionContaining(r.getFromAddress());
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        pw.printf("   %-8s from 0x%X in %s (fn 0x%X)%n",
                                  r.getReferenceType(), r.getFromAddress().subtract(base), ff.getName(), frva);
                        if (r.getReferenceType().isCall()) callers.add(frva);
                    }
                }
            }
            int n = 0;
            for (long rva : callers) { if (n++ >= 4) break; decompile(pw, fm, dec, base, rva, false); }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva, boolean callees) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        if (callees) {
            pw.println("-- callees --");
            for (Function c : f.getCalledFunctions(monitor)) {
                Address ep = c.getEntryPoint();
                if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                    pw.printf("   (extern)  %s%n", c.getName());
                else pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
            }
        }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
