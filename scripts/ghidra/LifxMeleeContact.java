// #154 STRIKE: find the PLAYER melee swing->contact handler that builds the hit
// descriptor and invokes _applyHit virtually. Players don't use DTS contact markers;
// a server-side handler runs the hit test mid-swing. We want to call THAT (attacker=
// bandit, victim=player) instead of reconstructing the giant _applyHit descriptor.
//
//  (1) vtable slot index of _applyHit (0xEE0F0) inside the Player vtable @0xC15FC0
//  (2) every function that references the fight\ source-file strings (hitboxes.cpp,
//      weapondata.cpp, player.cpp melee asserts) -> the contact code
//  (3) decompile the AI swing-completion poll FUN_1402e2740 + the swing 0x18B950
//      callees, to see whether contact is triggered there.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class LifxMeleeContact extends GhidraScript {

    private static final long APPLYHIT_PLAYER = 0x0EE0F0L;
    private static final long PLAYER_VTABLE    = 0xC15FC0L;
    private static final long NPC_VTABLE       = 0xC2B638L;
    private static final long[] DECOMP = {
        0x2E2740L, // AI swing-completion poll
        0x18B950L, // swing
    };
    private static final String[] WANT_STR = {
        "hitboxes.cpp", "weapondata.cpp", "Attack_Fast", "Attack_Power",
        "melee", "swingWeapon", "onCollision", "computeDamage", "inflictDamage",
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        Memory mem = prog.getMemory();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "melee_contact.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {

            // (1) vtable slot index of _applyHit
            pw.println("########## (1) _applyHit vtable slot index ##########");
            for (long vt : new long[]{PLAYER_VTABLE, NPC_VTABLE}) {
                pw.printf("-- vtable 0x%X --%n", vt);
                Address va = base.add(vt);
                for (int i = 0; i < 256; i++) {
                    long fnptr = mem.getLong(va.add((long)i * 8));
                    long rva = fnptr - base.getOffset();
                    if (rva == APPLYHIT_PLAYER || rva == 0x2E2E60L) {
                        pw.printf("   slot[%d] (vt+0x%X) -> 0x%X  *** _applyHit ***%n", i, i * 8, rva);
                    }
                }
            }

            // (2) functions referencing fight/melee source strings
            pw.println();
            pw.println("########## (2) functions referencing melee/fight strings ##########");
            LinkedHashSet<Long> meleeFns = new LinkedHashSet<>();
            DataIterator di = prog.getListing().getDefinedData(true);
            // brute scan defined strings
            for (Data d = di.hasNext() ? di.next() : null; d != null; d = di.hasNext() ? di.next() : null) {
                Object v = d.getValue();
                if (!(v instanceof String)) continue;
                String s = (String) v;
                boolean hit = false;
                for (String w : WANT_STR) if (s.contains(w)) { hit = true; break; }
                if (!hit) continue;
                ReferenceIterator ri = rm.getReferencesTo(d.getAddress());
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function ff = fm.getFunctionContaining(r.getFromAddress());
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        meleeFns.add(frva);
                        pw.printf("   \"%s\"  <- fn 0x%X %s%n",
                                  s.length() > 48 ? s.substring(0, 48) : s, frva, ff.getName());
                    }
                }
            }

            // (3) decompile the AI poll + swing
            pw.println();
            pw.println("########## (3) decompile swing-completion poll + swing ##########");
            for (long t : DECOMP) decompile(pw, fm, dec, base, t);

            // (4) decompile a few of the melee-string functions (likely the contact handler)
            pw.println();
            pw.println("########## (4) decompile melee-string functions (capped 8) ##########");
            int n = 0;
            for (long rva : meleeFns) {
                if (n++ >= 8) { pw.println("(capped at 8)"); break; }
                decompile(pw, fm, dec, base, rva);
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        pw.println("-- callees --");
        for (Function c : f.getCalledFunctions(monitor)) {
            Address ep = c.getEntryPoint();
            if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                pw.printf("   (extern)  %s%n", c.getName());
            else
                pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
        }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
