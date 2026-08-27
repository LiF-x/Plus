// #154 STRIKE: endAttack (0x18A4D0) bails at the weapon gate FUN_1400bde00(this+0x24f0).
// Decompile that gate + the descriptor builder FUN_1400a41d0 + apply FUN_14051f030 +
// the node/type helpers 0xBDD30/0xBDCE0, and find every instruction that WRITES the
// animal weapon field (this+0x24f0) so we learn how the native wolf populates it.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;

public class LifxWeaponField extends GhidraScript {
    private static final long[] DECOMP = {
        0x0BDE00L, // weapon gate (returns -1 if no weapon)
        0x0BDCE0L, // attack-type helper
        0x0BDD30L, // hit-node helper
        0x0A41D0L, // hit-descriptor builder
        0x51F030L, // apply-hit to victim
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);
        Listing lst = prog.getListing();

        File out = new File(outDir, "weapon_field.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : DECOMP) decompile(pw, fm, dec, base, t);

            // scan for MOV [reg+0x24f0], reg  (writes to the weapon field)
            pw.println();
            pw.println("########## instructions touching +0x24f0 / +0x24f8 (weapon/attack-type) ##########");
            InstructionIterator it = lst.getInstructions(true);
            int n = 0;
            while (it.hasNext() && n < 4000000) {
                Instruction ins = it.next();
                n++;
                String s = ins.toString();
                if (s.contains("0x24f0") || s.contains("0x24f8") || s.contains("0x24fc") || s.contains("0x24f0]")) {
                    Function ff = fm.getFunctionContaining(ins.getAddress());
                    long frva = ff == null ? -1 : ff.getEntryPoint().subtract(base);
                    pw.printf("   0x%X  %-40s  (fn 0x%X %s)%n",
                              ins.getAddress().subtract(base), s,
                              frva, ff == null ? "?" : ff.getName());
                }
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
