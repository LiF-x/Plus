// #154 stamina pt5: locate the soft-stamina VALUE field + the decrement(cost) + regen.
// 0xEBC90 (calls On_soft_stamina_exhausted 0x96D00) is a method of the soft-stamina
// component; its sibling methods (get/set/add/subtract/tick) cluster nearby. Decompile
// every function entry in [0xEB000, 0xEC300], plus the abstract vital tick 0x97BC0 and
// the soft-stamina exhausted handler 0x96D00, so we can read the value/max offsets and
// the cost argument the player swing passes.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxStamina5 extends GhidraScript {
    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "stamina5.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            decompile(pw, fm, dec, base, 0x96D00L);
            decompile(pw, fm, dec, base, 0x97BC0L);
            // sweep the soft-stamina component method cluster
            FunctionIterator it = fm.getFunctions(base.add(0xEB000L), true);
            int n = 0;
            while (it.hasNext() && n < 40) {
                Function f = it.next();
                long rva = f.getEntryPoint().subtract(base);
                if (rva < 0xEB000L) continue;
                if (rva > 0xEC300L) break;
                decompile(pw, fm, dec, base, rva);
                n++;
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        DecompileResults res = dec.decompileFunction(f, 60, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
