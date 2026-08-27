// #154 stamina pt2: pin the soft-stamina value offset + the swing drain path.
//  - 0x27DEF0 prints "[Stamina]=%u;"  -> reveals the field read
//  - 0x1F1DF0 CmPlayerEquipment::getMovementSoftStaminaRatio -> reads cur/max (the offsets)
//  - dump all symbols with soft_stamina / Soft_stamina / Spend / Use_ / Decrease + stamina
//  - decompile callers of On_soft_stamina_exhausted (0x96D00) -> the decrement/drain site
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxStamina2 extends GhidraScript {
    private static final long[] DECOMP = { 0x27DEF0L, 0x1F1DF0L };
    private static final String[] NEEDLES = {
        "soft_stamina", "Soft_stamina", "SoftStamina", "softStamina",
        "Spend", "Decrease", "Use_stamina", "useStamina", "Drain", "stamina_",
        "Hard_stamina", "hard_stamina", "Restore", "Recover",
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        SymbolTable st = prog.getSymbolTable();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "stamina2.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("########## (1) stamina-method symbols ##########");
            SymbolIterator si = st.getAllSymbols(true);
            while (si.hasNext()) {
                Symbol s = si.next();
                String nm = s.getName(true);
                if (!nm.toLowerCase().contains("stamina")) continue;
                boolean want = false;
                for (String n : NEEDLES) if (nm.contains(n)) { want = true; break; }
                if (!want) continue;
                Address a = s.getAddress();
                String rva = (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
                             ? String.format("0x%X", a.subtract(base)) : String.valueOf(a);
                pw.printf("   %-10s %s%n", rva, nm);
            }

            pw.println();
            pw.println("########## (2) value accessors ##########");
            for (long t : DECOMP) decompile(pw, fm, dec, base, t);

            pw.println();
            pw.println("########## (3) callers of On_soft_stamina_exhausted 0x96D00 (the drain site) ##########");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            ReferenceIterator ri = rm.getReferencesTo(base.add(0x96D00L));
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function ff = fm.getFunctionContaining(r.getFromAddress());
                if (ff != null) {
                    long frva = ff.getEntryPoint().subtract(base);
                    pw.printf("   %-10s from 0x%X in %s (fn 0x%X)%n",
                              r.getReferenceType(), r.getFromAddress().subtract(base), ff.getName(), frva);
                    if (r.getReferenceType().isCall()) callers.add(frva);
                }
            }
            int n = 0;
            for (long rva : callers) { if (n++ >= 5) break; decompile(pw, fm, dec, base, rva); }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private void decompile(PrintWriter pw, FunctionManager fm, DecompInterface dec, Address base, long rva) {
        Address a = base.add(rva);
        Function f = fm.getFunctionAt(a);
        pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
        if (f == null) { pw.println("(no function)"); return; }
        DecompileResults res = dec.decompileFunction(f, 90, monitor);
        pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
    }
}
