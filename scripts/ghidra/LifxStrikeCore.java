// #154 STRIKE: FUN_14018bd30 is the per-tick melee-contact handler (checks current
// anim == Attack_Fast/Power, then calls strike FUN_1405e0f80 on *(this+0x920)). It's
// the ideal single-call strike entry. Confirm FUN_1405e0f80 (0x5E0F80) reaches damage
// / _applyHit, learn what this+0x920 is (the swing/weapon-image obj the AI poll reads),
// and inspect the consume FUN_14018a4d0 + getCurrentAnim FUN_1402e2790.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxStrikeCore extends GhidraScript {
    private static final long[] DECOMP = {
        0x5E0F80L, // strike core (the hit)
        0x18A4D0L, // consume-contact
        0x2E2790L, // getCurrentAnim
        0x5FD550L, // AI-poll helper A (reads +0x920,+0x24e8)
        0x5FD700L, // AI-poll helper B
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);
        File out = new File(outDir, "strike_core.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : DECOMP) decompile(pw, fm, dec, base, t);
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
}
