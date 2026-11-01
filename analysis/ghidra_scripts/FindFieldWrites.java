// Lista instruções MOV que gravam em [<reg> + <offset>], com a função e o
// tamanho dela, para localizar setters de um campo.
// Uso: -postScript FindFieldWrites.java <arquivo_saida> <offset_hex> [max_resultados]
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class FindFieldWrites extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		String needle = " + 0x" + args[1].toLowerCase().replace("0x", "") + "]";
		int max = args.length > 2 ? Integer.parseInt(args[2]) : 200;
		int n = 0;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext() && n < max && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (!ins.getMnemonicString().startsWith("MOV") || ins.getNumOperands() != 2) continue;
			String dst = ins.getDefaultOperandRepresentation(0).toLowerCase();
			if (!dst.endsWith(needle)) continue;
			Function f = getFunctionContaining(ins.getAddress());
			out.printf("%s  %-40s  em %s (tam %d)%n", ins.getAddress(), ins,
					f == null ? "<sem funcao>" : f.getEntryPoint().toString(),
					f == null ? -1 : f.getBody().getNumAddresses());
			n++;
		}
		out.close();
	}
}
