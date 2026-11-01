// Lista instruções que LEEM [<reg> + <offset>] (MOV/CMP/TEST/MOVZX...) dentro
// de uma faixa de endereços, com a função e o tamanho dela.
// Uso: -postScript FindFieldReads.java <arquivo_saida> <offset_hex> <ini_hex> <fim_hex>
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class FindFieldReads extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		String needle = " + 0x" + args[1].toLowerCase().replace("0x", "") + "]";
		long lo = Long.parseLong(args[2], 16), hi = Long.parseLong(args[3], 16);
		InstructionIterator it = currentProgram.getListing().getInstructions(toAddr(lo), true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (ins.getAddress().getOffset() >= hi) break;
			for (int op = 0; op < ins.getNumOperands(); op++) {
				String rep = ins.getDefaultOperandRepresentation(op).toLowerCase();
				if (!rep.endsWith(needle)) continue;
				boolean isWrite = op == 0 && ins.getMnemonicString().startsWith("MOV");
				if (isWrite) continue;
				Function f = getFunctionContaining(ins.getAddress());
				out.printf("%s  %-44s  em %s (tam %d)%n", ins.getAddress(), ins,
						f == null ? "<sem funcao>" : f.getEntryPoint().toString(),
						f == null ? -1 : f.getBody().getNumAddresses());
			}
		}
		out.close();
	}
}
