// #154 VISUAL: idle/walk replicate but the one-shot attack swing doesn't show.
// Decompile setAnimation (0x2E2A90) + SetAnimByIndex (0x2E2520) to learn the thread
// slot, the netmask/dirty-bit it sets (does the action thread replicate like the base
// thread?), the cyclic-vs-oneshot handling, and the meaning of the `flag` arg. Also
// decompile getCurrentAnimationName (0x2E2790) neighbours to see thread layout.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxAnimThread extends GhidraScript {
    private static final long[] DECOMP = {
        0x2E2A90L, // NPCS::AnimatedNPC::setAnimation (name->idx->SetByIndex)
        0x2E2520L, // SetAnimByIndex(self, idx, flag) — does it set a netmask?
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);
        File out = new File(outDir, "anim_thread.txt");
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
            else pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
        }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
