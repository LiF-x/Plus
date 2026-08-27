// Decompile FUN_1400ebff0 (effect delta broadcaster), all its callers,
// FUN_1402aa030 (objectGID getter?), and FUN_140091610 (per-entry apply
// primitive). This maps the AddEffect/RemoveEffect emission path.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxBroadcasterTrace extends GhidraScript {
    private static final long BROADCASTER_RVA = 0xEBFF0L;
    private static final long[] EXTRA = { 0x2AA030L, 0x91610L };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Set<Function> toDecomp = new LinkedHashSet<>();
        toDecomp.add(fm.getFunctionAt(base.add(BROADCASTER_RVA)));
        for (long e : EXTRA) toDecomp.add(fm.getFunctionAt(base.add(e)));

        // Add direct callers of broadcaster
        Address bcast = base.add(BROADCASTER_RVA);
        try (PrintWriter pwList = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "broadcaster_callers.tsv"))))) {
            pwList.println("from_func_rva\tfrom_func\tfrom_addr");
            ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(bcast);
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pwList.printf("0x%X\t%s\t%s%n", frva, f.getName(), r.getFromAddress());
                toDecomp.add(f);
            }
        }

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "broadcaster_trace.txt"))))) {
            for (Function f : toDecomp) {
                if (f == null) continue;
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
        println("DONE. /tmp/lifx_ghidra/broadcaster_*.{tsv,txt}");
    }
}
