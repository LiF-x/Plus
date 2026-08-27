// Issue #52, follow-up pass. Targets the gaps left by LifxNetEventABI +
// LifxNetEventVtable:
//   - Decompile FUN_140418c40 (NetClassRep::add candidate — only fn called
//     after a ConcreteClassRep<T> is filled out in static init).
//   - Decompile FUN_140404B60 (the TS console-cmd registrar used by the
//     SendServerUUIDEvent slot at 0x6F6F0).
//   - Locate NetConnection::vftable by symbol and dump slot indexes up to
//     0x1F8/8 = 63 so we can name the post vfn.
//   - Decompile slot 63 (== +0x1F8) on NetConnection's vftable directly.
//   - Dump the .data words around DAT_140bc3860 (the "static classRep
//     instance for ServerUUIDEvent" returned by slot 2 of its vtable).
//
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxNetEventABI2 extends GhidraScript {

    private static final long[] FN_RVAS = {
        0x418C40L, // candidate NetClassRep::add
        0x404B60L, // SendServerUUIDEvent TS console-cmd registrar
        0x41E790L, // ServerUUIDEvent vtable slot 2 (returns &DAT_140bc3860)
    };

    private static final long CLASSREP_DATA_VA = 0x140bc3860L;
    private static final int  CLASSREP_DUMP_QWORDS = 32;

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

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_abi2.txt"))))) {

            // 1+2+3: fn decomps
            for (long rva : FN_RVAS) {
                Function f = fm.getFunctionAt(base.add(rva));
                if (f == null) { pw.printf("(no fn at 0x%X)%n%n", rva); continue; }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }

            // 4: NetConnection::vftable resolution + slot +0x1F8 (= index 63).
            Address ncVt = null;
            SymbolIterator it = st.getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (!"vftable".equals(s.getName())) continue;
                String parent = s.getParentNamespace().getName(true);
                if (parent.contains("NetConnection") && !parent.contains("Game")) {
                    pw.printf("--- candidate %s @ %s ---%n", parent, s.getAddress());
                    if (ncVt == null) ncVt = s.getAddress();
                }
            }
            // Also list GameConnection vftable since that's the concrete type
            // used on the server.
            SymbolIterator it2 = st.getAllSymbols(true);
            while (it2.hasNext()) {
                Symbol s = it2.next();
                if (!"vftable".equals(s.getName())) continue;
                String parent = s.getParentNamespace().getName(true);
                if (parent.contains("GameConnection") || parent.contains("cmGameConnection")) {
                    pw.printf("--- GameConn candidate %s @ %s ---%n", parent, s.getAddress());
                }
            }

            if (ncVt != null) {
                Address slot = ncVt.add(0x1F8L);
                long q = mem.getLong(slot);
                Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                pw.printf("%n=== NetConnection vtable +0x1F8 (slot 63) @ %s -> 0x%X ===%n", slot, q);
                Function f = fm.getFunctionAt(tgt);
                if (f != null) {
                    long frva = tgt.getOffset() - base.getOffset();
                    pw.printf("--- RVA 0x%X  %s ---%n", frva, f.getName());
                    DecompileResults r = dec.decompileFunction(f, 240, monitor);
                    pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                }
                // Also dump the previous + next 4 slots in case 0x1F8 is the
                // wrong offset on this build's class hierarchy.
                pw.printf("%n=== NetConnection vtable nearby slots ===%n");
                for (int i = -4; i <= 8; i++) {
                    Address sa = ncVt.add(0x1F8L + i * 8L);
                    long qq = mem.getLong(sa);
                    Address ta = prog.getAddressFactory().getAddress(Long.toHexString(qq));
                    Function ff = fm.getFunctionAt(ta);
                    long fr = (ff == null) ? -1 : ta.getOffset() - base.getOffset();
                    pw.printf("  +0x%X (slot %d) -> 0x%X  RVA 0x%X  %s%n",
                        0x1F8 + i * 8, 63 + i, qq, fr, ff == null ? "(no fn)" : ff.getName());
                }
            } else {
                pw.println("(no NetConnection::vftable symbol found)");
            }

            // 5: dump qwords around DAT_140bc3860 — the global ClassRep
            //    instance for ServerUUIDEvent. Each qword that lands in
            //    .text is a candidate fn pointer (factory / pack / unpack).
            pw.printf("%n=== ClassRep dump @ 0x%X (%d qwords) ===%n", CLASSREP_DATA_VA, CLASSREP_DUMP_QWORDS);
            for (int i = 0; i < CLASSREP_DUMP_QWORDS; i++) {
                long va = CLASSREP_DATA_VA + i * 8L;
                Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                long q;
                try { q = mem.getLong(a); } catch (Exception e) { pw.printf("  +0x%X (oob)%n", i*8); continue; }
                Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                Function f = (text != null && tgt.compareTo(text.getStart()) >= 0 && tgt.compareTo(text.getEnd()) <= 0)
                    ? fm.getFunctionAt(tgt) : null;
                long frva = (f == null) ? -1 : tgt.getOffset() - base.getOffset();
                pw.printf("  +0x%02X @ 0x%X  -> 0x%X  RVA 0x%X  %s%n",
                    i*8, va, q, frva, f == null ? "" : f.getName());
            }
        }

        println("DONE. /tmp/lifx_ghidra/netevent_abi2.txt");
    }
}
