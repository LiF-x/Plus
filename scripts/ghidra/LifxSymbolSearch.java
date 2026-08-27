// #154: dump all symbols whose (namespaced) name matches weapon/hit/swing/melee/mount
// patterns, with module-relative RVAs — to target the swing-initiator / hit-construction
// and the mount path by NAME (this binary carries rich RTTI/symbol names).
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class LifxSymbolSearch extends GhidraScript {

    private static final String[] PATTERNS = {
        "Weapons::", "Hit::", "::Hit", "HitNode", "Swing", "swing", "Melee", "melee",
        "applyHit", "inflict", "Inflict", "Damage::", "::strike", "Strike",
        "powerHit", "fastHit", "useWeapon", "mountImage", "ShapeBaseImage", "Mount_movable",
        "::Attack", "AttackAnimation"
    };

    @Override
    public void run() throws Exception {
        File out = new File("/tmp/lifx_ghidra/weapon_symbols.txt");
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        SymbolTable st = prog.getSymbolTable();
        int n = 0;
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (Symbol s : st.getAllSymbols(false)) {
                String nm = s.getName(true);
                if (nm == null) continue;
                for (String p : PATTERNS) {
                    if (nm.contains(p)) {
                        Address a = s.getAddress();
                        String rva = (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
                                     ? String.format("0x%X", a.subtract(base)) : String.valueOf(a);
                        pw.printf("%-14s %-9s %s%n", rva, s.getSymbolType(), nm);
                        n++;
                        break;
                    }
                }
            }
        }
        println("DONE  matches=" + n + " -> /tmp/lifx_ghidra/weapon_symbols.txt");
    }
}
