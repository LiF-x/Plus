// P0 wire-format RE for the dispatcher protocol (issue #48 / spec #47).
//
// Decompiles the three known entry points (ServerUUIDEvent::create factory,
// SendServerUUIDEvent script entry, cmUnitManager::parseDispatcherReply) and
// every direct callee from inside parseDispatcherReply's switch — those are
// the per-opcode payload helpers. Also dumps the NetClassRep registration
// table at 0x1407338B0 so we can confirm group/classNum and pack/unpack slot
// offsets.
//
// Output: /tmp/lifx_ghidra/dsp_wire_{entrypoints,opcode_helpers,classreg}.txt
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxDspWireFormat extends GhidraScript {

    private static final long SERVER_UUID_EVENT_CREATE_RVA = 0x4E7260L;
    private static final long SEND_SERVER_UUID_EVENT_RVA   = 0x4E7760L;
    private static final long PARSE_DSP_REPLY_RVA          = 0x3CC860L;

    // NetClassRep static-init table observed during the dispatcher vtable
    // walk. Dump a generous run so we capture surrounding NetEvent
    // registrations (group=2 / classNum=2 = ServerUUIDEvent).
    private static final long CLASSREG_TABLE_VA   = 0x1407338B0L;
    private static final int  CLASSREG_SLOTS      = 96;

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

        // 1) Entry points — factory + script binding + reply parser.
        Function create   = fm.getFunctionAt(base.add(SERVER_UUID_EVENT_CREATE_RVA));
        Function sendEvt  = fm.getFunctionAt(base.add(SEND_SERVER_UUID_EVENT_RVA));
        Function parseRpl = fm.getFunctionAt(base.add(PARSE_DSP_REPLY_RVA));

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_wire_entrypoints.txt"))))) {
            for (Function f : new Function[]{create, sendEvt, parseRpl}) {
                if (f == null) { pw.println("(missing entry fn)"); continue; }
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        // 2) Per-opcode helpers — every direct callee of parseDispatcherReply
        //    is a candidate payload reader. Walk pcode of parseRpl, collect
        //    CALL targets, decompile each.
        Set<Function> callees = new LinkedHashSet<>();
        if (parseRpl != null) {
            DecompileResults r = dec.decompileFunction(parseRpl, 240, monitor);
            HighFunction hf = r.getHighFunction();
            if (hf != null) {
                Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                while (ops.hasNext()) {
                    PcodeOpAST op = ops.next();
                    if (op.getOpcode() != PcodeOp.CALL && op.getOpcode() != PcodeOp.CALLIND) continue;
                    Varnode target = op.getInput(0);
                    if (target == null || !target.isAddress()) continue;
                    Address tgt = target.getAddress();
                    if (tgt == null) continue;
                    if (text != null && (tgt.compareTo(text.getStart()) < 0 || tgt.compareTo(text.getEnd()) > 0)) continue;
                    Function f = fm.getFunctionAt(tgt);
                    if (f != null) callees.add(f);
                }
            }
        }
        println("parseDispatcherReply direct callees: " + callees.size());

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_wire_opcode_helpers.txt"))))) {
            for (Function f : callees) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        // 3) NetClassRep registration table — dump CLASSREG_SLOTS qwords from
        //    the anchor. Each NetClassRep entry is a global ctor pointer;
        //    resolving each lets us read off group/classNum/event-name.
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_wire_classreg.txt"))))) {
            pw.printf("=== ClassReg table @ 0x%X (%d slots) ===%n", CLASSREG_TABLE_VA, CLASSREG_SLOTS);
            Set<Function> ctors = new LinkedHashSet<>();
            for (int i = 0; i < CLASSREG_SLOTS; i++) {
                long va = CLASSREG_TABLE_VA + i * 8L;
                Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                long q;
                try { q = mem.getLong(a); } catch (Exception e) { pw.printf("  slot %2d @ 0x%X  (oob)%n", i, va); continue; }
                if (q == 0) { pw.printf("  slot %2d @ 0x%X  -> 0%n", i, va); continue; }
                Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                Function f = fm.getFunctionAt(tgt);
                long frva = tgt.getOffset() - base.getOffset();
                pw.printf("  slot %2d @ 0x%X  -> 0x%X  RVA 0x%X  %s%n",
                    i, va, q, frva, f == null ? "(no fn)" : f.getName());
                if (f != null) ctors.add(f);
            }
            pw.println();
            for (Function f : ctors) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("--- RVA 0x%X  %s ---%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        println("DONE. /tmp/lifx_ghidra/dsp_wire_{entrypoints,opcode_helpers,classreg}.txt");
    }
}
