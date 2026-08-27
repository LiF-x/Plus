// List every caller of cObjEffects::Assign_effect (RVA 0x4DC810) and
// decompile each. We want to know whether Resurrected's server-side
// apply goes through this function at all — and if not, the closest
// gameplay-side write path will be the next hook target.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxAssignEffectCallers extends GhidraScript {
    private static final long ASSIGN_EFFECT = 0x4DC810L;

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();

        Address target = base.add(ASSIGN_EFFECT);
        Set<Function> callers = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "assign_effect_callers.tsv"))))) {
            pw.println("from_func_rva\tfrom_func\tfrom_addr");
            ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(target);
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("0x%X\t%s\t%s%n", frva, f.getName(), r.getFromAddress());
                callers.add(f);
            }
        }
        println("Direct callers of Assign_effect: " + callers.size());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "assign_effect_callers_decomp.txt"))))) {
            for (Function f : callers) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/assign_effect_callers_*.{tsv,txt}");
    }
}
