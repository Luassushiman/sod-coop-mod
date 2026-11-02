// Imprime as primeiras N instruções a partir de cada endereço dado.
// Uso: -postScript PrintAsm.java <arquivo_saida> <n> <addr1> [addr2 ...]
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import java.io.*;

public class PrintAsm extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		int n = Integer.parseInt(args[1]);
		for (int i = 2; i < args.length; i++) {
			out.println("##### " + args[i]);
			Instruction ins = getInstructionAt(toAddr(Long.parseLong(args[i].replace("0x", ""), 16)));
			for (int k = 0; k < n && ins != null; k++, ins = ins.getNext())
				out.println("  " + ins.getAddress() + "  " + ins);
		}
		out.close();
	}
}
