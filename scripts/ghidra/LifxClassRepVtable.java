// Walks ConcreteClassRep<T>::vftable for ServerUUIDEvent (identified at
// runtime by the dumper in PR #55 / issue #54) plus a few control vtables
// from other classRep instances. The slots that DIFFER across classes are
// the per-class pack/unpack/factory/create overrides; consistent slots are
// inherited ConsoleObject/ConcreteClassRepBase virtuals.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class LifxClassRepVtable extends GhidraScript {

    // Each label is "RepName  vtableVA"  — values come from
    // logs/netclassrep_dump.log via grep "+0x00".
    private static final String[][] TARGETS = {
        { "ServerUUIDEvent (rep#31, size 0x48)", "1408A1C08" },
        { "rep#1  (size 0x138)",                 "1408E7468" },
        { "rep#2  (size 0x108)",                 "1408E7180" },
        { "rep#5  (size 0x68)",                  "1408E4780" },
        { "rep#22 (size 0x48 — candidate)",      "" }, // patched in below if discoverable
    };

    private static final int SLOTS = 24;

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        MemoryBlock text = mem.getBlock(".text");
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        // Per-vtable slot RVAs, indexed [vtableIdx][slot]
        long[][] grid = new long[TARGETS.length][SLOTS];
        Set<Function> toDecomp = new LinkedHashSet<>();

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "classrep_vtables.txt"))))) {
            for (int i = 0; i < TARGETS.length; i++) {
                String name = TARGETS[i][0];
                String va = TARGETS[i][1];
                if (va.isEmpty()) continue;
                long vt = Long.parseLong(va, 16);
                pw.printf("=== %s @ 0x%X ===%n", name, vt);
                for (int s = 0; s < SLOTS; s++) {
                    long sa = vt + s * 8L;
                    Address a = prog.getAddressFactory().getAddress(Long.toHexString(sa));
                    long q;
                    try { q = mem.getLong(a); } catch (Exception e) { pw.printf("  slot %2d (oob)%n", s); break; }
                    if (q == 0) { pw.printf("  slot %2d -> 0%n", s); break; }
                    Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                    boolean inText = text != null && tgt.compareTo(text.getStart()) >= 0 && tgt.compareTo(text.getEnd()) <= 0;
                    Function f = inText ? fm.getFunctionAt(tgt) : null;
                    long frva = (f != null) ? tgt.getOffset() - base.getOffset() : -1;
                    grid[i][s] = frva;
                    pw.printf("  slot %2d -> 0x%X  RVA 0x%X  %s%n",
                        s, q, frva, f == null ? "" : f.getName());
                    if (f != null) toDecomp.add(f);
                }
                pw.println();
            }

            // Diff table — which slots vary across vtables?
            pw.println("=== per-slot consistency (slots where ALL non-empty rows agree) ===");
            for (int s = 0; s < SLOTS; s++) {
                long ref = 0; boolean any = false; boolean diff = false;
                for (int i = 0; i < TARGETS.length; i++) {
                    if (TARGETS[i][1].isEmpty()) continue;
                    long v = grid[i][s];
                    if (!any) { ref = v; any = true; continue; }
                    if (v != ref) { diff = true; break; }
                }
                if (!any) continue;
                pw.printf("  slot %2d : %s%n", s, diff ? "DIFFERS (candidate per-class override)" : "consistent");
            }
        }

        // Decompile only ServerUUIDEvent's per-class slots — slots where its
        // RVA differs from rep#1's. That isolates pack/unpack/factory.
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "classrep_servuuid_overrides.txt"))))) {
            for (int s = 0; s < SLOTS; s++) {
                if (grid[0][s] <= 0 || grid[0][s] == grid[1][s]) continue;
                long frva = grid[0][s];
                Function f = fm.getFunctionAt(base.add(frva));
                if (f == null) continue;
                pw.printf("=== slot %d  RVA 0x%X  %s ===%n", s, frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/classrep_{vtables,servuuid_overrides}.txt");
    }
}
