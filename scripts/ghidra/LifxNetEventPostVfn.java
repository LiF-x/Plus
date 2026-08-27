// Issue #56: decompile the post vfn captured by the runtime dumper
// (GameConnection-subobject vtable slot 0 = RVA 0x542CC0) plus every
// callee one hop deep. Goal: map the actual enqueue / serialize / dispatch
// path so we can find the receive entry on the other side.
//
// Also decompiles the other slots from the captured sink-vtable so we can
// label them (likely: post, process, pack, unpack, getType, getDebugName,
// destroy, …).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;

public class LifxNetEventPostVfn extends GhidraScript {

    // Slots from logs/netclassrep_dump.log "[netevent-sink] sink-vtable @ 0x140783118"
    private static final long[] SINK_SLOT_RVAS = {
        0x542CC0L, // slot 0 — the post vfn
        0x10D360L, // slot 2
        0x41E280L, // slot 3
        0x138800L, // slot 4
        // slot 5 is _guard_check_icall (0x85F40) — skip
        0x41DFF0L, // slot 6 — known: describeSelf
        0x138950L, // slot 7
        0x4168F0L, // slot 8
        0x137740L, // slot 9
        0x1381E0L, // slot 10
        0x1378D0L, // slot 11
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        // Step 1: decompile every slot fn.
        Set<Function> slotFns = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_postvfn_slots.txt"))))) {
            for (long rva : SINK_SLOT_RVAS) {
                Function f = fm.getFunctionAt(base.add(rva));
                if (f == null) { pw.printf("(no fn at RVA 0x%X)%n%n", rva); continue; }
                slotFns.add(f);
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        // Step 2: walk the post vfn's direct callees one hop deep.
        Function post = fm.getFunctionAt(base.add(0x542CC0L));
        Set<Function> callees = new LinkedHashSet<>();
        if (post != null) {
            DecompileResults r = dec.decompileFunction(post, 240, monitor);
            HighFunction hf = r.getHighFunction();
            if (hf != null) {
                Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                while (ops.hasNext()) {
                    PcodeOpAST op = ops.next();
                    if (op.getOpcode() != PcodeOp.CALL && op.getOpcode() != PcodeOp.CALLIND) continue;
                    Varnode tgt = op.getInput(0);
                    if (tgt == null || !tgt.isAddress()) continue;
                    Function cf = fm.getFunctionAt(tgt.getAddress());
                    if (cf != null && cf != post) callees.add(cf);
                }
            }
        }
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_postvfn_callees.txt"))))) {
            for (Function f : callees) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/netevent_postvfn_{slots,callees}.txt");
    }
}
