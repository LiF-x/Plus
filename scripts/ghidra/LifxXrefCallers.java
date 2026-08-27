// #154 strike RE: dump all references TO the hit-path functions and decompile the
// direct CALL callers — to find the player melee-swing initiator that builds the hit
// descriptor and invokes victim->_applyHit (the construction Route B should reuse).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxXrefCallers extends GhidraScript {

    private static final long[] TARGETS = {
        0x0EE0F0L, // Player::_applyHit (victim side; reads descriptor param_2)
        0x2E2E60L, // NPCS::Base::_applyHit
        0x0A4BF0L, // ONEPUNCHMAN_DAMAGE_CALC
        0x091A50L, // CHAR_CALC_HIT_DAMAGE (builds local from descriptor)
        0x1939A0L, // an Attack-slot fn (slot 10) — check its role
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        LinkedHashSet<Long> callers = new LinkedHashSet<>();
        File out = new File(outDir, "weapon_re4_callers.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : TARGETS) {
                Address ta = base.add(t);
                Function tf = fm.getFunctionAt(ta);
                pw.println();
                pw.printf("#### references TO 0x%X  %s ####%n", t, (tf == null ? "?" : tf.getName()));
                ReferenceIterator ri = rm.getReferencesTo(ta);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address fa = r.getFromAddress();
                    Function ff = fm.getFunctionContaining(fa);
                    String kind = r.getReferenceType().toString();
                    boolean isCall = r.getReferenceType().isCall();
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        pw.printf("   %-14s from %s in %s (fn 0x%X)%n",
                                  kind, relAddr(fa, base), ff.getName(), frva);
                        if (isCall) callers.add(frva);
                    } else {
                        pw.printf("   %-14s from %s (no containing fn — vtable/data slot)%n",
                                  kind, relAddr(fa, base));
                    }
                }
            }

            pw.println();
            pw.println("==================== DIRECT CALL CALLERS (decompiled) ====================");
            for (long rva : callers) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n==================== RVA 0x%X  %s ====================%n",
                          rva, (f == null ? "?" : f.getName()));
                if (f == null) continue;
                pw.println("-- callees --");
                for (Function c : f.getCalledFunctions(monitor)) {
                    Address ep = c.getEntryPoint();
                    if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                        pw.printf("   (extern)  %s%n", c.getName());
                    else
                        pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
                }
                pw.println("-- decompile --");
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private static String relAddr(Address a, Address base) {
        if (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
            return String.format("0x%X", a.subtract(base));
        return String.valueOf(a);
    }
}
