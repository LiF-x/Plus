// #154 strike RE: find the hit-construction code by xref'ing the Weapons:: type vtables
// (the swing builds a Weapons::HitNodeType / AttackTypes hit). Decompile the referencing
// functions to locate the player melee-swing initiator that builds the descriptor + applies it.
// Also dump the visible-equip-slot table bytes (right-hand weapon slot index).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxHitBuilders extends GhidraScript {

    private static final long[] DATA_TARGETS = {
        0x79C330L, // boost::any::holder<Weapons::HitNodeType>::vftable
        0x743550L, // EngineSimpleTypeInfo<Weapons::HitNodeType>::vftable
        0x7436C8L, // EngineSimpleTypeInfo<Weapons::AttackTypes>::vftable
        0x743860L, // EngineSimpleTypeInfo<Weapons::WeaponTypes>::vftable
    };
    private static final long[] SLOT_DATA = { 0x7ADE54L }; // visible-slot table (6 mountable slots)

    @Override
    public void run() throws Exception {
        File out = new File("/tmp/lifx_ghidra/weapon_hitbuilders.txt");
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        Memory mem = prog.getMemory();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        LinkedHashSet<Long> fns = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long d : SLOT_DATA) {
                Address a = base.add(d);
                pw.printf("SLOT DATA @0x%X: ", d);
                for (int i = 0; i < 16; i++) {
                    try { pw.printf("%02X ", mem.getByte(a.add(i)) & 0xff); }
                    catch (Exception e) { pw.print("?? "); }
                }
                pw.println();
            }

            for (long t : DATA_TARGETS) {
                Address ta = base.add(t);
                pw.println();
                pw.printf("#### refs TO 0x%X ####%n", t);
                ReferenceIterator ri = rm.getReferencesTo(ta);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address fa = r.getFromAddress();
                    Function ff = fm.getFunctionContaining(fa);
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        pw.printf("   from %s in fn 0x%X  %s%n", relAddr(fa, base), frva, ff.getName());
                        fns.add(frva);
                    } else {
                        pw.printf("   from %s (no containing fn)%n", relAddr(fa, base));
                    }
                }
            }

            pw.println();
            pw.println("==================== DECOMPILE referencing functions ====================");
            int count = 0;
            for (long rva : fns) {
                if (count++ >= 20) { pw.println("(capped at 20)"); break; }
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
                if (f == null) continue;
                pw.println("-- callees --");
                for (Function c : f.getCalledFunctions(monitor)) {
                    Address ep = c.getEntryPoint();
                    if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                        pw.printf("   (extern)  %s%n", c.getName());
                    else
                        pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
                }
                pw.println("-- decompile --");
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
            }
        }
        println("DONE -> /tmp/lifx_ghidra/weapon_hitbuilders.txt  (refFns=" + fns.size() + ")");
    }

    private static String relAddr(Address a, Address base) {
        if (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
            return String.format("0x%X", a.subtract(base));
        return String.valueOf(a);
    }
}
