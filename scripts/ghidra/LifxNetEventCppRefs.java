// Find every function in the binary that references the source path
// "engine\source\sim\netevent.cpp" — those are the NetEvent send/receive
// machinery (postNetEvent we already have; pack/unpack/process should
// surface here too). Issue #56.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxNetEventCppRefs extends GhidraScript {

    // Substrings of the path we're chasing. The exact form in the binary
    // (per the FUN_140542CC0 decompile) was `x:\dev\cm_clone\cm_yo_release\
    // engine\source\sim\netevent.cpp`. Match by tail to avoid pattern brittleness.
    private static final String[] PATH_TAILS = {
        "netevent.cpp",
        "eventconnection.cpp",
        "netconnection.cpp",
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager refMgr = prog.getReferenceManager();
        Address base = prog.getImageBase();
        MemoryBlock rdata = mem.getBlock(".rdata");
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        // Step 1: scan .rdata for matching strings.
        List<Address> stringAddrs = new ArrayList<>();
        if (rdata != null) {
            byte[] buf = new byte[(int)rdata.getSize()];
            mem.getBytes(rdata.getStart(), buf);
            // Walk null-terminated C strings; collect any whose tail
            // matches one of PATH_TAILS (case-insensitive — the binary's
            // source paths mix cases).
            for (int i = 0; i < buf.length - 32; ) {
                if (buf[i] < 0x20 || buf[i] > 0x7E) { i++; continue; }
                int end = i;
                while (end < buf.length && end - i < 512 && buf[end] >= 0x20 && buf[end] <= 0x7E) end++;
                if (end >= buf.length || buf[end] != 0 || end - i < 8) { i = end + 1; continue; }
                String s = new String(buf, i, end - i).toLowerCase();
                boolean hit = false;
                for (String tail : PATH_TAILS) if (s.endsWith(tail)) { hit = true; break; }
                if (hit) {
                    Address a = rdata.getStart().add(i);
                    stringAddrs.add(a);
                    println(String.format("string @ %s  =  %s", a, new String(buf, i, end - i)));
                }
                i = end + 1;
            }
        }
        println("Found " + stringAddrs.size() + " matching strings");

        // Step 2: every function that references any of those strings.
        Set<Function> hitFns = new LinkedHashSet<>();
        for (Address sa : stringAddrs) {
            ReferenceIterator ri = refMgr.getReferencesTo(sa);
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) hitFns.add(f);
            }
        }
        println("Found " + hitFns.size() + " functions referencing those strings");

        // Step 3: tabulate (RVA, function name, first reachable string).
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_cpp_refs.txt"))))) {
            pw.println("rva\tname");
            for (Function f : hitFns) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("0x%X\t%s%n", frva, f.getName());
            }
            pw.println();
            pw.println("=== decompiles ===");
            for (Function f : hitFns) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/netevent_cpp_refs.txt");
    }
}
