// Dump vtables surfaced by the parseDispatcherReply caller scan, and any
// associated NetEvent class-registration tables. Goal: lock down the wire
// format of the dispatcher protocol (DispatcherEvent's pack/unpack vfns and
// the full method roster on cmUnitManager).
//
// Strategy: for each vtable anchor we walk forward as long as the qwords
// resolve to functions in .text, list each slot, then decompile each unique
// function. Slot identity is left to a human (Ghidra hasn't named these).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class LifxDspVtableDump extends GhidraScript {

    // Each anchor is a vtable slot we already know contains a function we care
    // about. We walk slots both before (up to MAX_BACK) and after (until a
    // qword stops resolving to .text) so we capture the whole vtable.
    private static final long[] VTABLE_ANCHORS = {
        0x140c355c8L, // cmUnitManager::parseDispatcherReply slot
        0x140c0de78L, // DispatcherEvent / SendServerUUIDEvent vtable slot (one of two)
        0x140c0de84L, // DispatcherEvent / SendServerUUIDEvent vtable slot (the other)
        0x140733930L, // NetClassRep / class-registry table (one)
        0x140733938L, // NetClassRep / class-registry table (two)
    };

    private static final int MAX_BACK_SLOTS = 16;   // walk this many qwords before the anchor
    private static final int MAX_FORWARD_SLOTS = 64; // …and this many after, until a non-text qword

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        Memory mem = prog.getMemory();
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();

        // .text bounds — accept fn ptrs only if they land inside.
        MemoryBlock text = mem.getBlock(".text");
        Address textStart = text.getStart();
        Address textEnd = text.getEnd();

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Set<Function> toDecompile = new LinkedHashSet<>();

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_vtables.txt"))))) {

            for (long anchorVA : VTABLE_ANCHORS) {
                Address anchor = prog.getAddressFactory().getAddress(Long.toHexString(anchorVA));
                pw.printf("=========== Vtable anchor @ 0x%X ===========%n", anchorVA);

                // Walk backwards while qwords are valid function pointers.
                long startVA = anchorVA;
                for (int i = 1; i <= MAX_BACK_SLOTS; i++) {
                    long va = anchorVA - i * 8L;
                    Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                    try {
                        long q = mem.getLong(a);
                        Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                        if (q == 0 || tgt.compareTo(textStart) < 0 || tgt.compareTo(textEnd) > 0) {
                            break;
                        }
                        Function f = fm.getFunctionAt(tgt);
                        if (f == null) {
                            // Not a function entry point — likely past vtable start.
                            break;
                        }
                        startVA = va;
                    } catch (Exception e) {
                        break;
                    }
                }

                // Walk forwards from startVA until qwords stop resolving.
                long va = startVA;
                int slot = 0;
                while (slot < MAX_FORWARD_SLOTS) {
                    Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                    long q;
                    try {
                        q = mem.getLong(a);
                    } catch (Exception e) {
                        break;
                    }
                    if (q == 0) break;
                    Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                    if (tgt.compareTo(textStart) < 0 || tgt.compareTo(textEnd) > 0) break;
                    Function f = fm.getFunctionAt(tgt);
                    if (f == null) break;

                    long frva = tgt.getOffset() - base.getOffset();
                    String marker = (va == anchorVA) ? "  <-- anchor" : "";
                    pw.printf("  slot %2d @ 0x%X  ->  0x%X  RVA 0x%X  %s%s%n",
                        slot, va, q, frva, f.getName(), marker);
                    toDecompile.add(f);
                    va += 8;
                    slot++;
                }
                pw.println();
            }
        }
        println("Unique functions across all walked vtables: " + toDecompile.size());

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_vtables_decomp.txt"))))) {
            for (Function f : toDecompile) {
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
        println("DONE. /tmp/lifx_ghidra/dsp_vtables{,_decomp}.txt");
    }
}
