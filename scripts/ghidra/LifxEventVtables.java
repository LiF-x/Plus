// Dump vtables of every *Event class. For each entry, output the target function.
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxEventVtables extends GhidraScript {

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Memory mem = prog.getMemory();
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();

        // First pass: find all vftable symbols whose parent namespace ends in "Event"
        Map<String, Address> classVft = new TreeMap<>();
        SymbolIterator si = st.getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String name = s.getName();
            if (!"vftable".equals(name) && !"`vftable'".equals(name) && !name.endsWith("vftable")) continue;
            Namespace ns = s.getParentNamespace();
            if (ns == null) continue;
            String cn = ns.getName();
            if (!cn.endsWith("Event")) continue;
            // first vftable wins (primary)
            classVft.putIfAbsent(cn, s.getAddress());
        }
        println("Found " + classVft.size() + " Event-class vtables");

        // Output file
        try (PrintWriter pw = new PrintWriter(new File(outDir, "event_vtables.tsv"))) {
            pw.println("class\tslot\tvft_rva\tfn_rva\tfn_name");
            int total = 0;
            for (Map.Entry<String, Address> e : classVft.entrySet()) {
                String cn = e.getKey();
                Address vftAddr = e.getValue();
                long vftRva = vftAddr.getOffset() - base.getOffset();
                int slot = 0;
                Address cur = vftAddr;
                while (slot < 64) {  // safety cap
                    long ptr;
                    try {
                        ptr = mem.getLong(cur);
                    } catch (Exception ex) { break; }
                    if (ptr == 0L) break;
                    Address target;
                    try { target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ptr); }
                    catch (Exception ex) { break; }
                    Function fn = fm.getFunctionAt(target);
                    if (fn == null) {
                        // not a function entry — vtable ends
                        break;
                    }
                    long fnRva = target.getOffset() - base.getOffset();
                    pw.printf("%s\t%d\t0x%X\t0x%X\t%s%n", cn, slot, vftRva, fnRva, fn.getName());
                    total++;
                    slot++;
                    cur = cur.add(8);
                }
            }
            println("Wrote " + total + " vtable slots across all Event classes");
        }

        // Also produce a compact-per-class summary
        try (PrintWriter pw = new PrintWriter(new File(outDir, "event_classes.tsv"))) {
            pw.println("class\tvft_rva\tnum_virtuals");
            for (Map.Entry<String, Address> e : classVft.entrySet()) {
                String cn = e.getKey();
                Address vftAddr = e.getValue();
                long vftRva = vftAddr.getOffset() - base.getOffset();
                int slot = 0;
                Address cur = vftAddr;
                while (slot < 64) {
                    long ptr;
                    try { ptr = mem.getLong(cur); } catch (Exception ex) { break; }
                    if (ptr == 0L) break;
                    Address t;
                    try { t = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ptr); }
                    catch (Exception ex) { break; }
                    if (fm.getFunctionAt(t) == null) break;
                    slot++;
                    cur = cur.add(8);
                }
                pw.printf("%s\t0x%X\t%d%n", cn, vftRva, slot);
            }
        }
        println("DONE");
    }
}
