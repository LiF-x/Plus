// #154 round-2 RE: resolve AI::Nodes::Attack::vftable (and Move for comparison),
// dump their slots, decompile every Attack slot (to find Attack::process) plus a
// list of supporting fns (node dispatcher, mount path, animal damage calc, weaponData resolver).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class LifxAttackVtable extends GhidraScript {

    // round-3: callees of AI::Nodes::Attack::process (0x193400) — does any hide a strike?
    private static final long[] RVAS = {
        0x18B950L, // process callee, animal range — prime swing/strike suspect
        0x2E2740L, // process callee, anim range (near setAnimation 0x2E2A90)
        0x1530D0L, // process callee
        0x191130L, // process callee
        0x190D70L, // process callee (animal/aidata resolver)
        0x1531A0L, // interrupt callee
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        Memory mem = prog.getMemory();
        SymbolTable st = prog.getSymbolTable();
        AddressSpace sp = prog.getAddressFactory().getDefaultAddressSpace();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        LinkedHashSet<Long> toDecompile = new LinkedHashSet<>();
        for (long r : RVAS) toDecompile.add(r);

        File out = new File(outDir, "weapon_re3.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("### " + prog.getName() + "  base=" + base);

            // --- dump AI node vtables, collect Attack slots for decompile ---
            for (Symbol s : st.getAllSymbols(false)) {
                String nm = s.getName(true);
                if (nm == null) continue;
                boolean isAttack = nm.contains("Nodes::Attack") && nm.contains("vftable");
                boolean isMove   = nm.contains("Nodes::Move")   && nm.contains("vftable");
                if (!isAttack && !isMove) continue;
                Address va = s.getAddress();
                pw.printf("%n==== VTABLE %s @ 0x%X ====%n", nm, va.subtract(base));
                for (int i = 0; i < 24; i++) {
                    try {
                        long ptr = mem.getLong(va.add(i * 8L));
                        if (ptr == 0) break;
                        Address fa = sp.getAddress(ptr);
                        Function f = fm.getFunctionAt(fa);
                        long frva = fa.subtract(base);
                        pw.printf("  slot %2d: 0x%X  %s%n", i, frva, (f == null ? "(no fn)" : f.getName()));
                        if (isAttack && f != null) toDecompile.add(frva);
                    } catch (Exception e) { break; }
                }
            }

            // --- decompile collected functions ---
            for (long rva : toDecompile) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n==================== RVA 0x%X  %s ====================%n",
                          rva, (f == null ? "(no fn)" : f.getName()));
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
        println("DONE -> " + out.getAbsolutePath());
    }
}
