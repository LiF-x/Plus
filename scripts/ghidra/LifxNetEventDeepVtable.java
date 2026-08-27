// Re-walk ServerUUIDEvent::vftable (0x1408A03E8) past slot 7 — the earlier
// walker terminated on the first non-function qword, which may have masked
// real virtuals like pack/unpack. Also dump the struct at ClassRep+0x30 =
// 0x140BE5760, which is a per-class .data block that's a strong candidate
// for the function-pointer table.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class LifxNetEventDeepVtable extends GhidraScript {

    private static final long SERVUUID_VTABLE = 0x1408A03E8L;
    private static final long CLASSREP_PLUS30 = 0x140BE5760L; // ServerUUIDEvent ClassRep +0x30
    private static final int  SLOTS = 32;

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

        Set<Function> toDecomp = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_deep_vtable.txt"))))) {

            for (long anchor : new long[]{ SERVUUID_VTABLE, CLASSREP_PLUS30 }) {
                pw.printf("=== anchor @ 0x%X ===%n", anchor);
                for (int s = 0; s < SLOTS; s++) {
                    long va = anchor + s * 8L;
                    Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                    long q;
                    try { q = mem.getLong(a); } catch (Exception e) { pw.printf("  slot %2d (oob)%n", s); break; }
                    Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                    boolean inText = text != null && tgt.compareTo(text.getStart()) >= 0 && tgt.compareTo(text.getEnd()) <= 0;
                    Function f = inText ? fm.getFunctionAt(tgt) : null;
                    long frva = (f != null) ? tgt.getOffset() - base.getOffset() : -1;
                    boolean inImage = q >= 0x140000000L && q < 0x141000000L;
                    pw.printf("  slot %2d  +0x%02X  0x%016X  RVA 0x%X  %s%s%n",
                        s, s * 8, q, frva,
                        f == null ? (inImage ? "(image, non-fn)" : "(non-image)") : f.getName(),
                        inText && f != null ? "  [TEXT]" : "");
                    if (f != null) toDecomp.add(f);
                }
                pw.println();
            }

            // Decompile every newly-discovered fn (skip the ones from
            // the first 7 slots we already documented in PR #53).
            pw.println("=== decompiles ===");
            for (Function f : toDecomp) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                if (frva == 0x86A30 || frva == 0x85F40 || frva == 0x41E790
                 || frva == 0x41DFF0 || frva == 0x86850) continue; // already known
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/netevent_deep_vtable.txt");
    }
}
