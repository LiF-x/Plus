// Resolve ServerUUIDEvent::vftable by symbol and walk the slots. Companion
// to LifxNetEventABI.java which missed it because the assignment in the
// ctor uses the named symbol, not a numeric literal. Issue #52.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxNetEventVtable extends GhidraScript {

    private static final String[] VTABLE_SYMBOL_NAMES = {
        "ServerUUIDEvent::vftable",
        "vftable",   // fallback: list all
    };
    private static final int MAX_SLOTS = 64;

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();
        MemoryBlock text = mem.getBlock(".text");
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Address vt = null;
        SymbolIterator it = st.getSymbolIterator("ServerUUIDEvent::vftable", true);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getName().equals("ServerUUIDEvent::vftable")) { vt = s.getAddress(); break; }
        }
        if (vt == null) {
            // try just "vftable" matches whose namespace mentions ServerUUID
            SymbolIterator it2 = st.getAllSymbols(true);
            while (it2.hasNext()) {
                Symbol s = it2.next();
                if (!"vftable".equals(s.getName())) continue;
                String ns = s.getParentNamespace().getName();
                if (ns.contains("ServerUUIDEvent")) { vt = s.getAddress(); break; }
            }
        }

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_vtable.txt"))))) {
            if (vt == null) {
                pw.println("ServerUUIDEvent::vftable symbol not found. Listing all 'vftable' symbols:");
                SymbolIterator it3 = st.getAllSymbols(true);
                int n = 0;
                while (it3.hasNext() && n < 200) {
                    Symbol s = it3.next();
                    if ("vftable".equals(s.getName())) {
                        pw.printf("  %s @ %s%n", s.getParentNamespace().getName(true), s.getAddress());
                        n++;
                    }
                }
                println("DONE (no symbol)");
                return;
            }

            pw.printf("=== ServerUUIDEvent::vftable @ %s ===%n", vt);
            Set<Function> slots = new LinkedHashSet<>();
            for (int i = 0; i < MAX_SLOTS; i++) {
                Address slotAddr = vt.add(i * 8L);
                long q;
                try { q = mem.getLong(slotAddr); } catch (Exception e) { break; }
                if (q == 0) break;
                Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                if (text != null && (tgt.compareTo(text.getStart()) < 0 || tgt.compareTo(text.getEnd()) > 0)) break;
                Function f = fm.getFunctionAt(tgt);
                if (f == null) break;
                long frva = tgt.getOffset() - base.getOffset();
                pw.printf("  slot %2d @ %s  -> 0x%X  RVA 0x%X  %s%n",
                    i, slotAddr, q, frva, f.getName());
                slots.add(f);
            }
            pw.println();
            for (Function f : slots) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("--- slot fn RVA 0x%X  %s ---%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 120, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/netevent_vtable.txt");
    }
}
