// Locate every function that references the dispatcher / multi-world subsystem
// strings in ddctd_cm_yo_server.exe and decompile each one. Used as the
// starting point for federating multiple world-server instances behind a
// dispatcher (see issue #45).
//
// We pick strings rather than addresses so the script survives game updates.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxDispatcherScan extends GhidraScript {

    // Exact-match string targets observed in the binary.
    private static final String[] EXACT_TARGETS = {
        "joinToServer",
        "ServersGroup",
        "SendServerUUIDEvent",
        "CanBypassLimitedModeDispatcher",
        "cmUnitManager::parseDispatcherReply(%u, %u, %u) -- bad EventType!",
        "x:\\dev\\cm_clone\\cm_yo_release\\engine\\source\\sim\\dispatcherevents.cpp",
    };

    // Substring targets (case-insensitive). Use sparingly — broad strings can
    // pull in many unrelated callers.
    private static final String[] SUBSTRING_TARGETS = {
        "Dispatcher",
        "RelayServer",
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();
        ReferenceManager refMgr = prog.getReferenceManager();
        Address base = prog.getImageBase();

        // Resolve string definitions.
        Map<String, List<Address>> hits = new LinkedHashMap<>();
        for (String t : EXACT_TARGETS) hits.put("=" + t, new ArrayList<>());
        for (String t : SUBSTRING_TARGETS) hits.put("~" + t, new ArrayList<>());

        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            if (!d.hasStringValue()) continue;
            StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
            String v = sdi.getStringValue();
            if (v == null) continue;

            for (String t : EXACT_TARGETS) {
                if (t.equals(v)) hits.get("=" + t).add(d.getAddress());
            }
            String vLower = v.toLowerCase();
            for (String t : SUBSTRING_TARGETS) {
                if (vLower.contains(t.toLowerCase()) && !v.contains("QAbstractEventDispatcher")
                        && !v.contains("QtEventDispatcher") && !v.contains("btDispatcher")
                        && !v.contains("btCollisionDispatcher")) {
                    hits.get("~" + t).add(d.getAddress());
                }
            }
        }

        for (Map.Entry<String, List<Address>> e : hits.entrySet()) {
            println(e.getKey() + " : " + e.getValue().size() + " definitions");
        }

        // Collect unique caller functions across all matching strings.
        Set<Function> callers = new LinkedHashSet<>();
        try (PrintWriter pwList = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dispatcher_xrefs.tsv"))))) {
            pwList.println("match\tstring_addr\tfrom_func_rva\tfrom_func\tfrom_addr");
            for (Map.Entry<String, List<Address>> e : hits.entrySet()) {
                for (Address sa : e.getValue()) {
                    ReferenceIterator ri = refMgr.getReferencesTo(sa);
                    while (ri.hasNext()) {
                        Reference r = ri.next();
                        Function f = fm.getFunctionContaining(r.getFromAddress());
                        if (f == null) continue;
                        long frva = f.getEntryPoint().getOffset() - base.getOffset();
                        pwList.printf("%s\t%s\t0x%X\t%s\t%s%n",
                            e.getKey(), sa, frva, f.getName(), r.getFromAddress());
                        callers.add(f);
                    }
                }
            }
        }
        println("Unique caller functions: " + callers.size());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dispatcher_callers_decomp.txt"))))) {
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
        println("DONE. /tmp/lifx_ghidra/dispatcher_{xrefs.tsv,callers_decomp.txt}");
    }
}
