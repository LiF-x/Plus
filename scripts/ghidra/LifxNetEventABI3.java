// Issue #52, third pass. Close the two gaps from LifxNetEventABI2:
//   A. NetConnection vftable — try each candidate (1408BE630 / 908 / 940),
//      decompile slot at +0x1F8 from each, and pick the one whose body
//      looks like NetEvent post (handles a NetEvent*).
//   B. Pack/unpack discovery — at static-init time someone writes
//      function pointers into the ClassRep struct rooted at 0x140BC3860.
//      Walk writes (References of type DATA WRITE) into the 0x140BC3860..
//      0x140BC3960 range and decompile each writer.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxNetEventABI3 extends GhidraScript {

    private static final long[] NETCONN_VTABLE_CANDIDATES = {
        0x1408BE630L, 0x1408BE908L, 0x1408BE940L,
    };
    private static final long CLASSREP_LO = 0x140BC3860L;
    private static final long CLASSREP_HI = 0x140BC3960L;

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager refMgr = prog.getReferenceManager();
        Address base = prog.getImageBase();
        MemoryBlock text = mem.getBlock(".text");
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_abi3.txt"))))) {

            // A: vtable candidates — dump +0x1F8 from each.
            for (long vt : NETCONN_VTABLE_CANDIDATES) {
                pw.printf("=== candidate vtable @ 0x%X ===%n", vt);
                // dump slot 60..65 first for context
                for (int i = 60; i <= 65; i++) {
                    Address sa = prog.getAddressFactory().getAddress(Long.toHexString(vt + i * 8L));
                    long q;
                    try { q = mem.getLong(sa); } catch (Exception e) { pw.printf("  slot %d  (oob)%n", i); continue; }
                    Address ta = prog.getAddressFactory().getAddress(Long.toHexString(q));
                    Function f = (text != null && ta.compareTo(text.getStart()) >= 0 && ta.compareTo(text.getEnd()) <= 0) ? fm.getFunctionAt(ta) : null;
                    long fr = (f == null) ? -1 : ta.getOffset() - base.getOffset();
                    pw.printf("  slot %d (+0x%X) -> 0x%X  RVA 0x%X  %s%n",
                        i, i * 8, q, fr, f == null ? "(not text/no fn)" : f.getName());
                }
                // decompile slot 63 if it points at text
                Address s63 = prog.getAddressFactory().getAddress(Long.toHexString(vt + 0x1F8L));
                long q63;
                try { q63 = mem.getLong(s63); } catch (Exception e) { pw.println("  slot 63 OOB"); continue; }
                Address t63 = prog.getAddressFactory().getAddress(Long.toHexString(q63));
                Function f63 = (text != null && t63.compareTo(text.getStart()) >= 0 && t63.compareTo(text.getEnd()) <= 0) ? fm.getFunctionAt(t63) : null;
                if (f63 != null) {
                    long fr = t63.getOffset() - base.getOffset();
                    pw.printf("--- slot 63 fn  RVA 0x%X  %s ---%n", fr, f63.getName());
                    DecompileResults r = dec.decompileFunction(f63, 240, monitor);
                    pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                }
                pw.println();
            }

            // B: writes into the ClassRep range.
            pw.printf("=== writes into ClassRep range 0x%X..0x%X ===%n", CLASSREP_LO, CLASSREP_HI);
            Set<Function> writers = new LinkedHashSet<>();
            for (long va = CLASSREP_LO; va < CLASSREP_HI; va += 8L) {
                Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                ReferenceIterator ri = refMgr.getReferencesTo(a);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    if (!r.getReferenceType().isWrite()) continue;
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f != null) writers.add(f);
                    pw.printf("  +0x%02X write from %s  %s  (%s)%n",
                        va - CLASSREP_LO, r.getFromAddress(),
                        f == null ? "(no fn)" : f.getName(),
                        r.getReferenceType());
                }
            }
            pw.println();
            for (Function f : writers) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("--- writer RVA 0x%X  %s ---%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/netevent_abi3.txt");
    }
}
