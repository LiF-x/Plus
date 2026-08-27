// Xref + decompile the callers of cmUnitManager::parseDispatcherReply and a
// few related dispatcher-inbound entry points. The goal is to find the
// NetEvent / packet-processor that hands inbound dispatcher RPCs to the
// in-engine state machine — i.e. the spot a hook would need to live to
// re-enable the dispatcher protocol on a YO build. See issue #45.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxParseDspReplyCallers extends GhidraScript {

    private static final long[] TARGET_RVAS = {
        0x3CC860L, // cmUnitManager::parseDispatcherReply
        0x6F6F0L,  // SendServerUUIDEvent (one of the two surfaced fns)
        0x6F750L,  // SendServerUUIDEvent (the other)
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager refMgr = prog.getReferenceManager();
        Address base = prog.getImageBase();

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Set<Function> allCallers = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "parsedspreply_xrefs.tsv"))))) {
            pw.println("callee_rva\tcaller_rva\tcaller\tfrom_addr\tref_type");
            for (long rva : TARGET_RVAS) {
                Address a = base.add(rva);
                ReferenceIterator ri = refMgr.getReferencesTo(a);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    String fname = f == null ? "(no func)" : f.getName();
                    long frva = f == null ? -1 : f.getEntryPoint().getOffset() - base.getOffset();
                    pw.printf("0x%X\t0x%X\t%s\t%s\t%s%n",
                        rva, frva, fname, r.getFromAddress(), r.getReferenceType());
                    if (f != null) allCallers.add(f);
                }
            }
        }
        println("Unique callers: " + allCallers.size());

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "parsedspreply_callers_decomp.txt"))))) {
            for (Function f : allCallers) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 180, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/parsedspreply_{xrefs.tsv,callers_decomp.txt}");
    }
}
