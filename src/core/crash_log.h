// Registro de fechamento: quando o jogo cai por uma exceção não tratada, grava no log
// do mod o código, o endereço, os registradores e os valores da pilha que parecem
// endereços do exe (o jogo nem sempre grava o próprio .dmp). Encadeia o filtro que já
// estava instalado (o do CryEngine, que grava o .dmp quando funciona).
#pragma once

namespace sodcoop::crash_log {

// Instala (ou reinstala, se o jogo trocou o filtro depois) o filtro de exceções.
// Pode ser chamado a cada frame; só age quando o filtro atual não é o nosso.
void ensureInstalled();

}  // namespace sodcoop::crash_log
