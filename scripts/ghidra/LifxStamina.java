// #154 stamina-gated swing: find the stamina stat field + the player swing stamina-cost
// path so the bandit's swing can be gated/drained like a player's. Dump every symbol whose
// name mentions stamina/endurance/fatigue, plus any defined string mentioning them and the
// functions that reference those strings (the drain/regen/check sites).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxStamina extends GhidraScript {
    private static final String[] NEEDLES = {
        "stamina", "Stamina", "endurance", "Endurance", "fatigue", "Fatigue",
        "useStamina", "spendStamina", "consumeStamina", "Hardiness", "hardiness",
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

        File out = new File(outDir, "stamina.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("########## (1) symbols matching stamina/endurance/fatigue ##########");
            LinkedHashSet<Long> fns = new LinkedHashSet<>();
            SymbolIterator si = st.getAllSymbols(true);
            while (si.hasNext()) {
                Symbol s = si.next();
                String nm = s.getName(true);
                boolean hit = false;
                for (String n : NEEDLES) if (nm.contains(n)) { hit = true; break; }
                if (!hit) continue;
                Address a = s.getAddress();
                String rva = (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
                             ? String.format("0x%X", a.subtract(base)) : String.valueOf(a);
                pw.printf("   %-10s %s%n", rva, nm);
                Function f = fm.getFunctionContaining(a);
                if (f != null) fns.add(f.getEntryPoint().subtract(base));
            }

            pw.println();
            pw.println("########## (2) strings + referencing functions ##########");
            DataIterator di = prog.getListing().getDefinedData(true);
            for (Data d = di.hasNext() ? di.next() : null; d != null; d = di.hasNext() ? di.next() : null) {
                Object v = d.getValue();
                if (!(v instanceof String)) continue;
                String sv = (String) v;
                boolean hit = false;
                for (String n : NEEDLES) if (sv.contains(n)) { hit = true; break; }
                if (!hit) continue;
                ReferenceIterator ri = rm.getReferencesTo(d.getAddress());
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function ff = fm.getFunctionContaining(r.getFromAddress());
                    if (ff != null) {
                        long frva = ff.getEntryPoint().subtract(base);
                        fns.add(frva);
                        pw.printf("   \"%s\"  <- fn 0x%X %s%n",
                                  sv.length() > 50 ? sv.substring(0, 50) : sv, frva, ff.getName());
                    }
                }
            }

            pw.println();
            pw.println("########## (3) decompile candidate functions (capped 12) ##########");
            int n = 0;
            for (long rva : fns) {
                if (n++ >= 12) { pw.println("(capped at 12)"); break; }
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n========== RVA 0x%X  %s ==========%n", rva, (f == null ? "?" : f.getName()));
                if (f == null) continue;
                DecompileResults res = dec.decompileFunction(f, 90, monitor);
                pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }
}
