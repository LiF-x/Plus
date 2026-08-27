// Hunt the effect / ability / special-attack subsystem in the LiF server binary.
//
// Strategy: anchor on strings that the engine *must* reference (XML filenames
// and the parameter-type enum tokens listed in data/cm_effects.xml), then
// emit xrefs as RVA candidates. Also enumerate RTTI classes whose names hint
// at the subsystem so we can cross-check against vftables.
//
// Outputs (in /tmp/lifx_ghidra/):
//   - effects_xml_loaders.tsv     functions that reference effect-related XML filenames
//   - effects_param_strings.tsv   xrefs to parameter-type tokens (SPEED, HARD_HP_MAX, ...)
//   - effects_fan_in.tsv          functions ranked by how many param-token xrefs they contain
//   - effects_classes.tsv         RTTI classes matching Effect/Ability/SpecialAttack/Trigger
//   - effects_class_vtables.tsv   first vftable for each matching class, with slot 0..N targets
//
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxEffectsScan extends GhidraScript {

    // XML filenames the loader references verbatim.
    static final String[] XML_FILES = {
        "cm_effects.xml",
        "admin_lands_abilities.xml",
        "cm_special_attacks.xml",
        "characterTriggers.xml",
        "item_effects.xml",
        "skill_types.xml",
    };

    // Parameter-type tokens enumerated from cm_effects.xml (`parameter type=...`
    // and `applytype=...`). Engine parses these as strings -> internal enum.
    static final String[] PARAM_TOKENS = {
        "SPEED", "ATTACK_SPEED", "RANGED_ATTACK_SPEED", "BAREFOOTED_SPEED",
        "CAST_DURATION",
        "HARD_HP", "HARD_HP_MAX", "HARD_HP_REGEN_SPEED",
        "HARD_STAMINA", "HARD_STAMINA_REGEN_SPEED",
        "SOFT_HP",
        "AGI", "CON", "INT", "LUCK",
        "DAMAGE", "DEFENCE", "FEAR",
        "SKILL", "SKILL_GROW_BONUS_MULT",
        "TITLE", "RESTING", "SICK", "REGENERATE",
        "POISON_IMMUNE", "DRINK_IMMUNE",
        // applytypes
        "CONSUME", "INCREASE", "DECREASE", "INCREASE_COEFF", "DECREASE_COEFF",
        "MULTIPLY", "BIND_TO_EFFECT_LIFETIME",
    };

    // Class-name fragments worth listing (case-sensitive substring).
    static final String[] CLASS_HINTS = {
        "Effect", "Ability", "SpecialAttack", "Trigger", "Buff", "Debuff",
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();
        Memory mem = prog.getMemory();
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();

        Set<String> xmlSet = new HashSet<>(Arrays.asList(XML_FILES));
        Set<String> tokenSet = new HashSet<>(Arrays.asList(PARAM_TOKENS));

        // Per-function counters for fan-in ranking.
        Map<Function, Integer> tokenHitsPerFunc = new HashMap<>();
        Map<Function, Set<String>> tokensPerFunc = new HashMap<>();

        try (PrintWriter pwXml = pw(outDir, "effects_xml_loaders.tsv");
             PrintWriter pwTok = pw(outDir, "effects_param_strings.tsv")) {

            pwXml.println("string_rva\tstring_value\tfrom_func_rva\tfrom_func\tfrom_addr");
            pwTok.println("token\tstring_rva\tfrom_func_rva\tfrom_func\tfrom_addr");

            int strScanned = 0;
            DataIterator di = listing.getDefinedData(true);
            while (di.hasNext()) {
                Data d = di.next();
                String dn = d.getDataType().getName().toLowerCase();
                if (!(dn.contains("string") || dn.contains("char"))) continue;
                StringDataInstance sd = StringDataInstance.getStringDataInstance(d);
                String s = sd.getStringValue();
                if (s == null) continue;
                strScanned++;

                boolean isXml = xmlSet.contains(s);
                boolean isTok = tokenSet.contains(s);
                if (!isXml && !isTok) continue;

                Address a = d.getAddress();
                long srva = a.getOffset() - base.getOffset();

                ReferenceIterator ri = d.getReferenceIteratorTo();
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address from = r.getFromAddress();
                    Function f = fm.getFunctionContaining(from);
                    long frva = (f != null) ? f.getEntryPoint().getOffset() - base.getOffset() : -1;
                    String fname = (f != null) ? f.getName() : "<no-func>";

                    if (isXml) {
                        pwXml.printf("0x%X\t%s\t0x%X\t%s\t%s%n", srva, s, frva, fname, from);
                    }
                    if (isTok && f != null) {
                        pwTok.printf("%s\t0x%X\t0x%X\t%s\t%s%n", s, srva, frva, fname, from);
                        tokenHitsPerFunc.merge(f, 1, Integer::sum);
                        tokensPerFunc.computeIfAbsent(f, k -> new TreeSet<>()).add(s);
                    }
                }
            }
            println("Scanned " + strScanned + " strings");
        }

        // Fan-in: functions that reference the most distinct param tokens are
        // the strongest candidates for the string -> enum parser (and, by
        // extension, the effect loader / apply-with-parameter dispatcher).
        try (PrintWriter pw = pw(outDir, "effects_fan_in.tsv")) {
            pw.println("distinct_tokens\ttotal_hits\tfunc_rva\tfunc\ttokens");
            List<Map.Entry<Function, Set<String>>> ranked =
                new ArrayList<>(tokensPerFunc.entrySet());
            ranked.sort((a, b) -> {
                int byDistinct = Integer.compare(b.getValue().size(), a.getValue().size());
                if (byDistinct != 0) return byDistinct;
                return Integer.compare(
                    tokenHitsPerFunc.getOrDefault(b.getKey(), 0),
                    tokenHitsPerFunc.getOrDefault(a.getKey(), 0));
            });
            for (Map.Entry<Function, Set<String>> e : ranked) {
                Function f = e.getKey();
                long rva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("%d\t%d\t0x%X\t%s\t%s%n",
                    e.getValue().size(),
                    tokenHitsPerFunc.getOrDefault(f, 0),
                    rva, f.getName(),
                    String.join(",", e.getValue()));
            }
            println("Fan-in ranked " + ranked.size() + " functions");
        }

        // RTTI class hits.
        Set<String> classes = new TreeSet<>();
        SymbolIterator allSyms = st.getAllSymbols(true);
        while (allSyms.hasNext()) {
            Symbol sym = allSyms.next();
            Namespace ns = sym.getParentNamespace();
            if (ns == null || ns.isGlobal()) continue;
            String full = ns.getName(true);
            String leaf = ns.getName();
            for (String hint : CLASS_HINTS) {
                if (leaf.contains(hint)) { classes.add(full); break; }
            }
        }
        try (PrintWriter pw = pw(outDir, "effects_classes.tsv")) {
            pw.println("class");
            for (String c : classes) pw.println(c);
            println("Class hits: " + classes.size());
        }

        // Vftables for matching classes.
        try (PrintWriter pw = pw(outDir, "effects_class_vtables.tsv")) {
            pw.println("class\tvft_rva\tvft_addr\tslot\tslot_target_rva\tslot_target_func");
            SymbolIterator si = st.getAllSymbols(true);
            Map<String, Address> seenVft = new TreeMap<>();
            while (si.hasNext()) {
                Symbol s = si.next();
                String name = s.getName();
                if (!"vftable".equals(name) && !"`vftable'".equals(name) && !name.endsWith("vftable")) continue;
                Namespace ns = s.getParentNamespace();
                if (ns == null) continue;
                String full = ns.getName(true);
                if (!classes.contains(full)) continue;
                if (seenVft.putIfAbsent(full, s.getAddress()) != null) continue;

                Address vft = s.getAddress();
                long vftRva = vft.getOffset() - base.getOffset();
                // Dump up to 16 slots (8 bytes each).
                for (int slot = 0; slot < 16; slot++) {
                    Address slotAddr = vft.add((long) slot * 8);
                    long target;
                    try {
                        target = mem.getLong(slotAddr);
                    } catch (Exception ex) { break; }
                    if (target == 0) break;
                    Address tAddr;
                    try { tAddr = base.getNewAddress(target); }
                    catch (Exception ex) { break; }
                    long tRva = target - base.getOffset();
                    Function tf = fm.getFunctionContaining(tAddr);
                    String tName = (tf != null) ? tf.getName() : "<no-func>";
                    pw.printf("%s\t0x%X\t%s\t%d\t0x%X\t%s%n",
                        full, vftRva, vft, slot, tRva, tName);
                }
            }
        }

        println("DONE. Outputs in /tmp/lifx_ghidra/effects_*.tsv");
    }

    private static PrintWriter pw(File dir, String name) throws IOException {
        return new PrintWriter(new BufferedWriter(new FileWriter(new File(dir, name))));
    }
}
