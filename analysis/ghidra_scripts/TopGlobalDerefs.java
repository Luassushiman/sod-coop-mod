// Conta leituras "MOV reg, dword ptr [abs32]" por endereço global e lista os
// mais usados. Em CryEngine o ponteiro gEnv costuma ser o mais lido.
// Para os top N, mostra também os deslocamentos mais usados logo em seguida
// ("MOV reg2, [reg + off]"), que indicam campos da estrutura apontada.
// Uso: -postScript TopGlobalDerefs.java <arquivo_saida>
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class TopGlobalDerefs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		PrintWriter out = new PrintWriter(new FileWriter(getScriptArgs()[0]));
		Listing listing = currentProgram.getListing();
		MemoryBlock data = currentProgram.getMemory().getBlock(".data");
		Map<Long, Integer> count = new HashMap<>();
		Map<Long, Map<Long, Integer>> fieldUse = new HashMap<>();
		InstructionIterator it = listing.getInstructions(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (!ins.getMnemonicString().equals("MOV") || ins.getNumOperands() != 2) continue;
			Object[] dst = ins.getOpObjects(0);
			Object[] src = ins.getOpObjects(1);
			if (dst.length != 1 || !(dst[0] instanceof Register) || src.length != 1) continue;
			long addr;
			if (src[0] instanceof Address) addr = ((Address) src[0]).getOffset();
			else continue;
			if (!ins.getDefaultOperandRepresentation(1).startsWith("dword ptr [0x")) continue;
			Address a = toAddr(addr);
			if (data == null || !data.contains(a)) continue;
			count.merge(addr, 1, Integer::sum);
			// próxima instrução usa o registrador como base?
			Register r = (Register) dst[0];
			Instruction nx = ins.getNext();
			for (int k = 0; k < 2 && nx != null; k++, nx = nx.getNext()) {
				if (!nx.getMnemonicString().equals("MOV") || nx.getNumOperands() != 2) continue;
				Object[] s2 = nx.getOpObjects(1);
				if (s2.length == 2 && s2[0] instanceof Register && ((Register) s2[0]).equals(r) && s2[1] instanceof Scalar) {
					long off = ((Scalar) s2[1]).getValue();
					fieldUse.computeIfAbsent(addr, x -> new HashMap<>()).merge(off, 1, Integer::sum);
					break;
				}
			}
		}
		List<Map.Entry<Long, Integer>> list = new ArrayList<>(count.entrySet());
		list.sort((x, y) -> y.getValue() - x.getValue());
		for (int i = 0; i < Math.min(25, list.size()); i++) {
			long a = list.get(i).getKey();
			out.printf("%08x  reads=%d%n", a, list.get(i).getValue());
			Map<Long, Integer> f = fieldUse.getOrDefault(a, Map.of());
			List<Map.Entry<Long, Integer>> fl = new ArrayList<>(f.entrySet());
			fl.sort((x, y) -> y.getValue() - x.getValue());
			StringBuilder sb = new StringBuilder("    fields:");
			for (int j = 0; j < Math.min(20, fl.size()); j++) sb.append(String.format(" +0x%x(%d)", fl.get(j).getKey(), fl.get(j).getValue()));
			out.println(sb);
		}
		out.close();
		println("TopGlobalDerefs: wrote " + getScriptArgs()[0]);
	}
}
