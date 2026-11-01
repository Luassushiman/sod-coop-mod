// Acha funções curtas que gravam em [ECX + <offset>] (setters de um campo) e,
// para cada uma, as vtables onde aparecem: endereço da entrada, início provável
// da vtable (primeira entrada referenciada por código) e o offset da entrada.
// Uso: -postScript FindFieldSetter.java <arquivo_saida> <offset_hex> [tamanho_max_bytes]
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FindFieldSetter extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		String needle = "[ECX + 0x" + args[1].toLowerCase().replace("0x", "") + "]";
		int maxSize = args.length > 2 ? Integer.parseInt(args[2]) : 32;
		Memory mem = currentProgram.getMemory();
		MemoryBlock text = mem.getBlock(".text");
		ReferenceManager rm = currentProgram.getReferenceManager();

		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (monitor.isCancelled()) break;
			if (f.getBody().getNumAddresses() > maxSize) continue;
			boolean writes = false;
			StringBuilder body = new StringBuilder();
			for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
				String s = ins.toString();
				body.append("    ").append(s).append('\n');
				if (ins.getMnemonicString().startsWith("MOV") && s.toLowerCase().contains(needle.toLowerCase())
						&& ins.getDefaultOperandRepresentation(0).toLowerCase().contains(needle.toLowerCase()))
					writes = true;
			}
			if (!writes) continue;
			out.println("== setter candidato " + f.getEntryPoint());
			out.print(body);
			// Entradas de vtable: referências de dados ao início da função.
			for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
				Address slot = r.getFromAddress();
				if (text.contains(slot)) continue;  // chamada direta, não vtable
				// Volta enquanto as entradas anteriores apontam para .text.
				Address start = slot;
				for (int i = 0; i < 400; i++) {
					Address prev = start.subtract(4);
					int v = mem.getInt(prev);
					if (!text.contains(toAddr(v & 0xffffffffL))) break;
					start = prev;
					if (rm.getReferenceCountTo(start) > 0 && hasCodeRef(rm, start, text)) break;
				}
				out.printf("    vtable entry %s; inicio provavel %s; offset +0x%x%n", slot, start,
						slot.subtract(start));
			}
		}
		out.close();
	}

	boolean hasCodeRef(ReferenceManager rm, Address a, MemoryBlock text) {
		for (Reference r : rm.getReferencesTo(a)) if (text.contains(r.getFromAddress())) return true;
		return false;
	}
}
