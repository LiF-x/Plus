// For each of the 7 broadcaster-pointer slots in .rdata, walk backward
// to find the nearest preceding `vftable` symbol — that names the class
// whose vtable contains FUN_1400ebff0 (the effect broadcaster).
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.*;
import java.io.*;

public class LifxIdentifyVtableSlot extends GhidraScript {
    private static final long[] SLOTS = {
        0x75CBE8L, 0x799078L, 0x7E2270L, 0x7E2EB0L,
        0x7E42E0L, 0x7E51A8L, 0x7E5C10L,
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "broadcaster_vtable_owners.tsv"))))) {
            pw.println("slot_rva\tvft_rva\tslot_index\tclass\tnamespace_chain");
            for (long rva : SLOTS) {
                Address a = base.add(rva);
                // Walk backward by 8 bytes at a time, scanning for any symbol;
                // pick the first whose name == "vftable" / `vftable`.
                Address probe = a;
                Symbol vft = null;
                long vftRva = -1;
                for (int i = 0; i < 4096; i++) {
                    Symbol[] syms = st.getSymbols(probe);
                    for (Symbol s : syms) {
                        String n = s.getName();
                        if ("vftable".equals(n) || "`vftable'".equals(n) || n.endsWith("vftable")) {
                            vft = s;
                            vftRva = probe.getOffset() - base.getOffset();
                            break;
                        }
                    }
                    if (vft != null) break;
                    try { probe = probe.subtract(8); } catch (Exception ex) { break; }
                }
                if (vft == null) {
                    pw.printf("0x%X\t?\t?\t?\t?%n", rva);
                    continue;
                }
                long slotIdx = (rva - vftRva) / 8;
                Namespace ns = vft.getParentNamespace();
                String cls = (ns == null) ? "?" : ns.getName();
                // Build the full namespace chain
                StringBuilder chain = new StringBuilder();
                Namespace n = ns;
                while (n != null) {
                    if (chain.length() > 0) chain.insert(0, "::");
                    chain.insert(0, n.getName());
                    n = n.getParentNamespace();
                    if (n != null && "Global".equals(n.getName())) break;
                }
                pw.printf("0x%X\t0x%X\t%d\t%s\t%s%n",
                          rva, vftRva, slotIdx, cls, chain.toString());
                println(String.format("slot 0x%X -> %s (idx %d, vft 0x%X)",
                                       rva, chain, slotIdx, vftRva));
            }
        }
        println("DONE. /tmp/lifx_ghidra/broadcaster_vtable_owners.tsv");
    }
}
