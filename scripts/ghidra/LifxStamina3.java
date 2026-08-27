// #154 stamina pt3: find the PLAYER swing stamina-cost calc + drain, to reuse for the NPC.
//  (1) every symbol whose (demangled) name contains "tamina" — the full CharacterStatsAPI
//      / CharacterVitalParameters stamina surface (Get/Set/Use/Spend/Decrease/Add soft+hard).
//  (2) decompile the soft-stamina field owner: trace On_soft_stamina_exhausted (0x96D00) up
//      via its caller 0xEBC90 -> 0xEBC90's callers = the decrement site (softStam -= cost).
//  (3) decompile any symbol that looks like "use/spend/decrease ... stamina" so we see the
//      cost argument + the field it writes.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxStamina3 extends GhidraScript {

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        SymbolTable st = prog.getSymbolTable();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "stamina3.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("########## (1) ALL symbols containing 'tamina' (functions only) ##########");
            ArrayList<Long> spendish = new ArrayList<>();
            SymbolIterator si = st.getAllSymbols(true);
            while (si.hasNext()) {
                Symbol s = si.next();
                String nm = s.getName(true);
                if (!nm.contains("tamina")) continue;
                Function f = fm.getFunctionAt(s.getAddress());
                if (f == null) continue;   // functions only
                long rva = s.getAddress().subtract(base);
                pw.printf("   0x%-8X %s%n", rva, nm);
                String low = nm.toLowerCase();
                if (low.contains("use") || low.contains("spend") || low.contains("decrease")
                    || low.contains("drain") || low.contains("consume") || low.contains("subtract")
                    || low.contains("apply") || low.contains("change") || low.contains("set_soft")
                    || low.contains("add_soft") || low.contains("modif"))
                    spendish.add(rva);
            }

            pw.println();
            pw.println("########## (2) soft-stamina decrement site (callers of 0xEBC90) ##########");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            ReferenceIterator ri = rm.getReferencesTo(base.add(0xEBC90L));
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function ff = fm.getFunctionContaining(r.getFromAddress());
                if (ff != null) {
                    long frva = ff.getEntryPoint().subtract(base);
                    pw.printf("   %-8s from 0x%X in %s (fn 0x%X)%n",
                              r.getReferenceType(), r.getFromAddress().subtract(base), ff.getName(), frva);
                    if (r.getReferenceType().isCall()) callers.add(frva);
                }
            }
            int n = 0;
            for (long rva : callers) { if (n++ >= 4) break; decompile(pw, fm, dec, base, rva); }

            pw.println();
            pw.println("########## (3) decompile spend/use/decrease-stamina methods (capped 8) ##########");
            n = 0;
            for (long rva : spendish) { if (n++ >= 8) break; decompile(pw, fm, dec, base, rva); }
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
