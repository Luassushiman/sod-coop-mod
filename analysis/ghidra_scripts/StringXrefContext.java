// Para cada string alvo: acha a string exata (terminada em \0) na memória,
// lista as instruções que a referenciam e imprime o contexto em assembly
// (instruções anteriores e posteriores) de cada referência.
// Uso: -postScript StringXrefContext.java <arquivo_saida> <arquivo_com_strings>
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class StringXrefContext extends GhidraScript {
	static final int BEFORE = 14, AFTER = 6, MAX_REFS = 12;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		List<String> targets = Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8);
		Memory mem = currentProgram.getMemory();
		Listing listing = currentProgram.getListing();

		// Índice de referências por varredura: operandos escalares (push imm32 etc.)
		Map<Long, List<Address>> scalarRefs = new HashMap<>();
		Set<Long> wanted = new HashSet<>();
		Map<String, List<Address>> strAddrs = new LinkedHashMap<>();
		for (String t : targets) {
			if (t.isBlank()) continue;
			byte[] pat = (t + "\0").getBytes(StandardCharsets.ISO_8859_1);
			List<Address> found = new ArrayList<>();
			Address a = mem.getMinAddress();
			while (a != null && found.size() < 8) {
				a = mem.findBytes(a, pat, null, true, monitor);
				if (a == null) break;
				// garante início de string (byte anterior é \0)
				byte prev = mem.getByte(a.subtract(1));
				if (prev == 0) { found.add(a); wanted.add(a.getOffset()); }
				a = a.add(1);
			}
			strAddrs.put(t, found);
		}
		InstructionIterator it = listing.getInstructions(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			for (int i = 0; i < ins.getNumOperands(); i++) {
				for (Object o : ins.getOpObjects(i)) {
					long v = -1;
					if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
					else if (o instanceof Address) v = ((Address) o).getOffset();
					if (v >= 0 && wanted.contains(v))
						scalarRefs.computeIfAbsent(v, k -> new ArrayList<>()).add(ins.getAddress());
				}
			}
		}

		for (Map.Entry<String, List<Address>> e : strAddrs.entrySet()) {
			out.println("##### STRING \"" + e.getKey() + "\" at " + e.getValue());
			for (Address s : e.getValue()) {
				List<Address> refs = scalarRefs.getOrDefault(s.getOffset(), List.of());
				out.println("  refs to " + s + ": " + refs.size());
				int n = 0;
				for (Address r : refs) {
					if (n++ >= MAX_REFS) break;
					Function f = getFunctionContaining(r);
					out.println("  --- ref " + r + " in " + (f == null ? "<no func>" : f.getName() + "@" + f.getEntryPoint()));
					Instruction ins = listing.getInstructionAt(r);
					List<Instruction> before = new ArrayList<>();
					Instruction p = ins;
					for (int i = 0; i < BEFORE && p != null; i++) { p = p.getPrevious(); if (p != null) before.add(0, p); }
					for (Instruction b : before) out.println("      " + b.getAddress() + "  " + b);
					out.println("   >> " + ins.getAddress() + "  " + ins);
					Instruction q = ins;
					for (int i = 0; i < AFTER && q != null; i++) { q = q.getNext(); if (q != null) out.println("      " + q.getAddress() + "  " + q); }
				}
			}
		}
		out.close();
		println("StringXrefContext: wrote " + args[0]);
	}
}
