// #154 held-weapon RE: decompile target fns + list each fn's callees + dump
// discriminating string xrefs. Picks the RVA/string set by program name so the
// same script serves both ddctd_cm_yo_server.exe and yo_cm_client.exe.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class LifxWeaponRE extends GhidraScript {

    // server-binary RVAs (base 0x140000000)
    private static final long[] SERVER_RVAS = {
        0x18E8A0L, // _AnimalBehaviorNodes::init  (registers the native "Attack" node)
        0x18C930L, // AnimalData::initPersistFields  (weaponData @ datablock +0x8488)
        0x18C790L, // ANIMAL_GET_DATABLOCK_BY_TYPE
        0x18B450L, // Animal::packUpdate  (ticks the AI tree)
        0x154020L, // AI_TREE_PROCESS
        0x0A4BF0L, // ONEPUNCHMAN_DAMAGE_CALC  (the calc that never fires)
        0x091A50L, // CHAR_CALC_HIT_DAMAGE
        0x0EE0F0L, // PLAYER_APPLY_HIT  (landed-hit / victim side)
        0x2E2E60L, // NPCS::Base::_applyHit
        0x1F38D0L, // EQUIP_SET_SLOT_HIGH_FN  (setSlot)
        0x1F01D0L, // EQUIP_CAN_SET_SLOT  (slot validation)
        0x1F2760L, // EQUIP_LOAD_FROM_DB
    };
    private static final String[] SERVER_STRINGS = {
        "weaponData", "mountImage", "ShapeBaseImage", "mountObject", "weaponWeight"
    };

    // client-binary RVAs
    private static final long[] CLIENT_RVAS = {
        0x3657D0L, // render dispatcher (held-weapon mount lead)
        0x37A4C0L, // EquipmentEvent::unpack
        0x379DE0L, // EquipmentEvent::process
    };
    private static final String[] CLIENT_STRINGS = {
        "mountImage", "ShapeBaseImage", "mountObject", "weaponData"
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        String pname = prog.getName();
        boolean isClient = pname.toLowerCase().contains("client");
        long[]   rvas    = isClient ? CLIENT_RVAS    : SERVER_RVAS;
        String[] strings = isClient ? CLIENT_STRINGS : SERVER_STRINGS;

        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        File out = new File(outDir, "weapon_re_" + (isClient ? "client" : "server") + ".txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            pw.println("### program: " + pname + "   base=" + base);

            for (long rva : rvas) {
                Address a = base.add(rva);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n==================== RVA 0x%X   %s ====================%n",
                          rva, (f == null ? "(no function at addr)" : f.getName()));
                if (f == null) continue;

                pw.println("-- callees (rva  name) --");
                for (Function c : f.getCalledFunctions(monitor)) {
                    Address ep = c.getEntryPoint();
                    if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace())) {
                        pw.printf("   (extern)  %s%n", c.getName());
                    } else {
                        pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
                    }
                }

                pw.println("-- decompile --");
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                pw.println(res.decompileCompleted()
                           ? res.getDecompiledFunction().getC()
                           : "(decompile failed)");
            }

            pw.println();
            pw.println("==================== STRING XREFS ====================");
            DataIterator di = prog.getListing().getDefinedData(true);
            while (di.hasNext() && !monitor.isCancelled()) {
                Data d = di.next();
                if (d == null || !d.hasStringValue()) continue;
                String s = d.getDefaultValueRepresentation();
                if (s == null) continue;
                for (String want : strings) {
                    if (s.contains(want)) {
                        Address da = d.getAddress();
                        pw.printf("STR @%s  %s%n", relAddr(da, base), s);
                        ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(da);
                        int n = 0;
                        while (ri.hasNext() && n++ < 10) {
                            Reference r = ri.next();
                            Address fa = r.getFromAddress();
                            Function ff = fm.getFunctionContaining(fa);
                            pw.printf("      <- %s  in %s%n", relAddr(fa, base), (ff == null ? "?" : ff.getName()));
                        }
                        break;
                    }
                }
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }

    private static String relAddr(Address a, Address base) {
        if (a != null && a.getAddressSpace().equals(base.getAddressSpace()))
            return String.format("0x%X", a.subtract(base));
        return String.valueOf(a);
    }
}
