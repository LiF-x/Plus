// Find who calls into DspUtil's tracking methods from the network side, plus
// dump cmUnitManager::parseDispatcherReply in full. Goal: identify the
// inbound DispatcherEvent packet path and (if present) the YO-mode gate that
// might disable it. See issue #45.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxDspInboundScan extends GhidraScript {

    // Methods on / accessors of DspUtil that mutate or query its state. The
    // network-side packet handlers should call these — that's our way in.
    private static final long[] DSPUTIL_METHOD_RVAS = {
        0x535D00L, // DspUtil::_dropCharacter
        0x536180L, // DspUtil::_trackCharacter
        0x5367A0L, // DspUtil lookup-by-charId helper
        0x5368E0L, // DspUtil isConnected helper
        0x535150L, // DspUtil accessor
        0x5351A0L, // DspUtil ctor / get-instance
    };

    // Full-decomp targets (no xref walking).
    private static final long[] DUMP_RVAS = {
        0x3CC860L, // cmUnitManager::parseDispatcherReply
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

        // 1) Xref the DspUtil methods. Capture who calls each.
        Map<Long, Set<Function>> callers = new LinkedHashMap<>();
        Set<Function> allCallers = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_inbound_xrefs.tsv"))))) {
            pw.println("callee_rva\tcaller_rva\tcaller\tfrom_addr");
            for (long rva : DSPUTIL_METHOD_RVAS) {
                Address a = base.add(rva);
                Function callee = fm.getFunctionAt(a);
                Set<Function> bucket = new LinkedHashSet<>();
                callers.put(rva, bucket);
                if (callee == null) {
                    pw.printf("0x%X\t(no function at addr)%n", rva);
                    continue;
                }
                ReferenceIterator ri = refMgr.getReferencesTo(a);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f == null) continue;
                    long frva = f.getEntryPoint().getOffset() - base.getOffset();
                    pw.printf("0x%X\t0x%X\t%s\t%s%n", rva, frva, f.getName(), r.getFromAddress());
                    bucket.add(f);
                    allCallers.add(f);
                }
            }
        }
        println("Unique callers across DspUtil methods: " + allCallers.size());

        // 2) Decompile every caller (so we can spot packet-handler shapes:
        //    NetEvent::unpack / readPacket / pack / write).
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_inbound_callers_decomp.txt"))))) {
            for (Function f : allCallers) {
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

        // 3) Dump fixed targets in full (parseDispatcherReply).
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_inbound_dump.txt"))))) {
            for (long rva : DUMP_RVAS) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                if (f == null) {
                    pw.printf("=== RVA 0x%X: no function ===%n%n", rva);
                    continue;
                }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 180, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/dsp_inbound_{xrefs.tsv,callers_decomp.txt,dump.txt}");
    }
}
