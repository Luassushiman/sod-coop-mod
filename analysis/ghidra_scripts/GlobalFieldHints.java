// Para um ponteiro global (ex.: gEnv), agrupa por campo [ptr+off] as chamadas
// virtuais feitas através dele (offset da vtable) e as strings empurradas
// como argumento logo antes da chamada. Serve para dar nome aos campos.
// Uso: -postScript GlobalFieldHints.java <arquivo_saida> <endereco_global_hex>
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class GlobalFieldHints extends GhidraScript {
	static final int WINDOW = 12;

	String readStr(Memory m, long a) {
		try {
			Address ad = toAddr(a);
			if (!m.contains(ad)) return null;
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < 80; i++) {
				byte b = m.getByte(ad.add(i));
				if (b == 0) break;
				if (b < 0x20 || b > 0x7e) return null;
				sb.append((char) b);
			}
			return sb.length() >= 3 ? sb.toString() : null;
		} catch (Exception e) { return null; }
	}

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		long g = Long.parseLong(args[1].replace("0x", ""), 16);
		Memory mem = currentProgram.getMemory();
		Map<Long, Integer> fieldCount = new TreeMap<>();
		Map<Long, Map<Long, Integer>> vcalls = new HashMap<>();
		Map<Long, LinkedHashSet<String>> strs = new HashMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (!ins.getMnemonicString().equals("MOV") || ins.getNumOperands() != 2) continue;
			Object[] s = ins.getOpObjects(1), d = ins.getOpObjects(0);
			if (s.length != 1 || !(s[0] instanceof Address) || ((Address) s[0]).getOffset() != g) continue;
			if (d.length != 1 || !(d[0] instanceof Register)) continue;
			Register base = (Register) d[0];
			Instruction nx = ins.getNext();
			Long field = null; Register obj = null;
			for (int k = 0; k < 3 && nx != null; k++, nx = nx.getNext()) {
				Object[] s2 = nx.getOpObjects(1);
				if (nx.getMnemonicString().equals("MOV") && s2.length == 2 && base.equals(s2[0]) && s2[1] instanceof Scalar) {
					field = ((Scalar) s2[1]).getValue();
					Object[] d2 = nx.getOpObjects(0);
					if (d2.length == 1 && d2[0] instanceof Register) obj = (Register) d2[0];
					break;
				}
			}
			if (field == null) continue;
			fieldCount.merge(field, 1, Integer::sum);
			// procura a chamada virtual seguinte e strings empurradas antes dela
			List<String> pushed = new ArrayList<>();
			Instruction c = nx == null ? null : nx.getNext();
			for (int k = 0; k < WINDOW && c != null; k++, c = c.getNext()) {
				String mn = c.getMnemonicString();
				if (mn.equals("PUSH")) {
					for (Object o : c.getOpObjects(0)) {
						long v = o instanceof Scalar ? ((Scalar) o).getUnsignedValue() : o instanceof Address ? ((Address) o).getOffset() : -1;
						String st = v > 0x400000 ? readStr(mem, v) : null;
						if (st != null) pushed.add(st);
					}
				}
				if (mn.equals("CALL")) {
					Object[] co = c.getOpObjects(0);
					if (co.length == 2 && co[0] instanceof Register && co[1] instanceof Scalar)
						vcalls.computeIfAbsent(field, x -> new HashMap<>()).merge(((Scalar) co[1]).getValue(), 1, Integer::sum);
					LinkedHashSet<String> set = strs.computeIfAbsent(field, x -> new LinkedHashSet<>());
					for (String p : pushed) if (set.size() < 10) set.add(p);
					break;
				}
			}
		}
		for (Map.Entry<Long, Integer> e : fieldCount.entrySet()) {
			long f = e.getKey();
			StringBuilder sb = new StringBuilder(String.format("+0x%03x uses=%-4d vcalls:", f, e.getValue()));
			List<Map.Entry<Long, Integer>> vl = new ArrayList<>(vcalls.getOrDefault(f, Map.of()).entrySet());
			vl.sort((x, y) -> y.getValue() - x.getValue());
			for (int i = 0; i < Math.min(8, vl.size()); i++) sb.append(String.format(" 0x%x(%d)", vl.get(i).getKey(), vl.get(i).getValue()));
			out.println(sb);
			for (String s : strs.getOrDefault(f, new LinkedHashSet<>())) out.println("        \"" + s + "\"");
		}
		out.close();
	}
}
