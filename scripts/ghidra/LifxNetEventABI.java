// Issue #52: lock down three calling conventions so the SectorHandoff
// NetEvent's pack/unpack thunks have a real target.
//
//   1. ServerUUIDEvent vtable — find the pack/unpack slot indexes.
//   2. NetConnection::post — the vfn at conn+0x1F8.
//   3. NetClassRep::add — the registration entry distinct from
//      EngineFunctionInfo::add (0x41DF20).
//
// Strategy:
//   - Decompile the ServerUUIDEvent ctor (FUN_1404E60D0). That ctor writes
//     the vftable pointer into *evt; pull the address out.
//   - Walk the vftable forward and decompile every slot — eyeball which
//     ones touch a BitStream (write/writeFlag/writeInt helpers).
//   - For NetConnection::post: decompile the SendServerUUIDEvent script
//     entry's downstream callee FUN_1404E7370 plus a handful of known
//     NetConnection-vtable candidates we can detect by looking at .rdata
//     qword tables that contain ServerUUIDEvent's vftable's enclosing
//     class hierarchy. (For this first pass we just dump the call sites
//     of FUN_1404E7370 so a human can pick the concrete-typed callers.)
//   - For NetClassRep::add: decompile the two known SendServerUUIDEvent
//     class-registration slots (FUN_14006F750 + FUN_14006F6F0) and any
//     fn they share with the other NetEvent slots (e.g. YoPatchTerCached…
//     at FUN_14006FDD0 which we already know calls EngineFunctionInfo::add
//     — its sibling that builds the NetClassRep entry should appear
//     nearby).
//
// Output: /tmp/lifx_ghidra/netevent_abi_{ctor_and_vtable,post_callers,classreg}.txt
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxNetEventABI extends GhidraScript {

    private static final long SERVER_UUID_EVENT_CTOR_RVA = 0x4E60D0L;
    private static final long SEND_FACTORY_RVA           = 0x4E7370L;

    // Known NetEvent class-registration slots from the registry walk in
    // #49: SendServerUUIDEvent (two slots) + YoPatchTerCachedDataRequestEvent
    // (just for cross-reference — it calls EngineFunctionInfo::add, not
    // NetClassRep::add, so it acts as a control).
    private static final long[] CLASS_REG_SLOT_RVAS = {
        0x6F750L, // SendServerUUIDEvent slot (one)
        0x6F6F0L, // SendServerUUIDEvent slot (the other)
        0x6FDD0L, // YoPatchTerCachedDataRequestEvent — script binding ctrl
        0x6E420L, 0x6ECD0L, 0x6FBD0L, 0x6FC50L, // immediate neighbours in the registry table
        0x6FDD0L, 0x6FE50L, 0x6E320L, 0x6EAB0L,
    };

    private static final int VTABLE_MAX_SLOTS = 64;

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

        // 1) ctor decomp + extract vftable VA, then walk it.
        Function ctor = fm.getFunctionAt(base.add(SERVER_UUID_EVENT_CTOR_RVA));
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_abi_ctor_and_vtable.txt"))))) {
            if (ctor == null) {
                pw.println("(ctor not present at expected RVA — re-check 0x4E60D0)");
            } else {
                long crva = ctor.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== ctor RVA 0x%X  %s ===%n", crva, ctor.getName());
                DecompileResults rr = dec.decompileFunction(ctor, 240, monitor);
                String code = rr.decompileCompleted() ? rr.getDecompiledFunction().getC() : "(decompile failed)";
                pw.println(code);
                pw.println();

                // Find a vftable assignment in the ctor: pattern is
                // `*<var> = &<vftable_symbol>` or a literal `*<var> = 0x140xxx`.
                long vtVA = scanDecompForVftable(code, prog);
                pw.printf("=== detected vftable VA: 0x%X ===%n%n", vtVA);
                if (vtVA != 0) {
                    Set<Function> slots = new LinkedHashSet<>();
                    for (int i = 0; i < VTABLE_MAX_SLOTS; i++) {
                        long va = vtVA + i * 8L;
                        Address a = prog.getAddressFactory().getAddress(Long.toHexString(va));
                        long q;
                        try { q = mem.getLong(a); } catch (Exception e) { break; }
                        if (q == 0) break;
                        Address tgt = prog.getAddressFactory().getAddress(Long.toHexString(q));
                        if (text != null && (tgt.compareTo(text.getStart()) < 0 || tgt.compareTo(text.getEnd()) > 0)) break;
                        Function f = fm.getFunctionAt(tgt);
                        if (f == null) break;
                        long frva = tgt.getOffset() - base.getOffset();
                        pw.printf("  slot %2d @ 0x%X  -> 0x%X  RVA 0x%X  %s%n",
                            i, va, q, frva, f.getName());
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
            }
        }

        // 2) Callers of the factory FUN_1404E7370. Whoever calls it with a
        //    typed conn tells us the concrete NetConnection vtable, which
        //    in turn tells us what the +0x1F8 vfn is.
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_abi_post_callers.txt"))))) {
            Address tgt = base.add(SEND_FACTORY_RVA);
            Function factory = fm.getFunctionAt(tgt);
            pw.printf("=== factory RVA 0x%X  %s ===%n", SEND_FACTORY_RVA, factory == null ? "(none)" : factory.getName());
            if (factory != null) {
                DecompileResults rr = dec.decompileFunction(factory, 240, monitor);
                pw.println(rr.decompileCompleted() ? rr.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
            Set<Function> callers = new LinkedHashSet<>();
            ReferenceIterator ri = refMgr.getReferencesTo(tgt);
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) callers.add(f);
            }
            for (Function f : callers) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== caller RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        // 3) Class-registration slots. Decompile each; whatever they call
        //    that isn't EngineFunctionInfo::add (FUN_14041DF20) is our
        //    candidate NetClassRep::add.
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "netevent_abi_classreg.txt"))))) {
            Set<Long> seen = new LinkedHashSet<>();
            for (long rva : CLASS_REG_SLOT_RVAS) {
                if (!seen.add(rva)) continue;
                Function f = fm.getFunctionAt(base.add(rva));
                if (f == null) { pw.printf("(no fn at 0x%X)%n%n", rva); continue; }
                pw.printf("=== slot fn RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 120, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }

        println("DONE. /tmp/lifx_ghidra/netevent_abi_*.txt");
    }

    /**
     * Heuristic: scan the ctor's decomp source for a vftable assignment.
     * Two shapes we accept:
     *   *plVar = &PTR_LAB_140xxxxxxx
     *   *plVar = (...)0x140xxxxxxx
     * Returns the VA or 0 if we can't tell.
     */
    private long scanDecompForVftable(String code, Program prog) {
        for (String line : code.split("\n")) {
            String s = line.trim();
            if (!s.startsWith("*")) continue;
            int eq = s.indexOf('=');
            if (eq < 0) continue;
            String rhs = s.substring(eq + 1).trim();
            // pull first 0x140... literal
            int i = rhs.indexOf("0x140");
            if (i < 0) continue;
            int j = i + 2;
            while (j < rhs.length() && isHex(rhs.charAt(j))) j++;
            try {
                long v = Long.parseLong(rhs.substring(i + 2, j), 16);
                // sanity: must be in image
                Address a = prog.getAddressFactory().getAddress(Long.toHexString(v));
                if (prog.getMemory().contains(a)) return v;
            } catch (Exception ignored) {}
        }
        return 0;
    }

    private static boolean isHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
}
