// Find every caller of ObjEffectsEvent's parameterized constructor
// (FUN_14010cde0) and default constructor (FUN_14010ce70). These are the
// server-side senders of "effect changed" net events.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxObjEffectsCtorCallers extends GhidraScript {

    private static final long[] CTORS = { 0x10CDE0L, 0x10CE70L };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();

        Set<Function> callers = new LinkedHashSet<>();
        try (PrintWriter pwList = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "objeffectsevent_ctor_callers.tsv"))))) {
            pwList.println("ctor_rva\tfrom_func_rva\tfrom_func\tfrom_addr");
            for (long rva : CTORS) {
                Address target = base.add(rva);
                ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(target);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f == null) continue;
                    long frva = f.getEntryPoint().getOffset() - base.getOffset();
                    pwList.printf("0x%X\t0x%X\t%s\t%s%n",
                                  rva, frva, f.getName(), r.getFromAddress());
                    callers.add(f);
                }
            }
        }
        // Don't decompile the constructors themselves — only their callers.
        callers.removeIf(f -> {
            long rva = f.getEntryPoint().getOffset() - base.getOffset();
            for (long c : CTORS) if (c == rva) return true;
            return false;
        });
        println("Unique non-ctor callers: " + callers.size());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "objeffectsevent_ctor_callers_decomp.txt"))))) {
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
        println("DONE. /tmp/lifx_ghidra/objeffectsevent_ctor_callers_*.{tsv,txt}");
    }
}
