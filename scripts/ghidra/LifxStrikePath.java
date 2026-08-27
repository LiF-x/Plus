// #154 STRIKE RE: map the melee damage path so the bandit's swing can deal damage.
// Decompile the AI Attack node + swing chain and the hit-application chain, and find
// every direct CALL caller of the _applyHit fns (the player/NPC melee strike that
// BUILDS the hit descriptor — the construction our explicit strike must mirror).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxStrikePath extends GhidraScript {

    // chain to read top-down
    private static final long[] DECOMP = {
        0x193400L, // Attack::process (AI node)
        0x1939A0L, // Attack-slot fn (slot 10)
        0x18B950L, // swing
        0x0EE0F0L, // Player::_applyHit (victim side; reads descriptor param_2)
        0x2E2E60L, // NPCS::Base::_applyHit
        0x091A50L, // CHAR_CALC_HIT_DAMAGE (builds local from descriptor)
        0x0A4BF0L, // ONEPUNCHMAN_DAMAGE_CALC
    };
    // find who CALLS these (descriptor builders)
    private static final long[] CALLERS_OF = {
        0x0EE0F0L, 0x2E2E60L,
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "strike_path.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : DECOMP) decompile(pw, fm, dec, base, t);

            pw.println();
            pw.println("################## CALLERS (descriptor builders) ##################");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            for (long t : CALLERS_OF) {
                pw.printf("%n#### refs TO 0x%X ####%n", t);
                ReferenceIterator ri = rm.getReferencesTo(base.add(t));
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function ff = fm.getFunctionContaining(r.getFromAddress());
                    boolean call = r.getReferenceType().isCall();
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        pw.printf("   %-10s from %s in %s (fn 0x%X)%n",
                                  r.getReferenceType(), rel(r.getFromAddress(), base), ff.getName(), frva);
                        if (call) callers.add(frva);
                    } else {
                        pw.printf("   %-10s from %s (vtable/data slot)%n",
                                  r.getReferenceType(), rel(r.getFromAddress(), base));
                    }
                }
            }
            int n = 0;
            for (long rva : callers) {
                if (n++ >= 10) { pw.println("(callers capped at 10)"); break; }
                decompile(pw, fm, dec, base, rva);
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        pw.println("-- callees --");
        for (Function c : f.getCalledFunctions(monitor)) {
            Address ep = c.getEntryPoint();
            if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                pw.printf("   (extern)  %s%n", c.getName());
            else
                pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
        }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }

    private static String rel(Address a, Address base) {
        if (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
            return String.format("0x%X", a.subtract(base));
        return String.valueOf(a);
    }
}
