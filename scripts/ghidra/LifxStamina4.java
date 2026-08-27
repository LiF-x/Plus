// #154 stamina pt4: 0x3DEA90 registers "stamina"+"staminaRegen" datablock fields = the
// player's stamina SETTINGS (max + regen). Decompile it (reveals field offsets + the
// registration helper, so we learn where stamina/regen live and their defaults), plus
// the Restore/Raise-stamina handlers (0x4DCC80) and the player-side stamina users.
// Also xref the stamina data strings to find every function that reads these settings.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxStamina4 extends GhidraScript {
    private static final long[] DECOMP = { 0x3DEA90L, 0x4DCC80L, 0x4DFA60L };
    // stamina-related data string addresses (from pt1)
    private static final long[] STR_ADDRS = {
        0x73D220L, // StaminaRate
        0x83AE48L, // stamina
        0x83AE50L, // staminaRegen
        0x89D338L, // Restore Hard Stamina
        0x89D350L, // Raise maximum Soft Stamina
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "stamina4.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("########## xrefs to stamina setting strings ##########");
            LinkedHashSet<Long> fns = new LinkedHashSet<>();
            for (long sa : STR_ADDRS) {
                pw.printf("%n-- refs to 0x%X --%n", sa);
                ReferenceIterator ri = rm.getReferencesTo(base.add(sa));
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function ff = fm.getFunctionContaining(r.getFromAddress());
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        fns.add(frva);
                        pw.printf("   from 0x%X in fn 0x%X%n", r.getFromAddress().subtract(base), frva);
                    }
                }
            }

            pw.println();
            pw.println("########## decompiles ##########");
            LinkedHashSet<Long> all = new LinkedHashSet<>();
            for (long d : DECOMP) all.add(d);
            for (long f : fns) all.add(f);
            int n = 0;
            for (long rva : all) { if (n++ >= 10) { pw.println("(capped 10)"); break; } decompile(pw, fm, dec, base, rva); }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        DecompileResults res = dec.decompileFunction(f, 90, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
