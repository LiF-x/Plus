// Find every reference (data or code) to address 0x1400EBFF0 — the effect
// delta broadcaster. Since it has no direct call xrefs, it must be stored
// as a function pointer somewhere (vtable slot or callback table).
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxFindBroadcasterRefs extends GhidraScript {
    private static final long BROADCASTER_RVA = 0xEBFF0L;

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();

        Address target = base.add(BROADCASTER_RVA);
        long targetVA = target.getOffset();
        println("Hunting for stored ptr to " + target);

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "broadcaster_data_refs.tsv"))))) {
            pw.println("addr_va\taddr_rva\tblock\tcontaining_sym\tcontaining_ns");

            // Scan every initialized data block for a qword equal to targetVA.
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized()) continue;
                // skip executable code blocks
                if (blk.isExecute()) continue;
                Address a = blk.getStart();
                Address end = blk.getEnd();
                int hits = 0;
                while (a.compareTo(end) <= 0) {
                    long v;
                    try { v = mem.getLong(a); }
                    catch (Exception ex) { break; }
                    if (v == targetVA) {
                        long rva = a.getOffset() - base.getOffset();
                        // Find nearest preceding symbol to get context
                        String symInfo = "";
                        Symbol[] syms = st.getSymbols(a);
                        if (syms.length > 0) {
                            symInfo = syms[0].getName();
                            Namespace ns = syms[0].getParentNamespace();
                            if (ns != null && !"Global".equals(ns.getName())) {
                                symInfo += " (ns=" + ns.getName() + ")";
                            }
                        } else {
                            // walk backward to find the nearest symbol
                            Symbol before = null;
                            Address probe = a;
                            for (int i = 0; i < 64 && probe != null; i++) {
                                probe = probe.subtract(8);
                                Symbol[] s2 = st.getSymbols(probe);
                                if (s2.length > 0) { before = s2[0]; break; }
                            }
                            if (before != null) {
                                symInfo = "after:" + before.getName();
                                Namespace ns = before.getParentNamespace();
                                if (ns != null && !"Global".equals(ns.getName())) {
                                    symInfo += " (ns=" + ns.getName() + ")";
                                }
                            }
                        }
                        pw.printf("0x%X\t0x%X\t%s\t%s%n",
                                  a.getOffset(), rva, blk.getName(), symInfo);
                        hits++;
                    }
                    try { a = a.add(8); } catch (Exception ex) { break; }
                }
                if (hits > 0) println("  " + blk.getName() + ": " + hits + " hits");
            }
        }
        println("DONE. /tmp/lifx_ghidra/broadcaster_data_refs.tsv");
    }
}
