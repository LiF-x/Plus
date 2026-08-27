// Walk every reference to the DspUtil singleton slot (RVA 0xbf1a60) and the
// SendServerUUIDEvent vtable / cmUnitManager dispatcher reply paths. This is
// the actual game-dispatcher surface — distinct from the Qt
// QtEventDispatcherWin32 noise that the broader "Dispatcher" substring scan
// picked up. See issue #45.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxDspUtilXrefs extends GhidraScript {

    // Image-base-relative addresses observed in this build. If this script
    // breaks on a future game update, re-run LifxDispatcherScan.java first to
    // find the equivalent locations and update these constants.
    private static final long[] TARGET_RVAS = {
        0xBF1A60L, // DspUtil singleton slot
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager refMgr = prog.getReferenceManager();
        Address base = prog.getImageBase();

        Set<Function> callers = new LinkedHashSet<>();
        try (PrintWriter pwList = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsputil_xrefs.tsv"))))) {
            pwList.println("target_rva\tfrom_func_rva\tfrom_func\tfrom_addr\tref_type");
            for (long rva : TARGET_RVAS) {
                Address a = base.add(rva);
                ReferenceIterator ri = refMgr.getReferencesTo(a);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    String fname = f == null ? "(no func)" : f.getName();
                    long frva = f == null ? -1 : f.getEntryPoint().getOffset() - base.getOffset();
                    pwList.printf("0x%X\t0x%X\t%s\t%s\t%s%n",
                        rva, frva, fname, r.getFromAddress(), r.getReferenceType());
                    if (f != null) callers.add(f);
                }
            }
        }
        println("Unique caller functions: " + callers.size());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsputil_callers_decomp.txt"))))) {
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
        println("DONE. /tmp/lifx_ghidra/dsputil_{xrefs.tsv,callers_decomp.txt}");
    }
}
