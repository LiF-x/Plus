// #154 STRIKE: the two extra swing fns (0x18CCD0, 0x18BD30) reference Attack_Fast/
// Attack_Power like the AI swing 0x18B950 but are distinct -> candidate PLAYER swing
// that wires contact->damage. Decompile them + their callers, and check whether any
// reach _applyHit (0xEE0F0 / 0x2E2E60). Also decompile 0x18B950's CALLERS to learn the
// full call graph above the animation-only swing.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxPlayerSwing extends GhidraScript {

    private static final long[] DECOMP = { 0x18CCD0L, 0x18BD30L };
    private static final long[] CALLERS_OF = { 0x18CCD0L, 0x18BD30L, 0x18B950L };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "player_swing.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : DECOMP) decompile(pw, fm, dec, base, t, true);

            pw.println();
            pw.println("########## CALLERS ##########");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            for (long t : CALLERS_OF) {
                pw.printf("%n#### refs TO 0x%X ####%n", t);
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
            for (long rva : callers) {
                if (n++ >= 6) { pw.println("(callers capped at 6)"); break; }
                decompile(pw, fm, dec, base, rva, false);
            }
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
                else
                    pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
            }
        }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
