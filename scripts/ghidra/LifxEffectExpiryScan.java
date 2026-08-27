// Locate the engine routine(s) that expire/remove an active effect on a
// Player and broadcast the change to the client.
//
// Run against: ddctd_cm_yo_server.exe (the game server binary that hosts
// the RVAs referenced below — NOT our mod DLL 4ba5cb5e.dll).
//
// Anchors (any one is a strong indicator; combined they are decisive):
//   - Immediate 0x1238 ............ active-effect table base offset
//   - Immediate 0x16A0 ............ Player+0x1238 + 47*24 (Resurrected row)
//   - Immediate 24 / 0x18 ......... row stride (paired with 0x1238)
//   - Reference to Character_SendChanges (RVA 0x1BC3D0) — known broadcaster
//   - Reference to ServerTime_Now    (RVA 0x5147A0)  — expiry comparator
//
// Output (in /tmp/lifx_ghidra/):
//   - effect_expiry_anchors.tsv   per-anchor function hit list
//   - effect_expiry_fan_in.tsv    functions ranked by anchor overlap
//
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxEffectExpiryScan extends GhidraScript {

    // Known RVAs from source/server/hooks/furnace/engine_internals.h
    private static final long RVA_CHARACTER_SENDCHANGES = 0x1BC3D0L;
    private static final long RVA_SERVERTIME_NOW        = 0x5147A0L;

    private static class Row {
        Function f;
        int base, res, str, snd, srv;
        int score() { return base + res + str + snd + srv; }
    }

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();
        Address base = prog.getImageBase();

        Set<Function> tableBaseFns = new HashSet<>();
        Set<Function> resRowFns    = new HashSet<>();
        Set<Function> strideFns    = new HashSet<>();

        // --- Pass 1: scan instructions for the table-base / row / stride anchors.
        try (PrintWriter pw = pw(outDir, "effect_expiry_anchors.tsv")) {
            pw.println("anchor\tfrom_func_rva\tfrom_func\tfrom_addr\tinsn");
            InstructionIterator it = listing.getInstructions(true);
            int hits = 0;
            outer:
            while (it.hasNext()) {
                Instruction ins = it.next();
                Function f = fm.getFunctionContaining(ins.getAddress());
                if (f == null) continue;
                int nops = ins.getNumOperands();
                for (int op = 0; op < nops; op++) {
                    Object[] objs = ins.getOpObjects(op);
                    for (Object o : objs) {
                        if (!(o instanceof Scalar)) continue;
                        long v = ((Scalar) o).getValue();
                        String anchor = null;
                        if      (v == 0x1238L) { anchor = "table_base"; tableBaseFns.add(f); }
                        else if (v == 0x16A0L) { anchor = "res_row_47"; resRowFns.add(f); }
                        else if (v == 24L)     { anchor = "stride_24";  strideFns.add(f); }
                        if (anchor == null) continue;
                        long frva = f.getEntryPoint().getOffset() - base.getOffset();
                        pw.printf("%s\t0x%X\t%s\t%s\t%s%n",
                                  anchor, frva, f.getName(), ins.getAddress(), ins);
                        if (++hits > 20000) { println("Stopped at 20000 anchor hits"); break outer; }
                    }
                }
            }
        }
        // stride_24 alone is noise; require co-occurrence with table_base
        strideFns.retainAll(tableBaseFns);
        println("table_base callers: " + tableBaseFns.size());
        println("res_row_47 callers: " + resRowFns.size());
        println("stride_24 in base: "  + strideFns.size());

        // --- Pass 2: xrefs INTO Character_SendChanges and ServerTime_Now.
        Set<Function> sendChangesFns = callersOf(prog, fm, base, RVA_CHARACTER_SENDCHANGES);
        Set<Function> serverTimeFns  = callersOf(prog, fm, base, RVA_SERVERTIME_NOW);
        println("Character_SendChanges callers: " + sendChangesFns.size());
        println("ServerTime_Now callers: "        + serverTimeFns.size());

        // --- Pass 3: rank by anchor-set overlap (universe = any table-row hit).
        Set<Function> universe = new HashSet<>();
        universe.addAll(tableBaseFns);
        universe.addAll(resRowFns);

        List<Row> ranked = new ArrayList<>();
        for (Function f : universe) {
            Row r = new Row();
            r.f   = f;
            r.base = tableBaseFns.contains(f)   ? 1 : 0;
            r.res  = resRowFns.contains(f)      ? 1 : 0;
            r.str  = strideFns.contains(f)      ? 1 : 0;
            r.snd  = sendChangesFns.contains(f) ? 1 : 0;
            r.srv  = serverTimeFns.contains(f)  ? 1 : 0;
            ranked.add(r);
        }
        ranked.sort((a, b) -> Integer.compare(b.score(), a.score()));

        try (PrintWriter pw = pw(outDir, "effect_expiry_fan_in.tsv")) {
            pw.println("score\tin_base\tin_res47\tin_stride\tin_sendchanges\tin_servertime\tfunc_rva\tfunc");
            for (Row r : ranked) {
                long rva = r.f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("%d\t%d\t%d\t%d\t%d\t%d\t0x%X\t%s%n",
                    r.score(), r.base, r.res, r.str, r.snd, r.srv, rva, r.f.getName());
            }
        }
        println("Ranked " + ranked.size() + " candidates");
        println("DONE. /tmp/lifx_ghidra/effect_expiry_*.tsv");
    }

    private static Set<Function> callersOf(Program prog, FunctionManager fm,
                                           Address base, long rva) {
        Set<Function> out = new HashSet<>();
        Address target = base.add(rva);
        Function targetFn = fm.getFunctionAt(target);
        if (targetFn == null) return out;
        ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(target);
        while (ri.hasNext()) {
            Reference r = ri.next();
            Function f = fm.getFunctionContaining(r.getFromAddress());
            if (f != null) out.add(f);
        }
        return out;
    }

    private static PrintWriter pw(File dir, String name) throws IOException {
        return new PrintWriter(new BufferedWriter(new FileWriter(new File(dir, name))));
    }
}
